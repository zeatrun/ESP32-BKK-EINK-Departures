#pragma once

/**
 * @file battery_monitor.h
 * @brief Li-Ion battery voltage monitoring for the XIAO ePaper Display Board EE04.
 *
 * Monitors the battery connected to GPIO1_D0 (A0) via a 15-second periodic FreeRTOS task.
 * Voltage is read through a resistor divider on the board (scale factor 7.16),
 * smoothed with an exponential low-pass filter, and mapped to one of 7 charge bands.
 *
 * On band transitions, display_manager is notified via displayNotifyBatteryBandChanged().
 *
 * Usage:
 *   batteryMonitorInit();  // call once in setup(), after displayBegin()
 */

#include <Arduino.h>

/**
 * @brief Discretised charge level of the Li-Ion battery.
 *
 * Voltage thresholds (linear map, 3.10 V = 0 %, 4.20 V = 100 %):
 *
 *   NoBattery       — measured voltage below 1.80 V (no battery or disconnected)
 *   Charging        — voltage rising steadily over multiple samples
 *   Percent100To80  — 80–100 %  (≈ 4.05–4.20 V)
 *   Percent79To60   — 60–79 %  (≈ 3.86–4.04 V)
 *   Percent59To40   — 40–59 %  (≈ 3.67–3.85 V)
 *   Percent39To20   — 20–39 %  (≈ 3.48–3.66 V)
 *   Percent19To10   — 10–19 %  (≈ 3.29–3.47 V)
 *   Percent10OrLess — 0–9 %   (≈ 3.10–3.28 V)
 */
enum class BatteryBand : uint8_t
{
    NoBattery = 0,
    Charging,
    Percent100To80,
    Percent79To60,
    Percent59To40,
    Percent39To20,
    Percent19To10,
    Percent10OrLess
};

/**
 * @brief Initialise the battery monitor and start the monitoring task.
 *
 * Configures the ADC pin, performs 6 warm-up readings (100 ms apart) to seed
 * the low-pass filter, determines the initial band, and spawns the periodic
 * FreeRTOS task on Core 1.
 *
 * Must be called once in setup(), after Serial and display have been initialised.
 */
void batteryMonitorInit();

/**
 * @brief Return the last computed charge band.
 *
 * Thread-safe (volatile read). Updated every 15 seconds by the monitoring task.
 */
BatteryBand batteryMonitorGetCurrentBand();

/**
 * @brief Return the last low-pass-filtered battery voltage in volts.
 *
 * Thread-safe (volatile read).
 */
float batteryMonitorGetVoltage();

/**
 * @brief Return the estimated charge level as a percentage (0–100).
 *
 * Derived from a linear mapping between BATTERY_EMPTY_V (3.10 V = 0 %)
 * and BATTERY_FULL_V (4.20 V = 100 %). Thread-safe (volatile read).
 */
int batteryMonitorGetPercent();

/**
 * @brief Return true when the battery is currently detected as charging.
 *
 * Charging is inferred heuristically: the filtered voltage must rise by at
 * least 0.02 V across CHARGING_STREAK_REQUIRED (3) consecutive samples.
 * Thread-safe (volatile read).
 */
bool batteryMonitorIsCharging();

/**
 * @brief Return a short human-readable label for the given band.
 *
 * Examples: "100-80%", "Charging", "No battery".
 * The returned pointer is a string literal — do not free it.
 *
 * @param band  The band to convert.
 * @return      Null-terminated ASCII string.
 */
const char* batteryBandToString(BatteryBand band);
