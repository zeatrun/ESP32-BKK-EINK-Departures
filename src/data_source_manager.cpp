#include "data_source_manager.h"

#include "departures.h"
#include "display_manager.h"
#include "mqtt_manager.h"
#include "weather.h"
#include "weather_manager.h"
#include "departures_manager.h"
#include "weather_provider.h"
#include "openmeteo_weather_provider.h"
#include "departures_provider.h"
#include "bkk_departures_provider.h"
#include "configuration.h"

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

bool isBkkStopId(const char* stopId)
{
    return stopId != nullptr && strncmp(stopId, "BKK_", 4) == 0;
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
    Serial.println("[DATA][MONITOR] DirectApi monitor task started");
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
            Serial.println("[DATA][MONITOR] WiFi down, waiting for reconnect...");
            setStatusConnected(false);
            xEventGroupWaitBits(s_wifiEventGroup,
                                WIFI_CONNECTED_BIT,
                                pdFALSE,
                                pdTRUE,
                                portMAX_DELAY);
            Serial.println("[DATA][MONITOR] WiFi reconnected");
            continue;
        }

        bool weatherReady = true;
        bool departuresReady = true;

        if (!g_config.useWeatherMqtt())
        {
            weatherReady = g_weatherManager.isConnected();
        }
        if (!g_config.useDeparturesMqtt())
        {
            departuresReady = g_departuresManager.isConnected();
        }

        const bool connected = weatherReady && departuresReady;
        Serial.printf("[DATA][MONITOR] states: weatherReady=%s departuresReady=%s => connected=%s\n",
                  weatherReady ? "true" : "false",
                  departuresReady ? "true" : "false",
                  connected ? "true" : "false");
        setStatusConnected(connected);

        // Notify display task regularly so it can render first arrival and periodic refreshes.
        displayNotifyDataChanged();

        markLastUpdateNow();
        vTaskDelay(pdMS_TO_TICKS(5000)); // Monitor interval
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
    // s_mode is derived from weatherDataSourceMode for backward compatibility
    s_mode = g_config.useWeatherMqtt() ? Configuration::DataSourceMode::Mqtt : Configuration::DataSourceMode::DirectApi;

    Serial.printf("[DATA][INIT] weatherSource=%s departuresSource=%s weatherApi=%u departuresApi=%u\n",
                  g_config.useWeatherMqtt() ? "MQTT" : "DirectAPI",
                  g_config.useDeparturesMqtt() ? "MQTT" : "DirectAPI",
                  static_cast<unsigned int>(g_config.weatherApiProvider()),
                  static_cast<unsigned int>(g_config.departuresApiProvider()));
    Serial.printf("[DATA][INIT] location='%s' timezone='%s' busStop='%s' trainStop='%s'\n",
                  g_config.locationName(),
                  g_config.timezone(),
                  g_config.busStopId(),
                  g_config.trainStopId());

    if (g_config.useDeparturesMqtt())
    {
        Serial.println("[DATA][INIT] Departures source is MQTT, BKK DirectAPI provider is disabled");
    }

    // If weather and/or departures use MQTT, init MQTT manager
    if (g_config.useWeatherMqtt() || g_config.useDeparturesMqtt())
    {
        MqttRuntimeConfig mqttCfg = {
            g_config.mqttServer(),
            g_config.mqttPort(),
            g_config.mqttTopicDepartures(),
            g_config.mqttTopicWeather()
        };
        mqttManagerInit(s_wifiEventGroup, *s_espClient, s_clientMutex, mqttCfg);
        if (g_config.useWeatherMqtt() && g_config.useDeparturesMqtt())
        {
            Serial.println("[DATA] Selected data source: MQTT for both");
        }
        else
        {
            Serial.printf("[DATA] Mixed data sources: weather=%s departures=%s\n",
                          g_config.useWeatherMqtt() ? "MQTT" : "DirectAPI",
                          g_config.useDeparturesMqtt() ? "MQTT" : "DirectAPI");
        }
    }
    else
    {
        Serial.println("[DATA] Selected data source: DIRECT_API for both");
    }

    // Initialize DirectAPI providers (weather and/or departures)
    if (!g_config.useWeatherMqtt())
    {
        // Initialize weather provider based on configuration
        WeatherProvider* weatherProvider = nullptr;
        if (g_config.weatherApiProvider() == Configuration::WeatherApiProvider::OpenMeteo)
        {
            // Resolve location coordinates from configuration
            float latitude = 47.62f;   // Default: Budapest
            float longitude = 18.90f;
            if (!g_config.resolveLocationCoordinates(g_config.locationName(), latitude, longitude))
            {
                Serial.printf("[DATA] Warning: Unknown location '%s', using Budapest defaults\n", g_config.locationName());
            }
            weatherProvider = new OpenMeteoWeatherProvider(latitude, longitude, g_config.timezone());
            Serial.printf("[DATA] Weather provider: Open-Meteo (location: %s, lat=%.2f, lon=%.2f)\n", 
                          g_config.locationName(), latitude, longitude);
        }

        if (weatherProvider)
        {
            g_weatherManager.init(weatherProvider, 300000); // 5 minute interval
        }
        else
        {
            Serial.println("[DATA][INIT] No weather provider created for DirectAPI");
        }
    }

    if (!g_config.useDeparturesMqtt())
    {
        // Initialize departures provider based on configuration
        DeparturesProvider* departuresProvider = nullptr;
        if (g_config.departuresApiProvider() == Configuration::DeparturesApiProvider::Bkk)
        {
            // Select a BKK-compatible stop ID with preference order:
            // 1) bus stop if it starts with BKK_
            // 2) train stop if it starts with BKK_
            // 3) non-empty bus stop
            // 4) non-empty train stop
            const char* busStop = g_config.busStopId();
            const char* trainStop = g_config.trainStopId();
            const char* stopId = "";

            if (isBkkStopId(busStop))
            {
                stopId = busStop;
            }
            else if (isBkkStopId(trainStop))
            {
                stopId = trainStop;
            }
            else if (busStop[0] != '\0')
            {
                stopId = busStop;
            }
            else
            {
                stopId = trainStop;
            }

            if (!isBkkStopId(stopId))
            {
                Serial.printf("[DATA][INIT] Warning: selected stop '%s' does not look like a BKK stop ID (expected prefix: BKK_)\n",
                              stopId);
            }

            Serial.printf("[DATA][INIT] BKK stop selected='%s' (bus='%s' train='%s')\n",
                          stopId,
                          g_config.busStopId(),
                          g_config.trainStopId());
            departuresProvider = new BkkDeparturesProvider(g_config.bkkApiKey(), stopId);
            Serial.println("[DATA] Departures provider: BKK");
        }

        if (departuresProvider)
        {
            g_departuresManager.init(departuresProvider, 10000); // 10 second interval
        }
        else
        {
            Serial.println("[DATA][INIT] No departures provider created for DirectAPI");
        }
    }
}

