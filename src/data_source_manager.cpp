#include "data_source_manager.h"

#include "departures.h"
#include "display_manager.h"
#include "mqtt_manager.h"
#include "weather.h"

// Matches wifi_manager.cpp
#define WIFI_CONNECTED_BIT BIT0

namespace
{
constexpr TickType_t API_POLL_INTERVAL_TICKS = pdMS_TO_TICKS(30UL * 1000UL);

EventGroupHandle_t s_wifiEventGroup = nullptr;
SemaphoreHandle_t  s_clientMutex = nullptr;
WiFiClient*        s_espClient = nullptr;

Configuration::DataSourceMode s_mode = Configuration::DataSourceMode::Mqtt;
portMUX_TYPE s_stateMux = portMUX_INITIALIZER_UNLOCKED;
bool s_connected = false;
bool s_hasLastUpdate = false;
time_t s_lastUpdateTs = 0;

bool departureEquals(const Departure& a, const Departure& b)
{
    return strncmp(a.line, b.line, sizeof(a.line)) == 0
        && strncmp(a.routeIdText, b.routeIdText, sizeof(a.routeIdText)) == 0
        && strncmp(a.destination, b.destination, sizeof(a.destination)) == 0
        && strncmp(a.stopName, b.stopName, sizeof(a.stopName)) == 0
        && a.minutes == b.minutes
        && a.timestamp == b.timestamp;
}

bool departuresEqual(const Departure* a, int aCount, const Departure* b, int bCount)
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

bool weatherDayEquals(const WeatherDay& a, const WeatherDay& b)
{
    return strncmp(a.date, b.date, sizeof(a.date)) == 0
        && a.weatherCode == b.weatherCode
        && a.tempMaxC == b.tempMaxC
        && a.tempMinC == b.tempMinC
        && a.precipMm == b.precipMm
        && a.precipProbMax == b.precipProbMax;
}

bool weatherEquals(const WeatherData& a, const WeatherData& b)
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
        if (!weatherDayEquals(a.daily[i], b.daily[i]))
        {
            return false;
        }
    }

    return true;
}

void setStatusConnected(bool connected)
{
    taskENTER_CRITICAL(&s_stateMux);
    s_connected = connected;
    taskEXIT_CRITICAL(&s_stateMux);
}

void markLastUpdateNow()
{
    taskENTER_CRITICAL(&s_stateMux);
    s_lastUpdateTs = time(nullptr);
    s_hasLastUpdate = true;
    taskEXIT_CRITICAL(&s_stateMux);
}

int clampMinutes(int value)
{
    if (value < 1)
    {
        return 1;
    }
    return value;
}

