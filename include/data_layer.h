#pragma once

#include <Arduino.h>
#include "departures.h"
#include "weather.h"

class DataLayer
{
public:
    bool applyWeather(const WeatherData& data);
    bool applyDepartures(const Departure* buses,
                         int busCount,
                         const Departure* trains,
                         int trainCount);

private:
    static bool weatherEquals(const WeatherData& a, const WeatherData& b);
    static bool departureEquals(const Departure& a, const Departure& b);
    static bool departuresEqual(const Departure* a, int aCount, const Departure* b, int bCount);
};

extern DataLayer g_dataLayer;
