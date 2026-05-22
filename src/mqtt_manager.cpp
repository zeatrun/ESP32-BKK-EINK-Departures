#include "mqtt_manager.h"
#include "departures.h"
#include "weather.h"
#include "display_manager.h"
#include <ArduinoJson.h>

// Matches the bit defined in wifi_manager.cpp
#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifiEventGroup = nullptr;
static SemaphoreHandle_t  s_clientMutex    = nullptr;
static PubSubClient*      s_mqttClient     = nullptr;
static portMUX_TYPE       s_stateMux       = portMUX_INITIALIZER_UNLOCKED;
static bool               s_mqttConnected  = false;
static bool               s_hasLastUpdate  = false;
static time_t             s_lastUpdateTs   = 0;

static bool setMqttConnected(bool connected)
{
    bool changed = false;
    taskENTER_CRITICAL(&s_stateMux);
    if (s_mqttConnected != connected)
    {
        s_mqttConnected = connected;
        changed = true;
    }
    taskEXIT_CRITICAL(&s_stateMux);
    return changed;
}

static bool departureEquals(const Departure& a, const Departure& b)
{
    return strncmp(a.line, b.line, sizeof(a.line)) == 0
        && strncmp(a.routeIdText, b.routeIdText, sizeof(a.routeIdText)) == 0
        && strncmp(a.destination, b.destination, sizeof(a.destination)) == 0
        && strncmp(a.stopName, b.stopName, sizeof(a.stopName)) == 0
        && a.minutes == b.minutes
        && a.timestamp == b.timestamp;
}

static bool departuresEqual(const Departure* a, int aCount, const Departure* b, int bCount)
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

static bool weatherDayEquals(const WeatherDay& a, const WeatherDay& b)
{
    return strncmp(a.date, b.date, sizeof(a.date)) == 0
        && a.weatherCode == b.weatherCode
        && a.tempMaxC == b.tempMaxC
        && a.tempMinC == b.tempMinC
        && a.precipMm == b.precipMm
        && a.precipProbMax == b.precipProbMax;
}

static bool weatherEquals(const WeatherData& a, const WeatherData& b)
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

// ── Callback ────────────────────────────────────────────────────────────────

