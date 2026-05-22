#pragma once

#include <Arduino.h>
#include <freertos/semphr.h>

#define MAX_WEATHER_DAYS 4

struct WeatherDay
{
    char date[16];
    int  weatherCode;
    float tempMaxC;
    float tempMinC;
    float precipMm;
    int   precipProbMax;
};

struct WeatherData
{
    char source[24];
    char publishedAtUtc[28];

    char locationName[48];
    char locationAdmin1[48];
    char locationCountry[16];
    float latitude;
    float longitude;
    char timezone[40];

    char currentTime[28];
    float temperatureC;
    float apparentTemperatureC;
    int   relativeHumidity;
    int   weatherCode;
    float windSpeedKmh;
    int   windDirectionDeg;
    int   isDay;

    WeatherDay daily[MAX_WEATHER_DAYS];
    int dailyCount;
};

extern SemaphoreHandle_t g_weatherMutex;
extern WeatherData       g_weatherData;
extern bool              g_weatherValid;

void weatherInit();
