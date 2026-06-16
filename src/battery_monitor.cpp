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

#ifndef A5
constexpr int8_t BATTERY_ADC_ENABLE_PIN = -1;
#else
constexpr int8_t BATTERY_ADC_ENABLE_PIN = A5; // GPIO6
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

struct BatteryReading
{
    float voltage;
    uint16_t rawAdc;
};

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

        lastVoltage = measuredVoltage;
        vTaskDelay(pdMS_TO_TICKS(BATTERY_SAMPLE_INTERVAL_MS));
    }
}
} // namespace

void batteryMonitorInit()
{
    pinMode(BATTERY_VOLTAGE_PIN, INPUT);

    if (BATTERY_ADC_ENABLE_PIN >= 0)
    {
        pinMode(BATTERY_ADC_ENABLE_PIN, OUTPUT);
        digitalWrite(BATTERY_ADC_ENABLE_PIN, HIGH);
    }

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

    Serial.printf("[BATTERY] Init complete. Starting at: %.2f V, %d%%, Band: %u\n",
                  static_cast<double>(s_voltage),
                  s_percent,
                  static_cast<unsigned int>(s_currentBand));

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