static void mqttCallback(char* topic, byte* payload, unsigned int length)
{
    Serial.print("[MQTT] Message arrived [");
    Serial.print(topic);
    Serial.print("] (");
    Serial.print(length);
    Serial.println(" bytes)");

    // The payload is NOT null-terminated; ArduinoJson accepts (data, length).
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);

    if (err)
    {
        Serial.print("[MQTT] JSON parse error: ");
        Serial.println(err.c_str());
        return;
    }

    bool payloadAccepted = false;
    bool changed = false;

    if (strcmp(topic, MQTT_SUB_TOPIC) == 0)
    {
        JsonArray arr = doc.as<JsonArray>();
        if (arr.isNull())
        {
            Serial.println("[MQTT] Departure payload is not a JSON array, skipping.");
            return;
        }

        Departure nextBusDepartures[MAX_DEPARTURES]   = {};
        Departure nextTrainDepartures[MAX_DEPARTURES] = {};
        int nextBusCount   = 0;
        int nextTrainCount = 0;

        for (JsonObject item : arr)
        {
            const char* line = item["line"] | "";
            if (line[0] == '\0') continue;

            // Buses have numeric line identifiers; trains use letter prefixes (Z, S, H, M…)
            bool isBus = isdigit(static_cast<unsigned char>(line[0]));

            Departure* dest = isBus ? nextBusDepartures   : nextTrainDepartures;
            int&       cnt  = isBus ? nextBusCount        : nextTrainCount;

            if (cnt >= MAX_DEPARTURES) continue;

            strlcpy(dest[cnt].line,        line,                      sizeof(dest[cnt].line));
            strlcpy(dest[cnt].routeIdText, item["routeIdText"] | "",  sizeof(dest[cnt].routeIdText));
            strlcpy(dest[cnt].destination, item["destination"] | "",  sizeof(dest[cnt].destination));
            strlcpy(dest[cnt].stopName,    item["stopName"]    | "",  sizeof(dest[cnt].stopName));
            dest[cnt].minutes   = item["minutes"]   | 0;
            dest[cnt].timestamp = item["timestamp"] | 0UL;

            ++cnt;
        }

        payloadAccepted = true;

        if (xSemaphoreTake(g_departuresMutex, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            const bool busesSame = departuresEqual(g_busDepartures, g_busCount,
                                                   nextBusDepartures, nextBusCount);
            const bool trainsSame = departuresEqual(g_trainDepartures, g_trainCount,
                                                    nextTrainDepartures, nextTrainCount);

            changed = !(busesSame && trainsSame);

            if (changed)
            {
                g_busCount   = nextBusCount;
                g_trainCount = nextTrainCount;

                for (int i = 0; i < g_busCount; ++i)   g_busDepartures[i]   = nextBusDepartures[i];
                for (int i = g_busCount; i < MAX_DEPARTURES; ++i) g_busDepartures[i] = {};

                for (int i = 0; i < g_trainCount; ++i) g_trainDepartures[i] = nextTrainDepartures[i];
                for (int i = g_trainCount; i < MAX_DEPARTURES; ++i) g_trainDepartures[i] = {};

                Serial.print("[MQTT] Departures updated: ");
                Serial.print(g_busCount);
                Serial.print(" bus(es), ");
                Serial.print(g_trainCount);
                Serial.println(" train(s).");
            }
            else
            {
                Serial.println("[MQTT] No departure change; skipping display refresh.");
            }

            xSemaphoreGive(g_departuresMutex);
        }
        else
        {
            Serial.println("[MQTT] Could not acquire departures mutex.");
        }
    }
    else if (strcmp(topic, MQTT_WEATHER_TOPIC) == 0)
    {
        JsonObject root = doc.as<JsonObject>();
        if (root.isNull())
        {
            Serial.println("[MQTT] Weather payload is not a JSON object, skipping.");
            return;
        }

        WeatherData nextWeather = {};

        strlcpy(nextWeather.source, root["source"] | "", sizeof(nextWeather.source));
        strlcpy(nextWeather.publishedAtUtc, root["publishedAtUtc"] | "", sizeof(nextWeather.publishedAtUtc));

        JsonObject location = root["location"].as<JsonObject>();
        strlcpy(nextWeather.locationName, location["name"] | "", sizeof(nextWeather.locationName));
        strlcpy(nextWeather.locationAdmin1, location["admin1"] | "", sizeof(nextWeather.locationAdmin1));
        strlcpy(nextWeather.locationCountry, location["country"] | "", sizeof(nextWeather.locationCountry));
        nextWeather.latitude = location["latitude"] | 0.0F;
        nextWeather.longitude = location["longitude"] | 0.0F;
        strlcpy(nextWeather.timezone, location["timezone"] | "", sizeof(nextWeather.timezone));

        JsonObject current = root["current"].as<JsonObject>();
        strlcpy(nextWeather.currentTime, current["time"] | "", sizeof(nextWeather.currentTime));
        nextWeather.temperatureC = current["temperatureC"] | 0.0F;
        nextWeather.apparentTemperatureC = current["apparentTemperatureC"] | 0.0F;
        nextWeather.relativeHumidity = current["relativeHumidity"] | 0;
        nextWeather.weatherCode = current["weatherCode"] | 0;
        nextWeather.windSpeedKmh = current["windSpeedKmh"] | 0.0F;
        nextWeather.windDirectionDeg = current["windDirectionDeg"] | 0;
        nextWeather.isDay = current["isDay"] | 0;

        JsonArray daily = root["daily"].as<JsonArray>();
        int dayIndex = 0;
        for (JsonObject day : daily)
        {
            if (dayIndex >= MAX_WEATHER_DAYS)
            {
                break;
            }

            strlcpy(nextWeather.daily[dayIndex].date,
                    day["date"] | "",
                    sizeof(nextWeather.daily[dayIndex].date));
            nextWeather.daily[dayIndex].weatherCode = day["weatherCode"] | 0;
            nextWeather.daily[dayIndex].tempMaxC = day["tempMaxC"] | 0.0F;
            nextWeather.daily[dayIndex].tempMinC = day["tempMinC"] | 0.0F;
            nextWeather.daily[dayIndex].precipMm = day["precipMm"] | 0.0F;
            nextWeather.daily[dayIndex].precipProbMax = day["precipProbMax"] | 0;
            ++dayIndex;
        }
        nextWeather.dailyCount = dayIndex;

        payloadAccepted = true;

        if (xSemaphoreTake(g_weatherMutex, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            changed = (!g_weatherValid) || (!weatherEquals(g_weatherData, nextWeather));

            if (changed)
            {
                g_weatherData = nextWeather;
                g_weatherValid = true;
                Serial.println("[MQTT] Weather updated.");
            }
            else
            {
                Serial.println("[MQTT] No weather change; skipping display refresh.");
            }

            xSemaphoreGive(g_weatherMutex);
        }
        else
        {
            Serial.println("[MQTT] Could not acquire weather mutex.");
        }
    }
    else
    {
        Serial.println("[MQTT] Unknown topic, skipping payload.");
        return;
    }

    if (payloadAccepted)
    {
        taskENTER_CRITICAL(&s_stateMux);
        s_lastUpdateTs  = time(nullptr);
        s_hasLastUpdate = true;
        taskEXIT_CRITICAL(&s_stateMux);

        // Top-right status should reflect every valid MQTT refresh.
        displayNotifyStatusChanged();
    }

    if (changed)
    {
        displayNotifyDataChanged();
    }
}

// ── Internal helpers ─────────────────────────────────────────────────────────

static bool mqttConnect()
{
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    Serial.print("[MQTT] Connecting as ");
    Serial.print(clientId);
    Serial.print("... ");

    if (s_mqttClient->connect(clientId.c_str()))
    {
        Serial.println("connected.");
        s_mqttClient->subscribe(MQTT_SUB_TOPIC);
        s_mqttClient->subscribe(MQTT_WEATHER_TOPIC);
        setMqttConnected(true);
        return true;
    }

    Serial.print("failed, rc=");
    Serial.println(s_mqttClient->state());
    setMqttConnected(false);
    return false;
}

// ── Task ─────────────────────────────────────────────────────────────────────

static void mqttTask(void* /*pvParameters*/)
{
    for (;;)
    {
        const EventBits_t bits   = xEventGroupGetBits(s_wifiEventGroup);
        const bool        wifiUp = (bits & WIFI_CONNECTED_BIT) != 0;

        if (!wifiUp)
        {
            if (setMqttConnected(false))
            {
                displayNotifyStatusChanged();
            }

            // Block until WiFi is available
            xEventGroupWaitBits(s_wifiEventGroup,
                                WIFI_CONNECTED_BIT,
                                pdFALSE,   // don't clear the bit
                                pdTRUE,
                                portMAX_DELAY);
            continue;
        }

        // Block until WiFi is available
        // ── Reconnect if needed ──────────────────────────────────────────────
        if (xSemaphoreTake(s_clientMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            if (!s_mqttClient->connected())
            {
                if (!mqttConnect())
                {
                    xSemaphoreGive(s_clientMutex);
                    if (setMqttConnected(false))
                    {
                        displayNotifyStatusChanged();
                    }
                    vTaskDelay(pdMS_TO_TICKS(MQTT_RECONNECT_DELAY_MS));
                    continue;
                }

                displayNotifyStatusChanged();
            }
            xSemaphoreGive(s_clientMutex);
        }

        // ── Pump the MQTT loop ───────────────────────────────────────────────
        if (xSemaphoreTake(s_clientMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            s_mqttClient->loop();

            if (!s_mqttClient->connected())
            {
                if (setMqttConnected(false))
                {
                    displayNotifyStatusChanged();
                }
            }

            xSemaphoreGive(s_clientMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool mqttManagerIsConnected()
{
    bool connected = false;
    taskENTER_CRITICAL(&s_stateMux);
    connected = s_mqttConnected;
    taskEXIT_CRITICAL(&s_stateMux);
    return connected;
}

bool mqttManagerGetLastUpdateTime(struct tm* outTime)
{
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

// ── Public API ────────────────────────────────────────────────────────────────

void mqttManagerInit(EventGroupHandle_t connectedEventGroup,
                     WiFiClient&        espClient,
                     SemaphoreHandle_t  clientMutex)
{
    s_wifiEventGroup = connectedEventGroup;
    s_clientMutex    = clientMutex;

    // PubSubClient is allocated once here; it will live for the lifetime of the program
    static PubSubClient mqttClient(espClient);
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
    mqttClient.setSocketTimeout(2);
    mqttClient.setCallback(mqttCallback);
    s_mqttClient = &mqttClient;
}

void mqttTaskStart()
{
    xTaskCreatePinnedToCore(
        mqttTask,
        "MQTTTask",
        8192,
        nullptr,
        1,          // priority
        nullptr,
        0           // core 0
    );
}
