#include "battery_monitor.h"

#include "display_manager.h"
#include "low_pass_filter.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace
{
#ifndef A0
constexpr uint8_t BATTERY_VOLTAGE_PIN = 1; // GPIO1_D0
#else
constexpr uint8_t BATTERY_VOLTAGE_PIN = A0;
#endif

constexpr uint32_t BATTERY_SAMPLE_INTERVAL_MS = 15000;
constexpr uint8_t BATTERY_SAMPLE_COUNT = 8;
constexpr float BATTERY_ADC_SCALE = 7.16F;

constexpr float BATTERY_EMPTY_V = 3.10F;
constexpr float BATTERY_FULL_V = 4.20F;
constexpr float BATTERY_DISCONNECT_THRESHOLD_V = 1.80F;  // Below this: no battery or disconnected
constexpr float BATTERY_RECOVERY_THRESHOLD_V = 2.50F;   // Hysteresis: need this to recover from NoBattery

constexpr float CHARGING_MIN_RISE_V = 0.02F;
constexpr uint8_t CHARGING_STREAK_REQUIRED = 3;
constexpr uint8_t NO_BATTERY_CONFIRM_STREAK = 5;         // 5 consecutive low readings: confirm no battery

TaskHandle_t s_batteryTaskHandle = nullptr;

volatile BatteryBand s_currentBand = BatteryBand::Percent100To80;
volatile float s_voltage = 0.0F;
volatile int s_percent = 0;
volatile bool s_isCharging = false;
volatile bool s_hasInitialBand = false;
volatile bool s_noBatteryConfirmed = false;  // Hysteresis: debounce battery disconnection

// Low-pass filter for voltage smoothing (reject noise and spurious values).
// alpha=0.15: good balance between response speed and stability (~3-5 sample average).
static LowPassFilter s_voltageFilter(2.0F, 0.15F);

/** Raw ADC measurement result. */
struct BatteryReading
{
    float voltage;    ///< Voltage converted from the ADC reading (V).
    uint16_t rawAdc;  ///< Average of BATTERY_SAMPLE_COUNT raw 12-bit ADC values.
};

/**
 * @brief Sample the battery voltage pin and return the averaged result.
 *
 * Takes BATTERY_SAMPLE_COUNT readings with a 10 ms settle delay each,
 * averages them, and converts to volts using the board's resistor-divider
 * scale factor (BATTERY_ADC_SCALE = 7.16).
 *
 * @return BatteryReading with the converted voltage and the average raw ADC value.
 */
BatteryReading readBatteryVoltageOnce()
{
    analogReadResolution(12);

    uint32_t sum = 0;
    for (uint8_t i = 0; i < BATTERY_SAMPLE_COUNT; ++i)
    {
        // Seeed EE04 recommendation: short delay before analogRead for precision.
        delay(10);
        sum += static_cast<uint32_t>(analogRead(BATTERY_VOLTAGE_PIN));
    }

    const uint16_t avgAdc = static_cast<uint16_t>(sum / BATTERY_SAMPLE_COUNT);
    const float voltage = (static_cast<float>(avgAdc) / 4096.0F) * BATTERY_ADC_SCALE;
    return {voltage, avgAdc};
}

/**
 * @brief Convert a filtered battery voltage to a charge percentage.
 *
 * Uses a linear mapping between BATTERY_EMPTY_V (3.10 V = 0 %) and
 * BATTERY_FULL_V (4.20 V = 100 %). Values outside this range are clamped.
 *
 * @param voltage  Filtered battery voltage in volts.
 * @return         Charge percentage in the range [0, 100].
 */
int voltageToPercent(float voltage)
{
    const float clamped = (voltage < BATTERY_EMPTY_V)
                              ? BATTERY_EMPTY_V
                              : ((voltage > BATTERY_FULL_V) ? BATTERY_FULL_V : voltage);

    const float ratio = (clamped - BATTERY_EMPTY_V) / (BATTERY_FULL_V - BATTERY_EMPTY_V);
    int percent = static_cast<int>(ratio * 100.0F + 0.5F);

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return percent;
}

/**
 * @brief Map a voltage/percent/charging state to a BatteryBand.
 *
 * Applies two-threshold hysteresis for the NoBattery state to prevent
 * chatter when the voltage is near the disconnection boundary:
 *   - Enter NoBattery when voltage < BATTERY_DISCONNECT_THRESHOLD_V (1.80 V)
 *   - Leave NoBattery only when voltage >= BATTERY_RECOVERY_THRESHOLD_V (2.50 V)
 *
 * The noBatteryConfirmed flag is maintained externally with a debounce streak
 * (NO_BATTERY_CONFIRM_STREAK consecutive low readings required).
 *
 * @param percent             Estimated charge percentage.
 * @param isCharging          True when a rising voltage streak has been detected.
 * @param voltage             Current filtered voltage (used for disconnect detection).
 * @param noBatteryConfirmed  True when the NoBattery state has been debounce-confirmed.
 * @return                    The corresponding BatteryBand.
 */
BatteryBand percentToBand(int percent, bool isCharging, float voltage, bool noBatteryConfirmed)
{
    // Hysteresis: battery disconnection detection with debounce.
    // If already detected as NoBattery, require higher voltage to recover (prevents chatter).
    if (noBatteryConfirmed)
    {
        if (voltage >= BATTERY_RECOVERY_THRESHOLD_V)
        {
            // Battery has been reconnected.
        }
        else
        {
            // Still no battery.
            return BatteryBand::NoBattery;
        }
    }
    else
    {
        // First detection: lower threshold.
        if (voltage < BATTERY_DISCONNECT_THRESHOLD_V)
        {
            return BatteryBand::NoBattery;
        }
    }

    if (isCharging)
    {
        return BatteryBand::Charging;
    }

    if (percent >= 80) return BatteryBand::Percent100To80;
    if (percent >= 60) return BatteryBand::Percent79To60;
    if (percent >= 40) return BatteryBand::Percent59To40;
    if (percent >= 20) return BatteryBand::Percent39To20;
    if (percent >= 10) return BatteryBand::Percent19To10;
    return BatteryBand::Percent10OrLess;
}

/**
 * @brief FreeRTOS task: periodically reads, filters, and evaluates battery state.
 *
 * Runs every BATTERY_SAMPLE_INTERVAL_MS (15 s) on Core 1.
 * On each tick:
 *   1. Reads raw voltage and passes it through the low-pass filter.
 *   2. Updates the NoBattery debounce streak.
 *   3. Tracks a rising-voltage streak to infer charging.
 *   4. Recomputes the BatteryBand; notifies display_manager on band changes.
 *   5. Writes the current state to the serial log.
 */
void batteryTask(void* /*pvParameters*/)
{
    float lastVoltage = 0.0F;
    uint8_t risingStreak = 0;
    uint8_t noBatteryStreak = 0;  // Debounce battery disconnection.

    for (;;)
    {
        const BatteryReading reading = readBatteryVoltageOnce();
        const float rawVoltage = reading.voltage;
        const float measuredVoltage = s_voltageFilter.update(rawVoltage);
        const int measuredPercent = voltageToPercent(measuredVoltage);

        // Debounce: only confirm no battery after 5 consecutive low readings.
        if (measuredVoltage < BATTERY_DISCONNECT_THRESHOLD_V)
        {
            if (noBatteryStreak < 255)
            {
                ++noBatteryStreak;
            }

            if (noBatteryStreak >= NO_BATTERY_CONFIRM_STREAK)
            {
                s_noBatteryConfirmed = true;
            }
        }
        else
        {
            noBatteryStreak = 0;
            // Good value received: can clear the NoBattery flag.
            if (measuredVoltage >= BATTERY_RECOVERY_THRESHOLD_V)
            {
                s_noBatteryConfirmed = false;
            }
        }

        // Charging streak: detect rising voltage pattern.
        if (lastVoltage > 0.0F && (measuredVoltage - lastVoltage) >= CHARGING_MIN_RISE_V)
        {
            if (risingStreak < 255)
            {
                ++risingStreak;
            }
        }
        else
        {
            risingStreak = 0;
        }

        const bool chargingNow = (risingStreak >= CHARGING_STREAK_REQUIRED);
        const BatteryBand nextBand = percentToBand(measuredPercent, chargingNow, measuredVoltage, s_noBatteryConfirmed);

        const bool bandChanged = (!s_hasInitialBand) || (nextBand != s_currentBand);

        s_voltage = measuredVoltage;
        s_percent = measuredPercent;
        s_isCharging = chargingNow;

        if (bandChanged)
        {
            if (s_hasInitialBand)
            {
                displayNotifyBatteryBandChanged(nextBand, measuredVoltage, measuredPercent);
            }

            s_currentBand = nextBand;
            s_hasInitialBand = true;
        }

        logBatteryStatus("Periodic", s_currentBand, measuredVoltage, measuredPercent);

        lastVoltage = measuredVoltage;
        vTaskDelay(pdMS_TO_TICKS(BATTERY_SAMPLE_INTERVAL_MS));
    }
}

/**
 * @brief Print a one-line battery status line to Serial.
 *
 * Format: [BATTERY] <context>: <band>, <voltage> V, <percent>%
 * Example: [BATTERY] Periodic: 100-80%, 4.05 V, 87%
 *
 * @param context  Short label identifying the call site (e.g. "Init", "Periodic").
 * @param band     Current battery band.
 * @param voltage  Filtered voltage in volts.
 * @param percent  Estimated charge percentage.
 */
void logBatteryStatus(const char* context, BatteryBand band, float voltage, int percent)
{
    Serial.printf("[BATTERY] %s: %s, %.2f V, %d%%\n",
                  context,
                  batteryBandToString(band),
                  static_cast<double>(voltage),
                  percent);
}

} // namespace