bool applyDummyDepartures(uint32_t tickIndex)
{
    Departure nextBuses[MAX_DEPARTURES] = {};
    Departure nextTrains[MAX_DEPARTURES] = {};

    const int drift = static_cast<int>(tickIndex % 3U);
    const time_t nowTs = time(nullptr);

    int nextBusCount = 2;
    strlcpy(nextBuses[0].line, "830", sizeof(nextBuses[0].line));
    strlcpy(nextBuses[0].routeIdText, "Dummy BKK bus", sizeof(nextBuses[0].routeIdText));
    strlcpy(nextBuses[0].destination, "Budapest", sizeof(nextBuses[0].destination));
    strlcpy(nextBuses[0].stopName, "Pilisvorosvar", sizeof(nextBuses[0].stopName));
    nextBuses[0].minutes = clampMinutes(7 - drift);
    nextBuses[0].timestamp = static_cast<unsigned long>(nowTs + (nextBuses[0].minutes * 60));

    strlcpy(nextBuses[1].line, "931", sizeof(nextBuses[1].line));
    strlcpy(nextBuses[1].routeIdText, "Dummy BKK bus", sizeof(nextBuses[1].routeIdText));
    strlcpy(nextBuses[1].destination, "Szentendre", sizeof(nextBuses[1].destination));
    strlcpy(nextBuses[1].stopName, "Pilisvorosvar", sizeof(nextBuses[1].stopName));
    nextBuses[1].minutes = clampMinutes(14 - drift);
    nextBuses[1].timestamp = static_cast<unsigned long>(nowTs + (nextBuses[1].minutes * 60));

    int nextTrainCount = 2;
    strlcpy(nextTrains[0].line, "Z72", sizeof(nextTrains[0].line));
    strlcpy(nextTrains[0].routeIdText, "Dummy MAV", sizeof(nextTrains[0].routeIdText));
    strlcpy(nextTrains[0].destination, "Nyugati", sizeof(nextTrains[0].destination));
    strlcpy(nextTrains[0].stopName, "Pilisvorosvar", sizeof(nextTrains[0].stopName));
    nextTrains[0].minutes = clampMinutes(5 - drift);
    nextTrains[0].timestamp = static_cast<unsigned long>(nowTs + (nextTrains[0].minutes * 60));

    strlcpy(nextTrains[1].line, "S76", sizeof(nextTrains[1].line));
    strlcpy(nextTrains[1].routeIdText, "Dummy MAV", sizeof(nextTrains[1].routeIdText));
    strlcpy(nextTrains[1].destination, "Esztergom", sizeof(nextTrains[1].destination));
    strlcpy(nextTrains[1].stopName, "Pilisvorosvar", sizeof(nextTrains[1].stopName));
    nextTrains[1].minutes = clampMinutes(18 - drift);
    nextTrains[1].timestamp = static_cast<unsigned long>(nowTs + (nextTrains[1].minutes * 60));

    bool changed = false;

    if (xSemaphoreTake(g_departuresMutex, pdMS_TO_TICKS(200)) == pdTRUE)
    {
        changed = !g_departuresValid
            || !departuresEqual(g_busDepartures, g_busCount, nextBuses, nextBusCount)
            || !departuresEqual(g_trainDepartures, g_trainCount, nextTrains, nextTrainCount);

        g_busCount = nextBusCount;
        g_trainCount = nextTrainCount;
        g_departuresValid = true;

        for (int i = 0; i < MAX_DEPARTURES; ++i)
        {
            g_busDepartures[i] = {};
            g_trainDepartures[i] = {};
        }
        for (int i = 0; i < nextBusCount; ++i)
        {
            g_busDepartures[i] = nextBuses[i];
        }
        for (int i = 0; i < nextTrainCount; ++i)
        {
            g_trainDepartures[i] = nextTrains[i];
        }

        xSemaphoreGive(g_departuresMutex);
    }

    return changed;
}

bool applyDummyWeather(uint32_t tickIndex)
{
    WeatherData next = {};

    const int tempOffset = static_cast<int>(tickIndex % 3U) - 1;

    strlcpy(next.source, "direct_api_dummy", sizeof(next.source));
    strlcpy(next.publishedAtUtc, "dummy", sizeof(next.publishedAtUtc));

    strlcpy(next.locationName, "Pilisvorosvar", sizeof(next.locationName));
    strlcpy(next.locationAdmin1, "Pest", sizeof(next.locationAdmin1));
    strlcpy(next.locationCountry, "HU", sizeof(next.locationCountry));
    next.latitude = 47.62F;
    next.longitude = 18.90F;
    strlcpy(next.timezone, "Europe/Budapest", sizeof(next.timezone));

    strlcpy(next.currentTime, "dummy", sizeof(next.currentTime));
    next.temperatureC = 25.0F + static_cast<float>(tempOffset);
    next.apparentTemperatureC = next.temperatureC + 0.5F;
    next.relativeHumidity = 48 + tempOffset;
    next.weatherCode = 2;
    next.windSpeedKmh = 14.0F;
    next.windDirectionDeg = 220;
    next.isDay = 1;

    next.dailyCount = MAX_WEATHER_DAYS;
    strlcpy(next.daily[0].date, "Mon", sizeof(next.daily[0].date));
    next.daily[0].weatherCode = 2;
    next.daily[0].tempMaxC = 27.0F;
    next.daily[0].tempMinC = 17.0F;
    next.daily[0].precipMm = 0.4F;
    next.daily[0].precipProbMax = 20;

    strlcpy(next.daily[1].date, "Tue", sizeof(next.daily[1].date));
    next.daily[1].weatherCode = 3;
    next.daily[1].tempMaxC = 28.0F;
    next.daily[1].tempMinC = 18.0F;
    next.daily[1].precipMm = 1.8F;
    next.daily[1].precipProbMax = 35;

    strlcpy(next.daily[2].date, "Wed", sizeof(next.daily[2].date));
    next.daily[2].weatherCode = 61;
    next.daily[2].tempMaxC = 23.0F;
    next.daily[2].tempMinC = 16.0F;
    next.daily[2].precipMm = 7.2F;
    next.daily[2].precipProbMax = 80;

    strlcpy(next.daily[3].date, "Thu", sizeof(next.daily[3].date));
    next.daily[3].weatherCode = 2;
    next.daily[3].tempMaxC = 26.0F;
    next.daily[3].tempMinC = 15.0F;
    next.daily[3].precipMm = 0.1F;
    next.daily[3].precipProbMax = 15;

    bool changed = false;

    if (xSemaphoreTake(g_weatherMutex, pdMS_TO_TICKS(200)) == pdTRUE)
    {
        changed = !g_weatherValid || !weatherEquals(g_weatherData, next);
        g_weatherData = next;
        g_weatherValid = true;
        xSemaphoreGive(g_weatherMutex);
    }

    return changed;
}