void dataSourceManagerStart()
{
    Serial.printf("[DATA][START] weatherSource=%s departuresSource=%s\n",
                  g_config.useWeatherMqtt() ? "MQTT" : "DirectAPI",
                  g_config.useDeparturesMqtt() ? "MQTT" : "DirectAPI");

    // If both use MQTT, start MQTT task only
    if (g_config.useWeatherMqtt() && g_config.useDeparturesMqtt())
    {
        Serial.println("[DATA][START] Starting MQTT task only");
        mqttTaskStart();
        return;
    }

    // Start MQTT if either weather or departures use it
    if (g_config.useWeatherMqtt() || g_config.useDeparturesMqtt())
    {
        Serial.println("[DATA][START] Starting MQTT task for mixed mode");
        mqttTaskStart();
    }

    // Start managers for DirectAPI providers
    if (!g_config.useWeatherMqtt())
    {
        Serial.println("[DATA][START] Starting WeatherManager task");
        g_weatherManager.start();
    }

    if (!g_config.useDeparturesMqtt())
    {
        Serial.println("[DATA][START] Starting DeparturesManager task");
        g_departuresManager.start();
    }

    // Start monitoring task if either uses DirectAPI
    if (!g_config.useWeatherMqtt() || !g_config.useDeparturesMqtt())
    {
        Serial.println("[DATA][START] Starting DirectApi monitor task");
        xTaskCreatePinnedToCore(
            directApiTask,
            "DirectApiMonitor",
            4096,
            nullptr,
            1,
            nullptr,
            0);
    }
}

bool dataSourceManagerIsConnected()
{
    bool directConnected = false;
    taskENTER_CRITICAL(&s_stateMux);
    directConnected = s_connected;
    taskEXIT_CRITICAL(&s_stateMux);

    const bool mqttEnabled = g_config.useWeatherMqtt() || g_config.useDeparturesMqtt();
    const bool directEnabled = !g_config.useWeatherMqtt() || !g_config.useDeparturesMqtt();

    if (mqttEnabled && directEnabled)
    {
        return mqttManagerIsConnected() && directConnected;
    }
    if (mqttEnabled)
    {
        return mqttManagerIsConnected();
    }
    return directConnected;
}

bool dataSourceManagerGetLastUpdateTime(struct tm* outTime)
{
    const bool mqttOnly = g_config.useWeatherMqtt() && g_config.useDeparturesMqtt();
    if (mqttOnly)
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
