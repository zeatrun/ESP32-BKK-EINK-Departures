#pragma once

#include <Arduino.h>

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

void batteryMonitorInit();
BatteryBand batteryMonitorGetCurrentBand();
float batteryMonitorGetVoltage();
int batteryMonitorGetPercent();
bool batteryMonitorIsCharging();