void batteryMonitorInit()
{
    pinMode(BATTERY_VOLTAGE_PIN, INPUT);

    // Perform quick initial readings to warm up the filter before task starts.
    // This ensures we have a good voltage value from startup (6 measurements, 100ms apart).
    for (uint8_t i = 0; i < 6; ++i)
    {
        const BatteryReading reading = readBatteryVoltageOnce();
        const float filteredVoltage = s_voltageFilter.update(reading.voltage);
        s_voltage = filteredVoltage;
        s_percent = voltageToPercent(filteredVoltage);

        if (i < 5)  // Don't delay after the last measurement.
        {
            delay(100);
        }
    }

    // Initialize with first band detection.
    s_currentBand = percentToBand(s_percent, false, s_voltage, false);
    s_hasInitialBand = true;

    logBatteryStatus("Init", s_currentBand, s_voltage, s_percent);

    if (s_batteryTaskHandle == nullptr)
    {
        xTaskCreatePinnedToCore(
            batteryTask,
            "BatteryTask",
            3072,
            nullptr,
            1,
            &s_batteryTaskHandle,
            1);
    }
}

BatteryBand batteryMonitorGetCurrentBand()
{
    return s_currentBand;
}

float batteryMonitorGetVoltage()
{
    return s_voltage;
}

int batteryMonitorGetPercent()
{
    return s_percent;
}

bool batteryMonitorIsCharging()
{
    return s_isCharging;
}

const char* batteryBandToString(BatteryBand band)
{
    switch (band)
    {
        case BatteryBand::NoBattery:       return "No battery";
        case BatteryBand::Charging:        return "Charging";
        case BatteryBand::Percent100To80:  return "100-80%";
        case BatteryBand::Percent79To60:   return "79-60%";
        case BatteryBand::Percent59To40:   return "59-40%";
        case BatteryBand::Percent39To20:   return "39-20%";
        case BatteryBand::Percent19To10:   return "19-10%";
        case BatteryBand::Percent10OrLess: return "<=10%";
        default:                           return "Unknown";
    }
}
