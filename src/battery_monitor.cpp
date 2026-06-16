#include "battery_monitor.h"

#include "display_manager.h"

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

constexpr float CHARGING_MIN_RISE_V = 0.02F;
constexpr uint8_t CHARGING_STREAK_REQUIRED = 3;

TaskHandle_t s_batteryTaskHandle = nullptr;

volatile BatteryBand s_currentBand = BatteryBand::Percent100To80;
volatile float s_voltage = 0.0F;
volatile int s_percent = 0;
volatile bool s_isCharging = false;
volatile bool s_hasInitialBand = false;

float readBatteryVoltageOnce()
{
    analogReadResolution(12);

    uint32_t sum = 0;
    for (uint8_t i = 0; i < BATTERY_SAMPLE_COUNT; ++i)
    {
        // Seeed EE04 recommendation: short delay before analogRead for precision.
        delay(10);
        sum += static_cast<uint32_t>(analogRead(BATTERY_VOLTAGE_PIN));
    }

    const float avgAdc = static_cast<float>(sum) / static_cast<float>(BATTERY_SAMPLE_COUNT);
    return (avgAdc / 4096.0F) * BATTERY_ADC_SCALE;
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

BatteryBand percentToBand(int percent, bool isCharging)
{
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

    for (;;)
    {
        const float measuredVoltage = readBatteryVoltageOnce();
        const int measuredPercent = voltageToPercent(measuredVoltage);

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
        const BatteryBand nextBand = percentToBand(measuredPercent, chargingNow);

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
