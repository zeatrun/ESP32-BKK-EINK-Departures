#include "data_layer.h"

#include "display_manager.h"

DataLayer g_dataLayer;

bool DataLayer::departureEquals(const Departure& a, const Departure& b)
{
    return strncmp(a.line, b.line, sizeof(a.line)) == 0
        && strncmp(a.routeIdText, b.routeIdText, sizeof(a.routeIdText)) == 0
        && strncmp(a.destination, b.destination, sizeof(a.destination)) == 0
        && strncmp(a.stopName, b.stopName, sizeof(a.stopName)) == 0
        && a.minutes == b.minutes
        && a.timestamp == b.timestamp;
}

bool DataLayer::departuresEqual(const Departure* a, int aCount, const Departure* b, int bCount)
{
    if (aCount != bCount)
    {
        return false;
    }

    for (int i = 0; i < aCount; ++i)
    {
        if (!departureEquals(a[i], b[i]))
        {
            return false;
        }
    }

    return true;
}

bool DataLayer::weatherEquals(const WeatherData& a, const WeatherData& b)
{
    if (strncmp(a.source, b.source, sizeof(a.source)) != 0
        || strncmp(a.publishedAtUtc, b.publishedAtUtc, sizeof(a.publishedAtUtc)) != 0
        || strncmp(a.locationName, b.locationName, sizeof(a.locationName)) != 0
        || strncmp(a.locationAdmin1, b.locationAdmin1, sizeof(a.locationAdmin1)) != 0
        || strncmp(a.locationCountry, b.locationCountry, sizeof(a.locationCountry)) != 0
        || a.latitude != b.latitude
        || a.longitude != b.longitude
        || strncmp(a.timezone, b.timezone, sizeof(a.timezone)) != 0
        || strncmp(a.currentTime, b.currentTime, sizeof(a.currentTime)) != 0
        || a.temperatureC != b.temperatureC
        || a.apparentTemperatureC != b.apparentTemperatureC
        || a.relativeHumidity != b.relativeHumidity
        || a.weatherCode != b.weatherCode
        || a.windSpeedKmh != b.windSpeedKmh
        || a.windDirectionDeg != b.windDirectionDeg
        || a.isDay != b.isDay
        || a.dailyCount != b.dailyCount)
    {
        return false;
    }

    for (int i = 0; i < a.dailyCount; ++i)
    {
        const WeatherDay& da = a.daily[i];
        const WeatherDay& db = b.daily[i];
        if (strncmp(da.date, db.date, sizeof(da.date)) != 0
            || da.weatherCode != db.weatherCode
            || da.tempMaxC != db.tempMaxC
            || da.tempMinC != db.tempMinC
            || da.precipMm != db.precipMm
            || da.precipProbMax != db.precipProbMax)
        {
            return false;
        }
    }

    return true;
}

bool DataLayer::applyDepartures(const Departure* buses,
                                int busCount,
                                const Departure* trains,
                                int trainCount)
{
    if (buses == nullptr || trains == nullptr)
    {
        return false;
    }

    if (busCount < 0) busCount = 0;
    if (trainCount < 0) trainCount = 0;
    if (busCount > MAX_DEPARTURES) busCount = MAX_DEPARTURES;
    if (trainCount > MAX_DEPARTURES) trainCount = MAX_DEPARTURES;

    bool changed = false;

    if (xSemaphoreTake(g_departuresMutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        Serial.println("[DATA_LAYER] Failed to lock departures mutex");
        return false;
    }

    const bool busesSame = departuresEqual(g_busDepartures, g_busCount, buses, busCount);
    const bool trainsSame = departuresEqual(g_trainDepartures, g_trainCount, trains, trainCount);
    const bool firstValid = !g_departuresValid;

    changed = firstValid || !(busesSame && trainsSame);

    g_busCount = busCount;
    g_trainCount = trainCount;
    g_departuresValid = true;

    for (int i = 0; i < busCount; ++i)
    {
        g_busDepartures[i] = buses[i];
    }
    for (int i = busCount; i < MAX_DEPARTURES; ++i)
    {
        g_busDepartures[i] = {};
    }

    for (int i = 0; i < trainCount; ++i)
    {
        g_trainDepartures[i] = trains[i];
    }
    for (int i = trainCount; i < MAX_DEPARTURES; ++i)
    {
        g_trainDepartures[i] = {};
    }

    xSemaphoreGive(g_departuresMutex);

    if (changed)
    {
        Serial.printf("[DATA_LAYER] Departures applied: buses=%d trains=%d\n", busCount, trainCount);
        displayNotifyDataChanged();
    }

    return true;
}

bool DataLayer::applyWeather(const WeatherData& data)
{
    bool changed = false;

    if (xSemaphoreTake(g_weatherMutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        Serial.println("[DATA_LAYER] Failed to lock weather mutex");
        return false;
    }

    const bool firstValid = !g_weatherValid;
    changed = firstValid || !weatherEquals(g_weatherData, data);

    g_weatherData = data;
    g_weatherValid = true;

    xSemaphoreGive(g_weatherMutex);

    if (changed)
    {
        Serial.printf("[DATA_LAYER] Weather applied: temp=%.1f code=%d daily=%d\n",
                      data.temperatureC,
                      data.weatherCode,
                      data.dailyCount);
        displayNotifyDataChanged();
    }

    return true;
}