void directApiTask(void* /*pvParameters*/)
{
    uint32_t tickIndex = 0;
    bool firstApplied = false;

    for (;;)
    {
        if (s_wifiEventGroup == nullptr)
        {
            setStatusConnected(false);
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        const EventBits_t bits = xEventGroupGetBits(s_wifiEventGroup);
        const bool wifiUp = (bits & WIFI_CONNECTED_BIT) != 0;

        if (!wifiUp)
        {
            setStatusConnected(false);
            xEventGroupWaitBits(s_wifiEventGroup,
                                WIFI_CONNECTED_BIT,
                                pdFALSE,
                                pdTRUE,
                                portMAX_DELAY);
            continue;
        }

        setStatusConnected(true);

        // Placeholder path: later replace with real direct weather/BKK API calls.
        const bool depChanged = applyDummyDepartures(tickIndex);
        const bool weatherChanged = applyDummyWeather(tickIndex);

        if (depChanged || weatherChanged || !firstApplied)
        {
            displayNotifyDataChanged();
            firstApplied = true;
        }

        markLastUpdateNow();
        ++tickIndex;

        vTaskDelay(API_POLL_INTERVAL_TICKS);
    }
}
}

void dataSourceManagerInit(EventGroupHandle_t connectedEventGroup,
                           WiFiClient&        espClient,
                           SemaphoreHandle_t  clientMutex)
{
    s_wifiEventGroup = connectedEventGroup;
    s_clientMutex = clientMutex;
    s_espClient = &espClient;
    s_mode = g_config.dataSourceMode();

    if (s_mode == Configuration::DataSourceMode::Mqtt)
    {
        MqttRuntimeConfig mqttCfg = {
            g_config.mqttServer(),
            g_config.mqttPort(),
            g_config.mqttTopicDepartures(),
            g_config.mqttTopicWeather()
        };
        mqttManagerInit(s_wifiEventGroup, *s_espClient, s_clientMutex, mqttCfg);
        Serial.println("[DATA] Selected data source: MQTT");
    }
    else
    {
        Serial.println("[DATA] Selected data source: DIRECT_API (dummy mode)");
    }
}

void dataSourceManagerStart()
{
    if (s_mode == Configuration::DataSourceMode::Mqtt)
    {
        mqttTaskStart();
        return;
    }

    xTaskCreatePinnedToCore(
        directApiTask,
        "DirectApiTask",
        8192,
        nullptr,
        1,
        nullptr,
        0);
}

bool dataSourceManagerIsConnected()
{
    if (s_mode == Configuration::DataSourceMode::Mqtt)
    {
        return mqttManagerIsConnected();
    }

    bool connected = false;
    taskENTER_CRITICAL(&s_stateMux);
    connected = s_connected;
    taskEXIT_CRITICAL(&s_stateMux);
    return connected;
}

bool dataSourceManagerGetLastUpdateTime(struct tm* outTime)
{
    if (s_mode == Configuration::DataSourceMode::Mqtt)
    {
        return mqttManagerGetLastUpdateTime(outTime);
    }

    if (outTime == nullptr)
    {
        return false;
    }

    time_t ts = 0;
    bool hasUpdate = false;

    taskENTER_CRITICAL(&s_stateMux);
    hasUpdate = s_hasLastUpdate;
    ts = s_lastUpdateTs;
    taskEXIT_CRITICAL(&s_stateMux);

    if (!hasUpdate)
    {
        return false;
    }

    return localtime_r(&ts, outTime) != nullptr;
}

Configuration::DataSourceMode dataSourceManagerMode()
{
    return s_mode;
}
