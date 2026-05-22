#include "mqtt_manager.h"
#include "departures.h"
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

// ── Callback ────────────────────────────────────────────────────────────────

static void mqttCallback(char* topic, byte* payload, unsigned int length)
{
    Serial.print("[MQTT] Message arrived [");
    Serial.print(topic);
    Serial.print("] (");
    Serial.print(length);
    Serial.println(" bytes)");

    // ── Parse JSON departure array ───────────────────────────────────────────
    // The payload is NOT null-terminated; ArduinoJson accepts (data, length).
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);

    if (err)
    {
        Serial.print("[MQTT] JSON parse error: ");
        Serial.println(err.c_str());
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull())
    {
        Serial.println("[MQTT] Payload is not a JSON array, skipping.");
        return;
    }

    taskENTER_CRITICAL(&s_stateMux);
    s_lastUpdateTs  = time(nullptr);
    s_hasLastUpdate = true;
    taskEXIT_CRITICAL(&s_stateMux);

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

    bool changed = false;

    // ── Fill departure globals under mutex ───────────────────────────────────
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

    if (changed)
    {
        displayNotifyDataChanged();
    }

    // Top-right status should reflect every valid MQTT refresh, not only data changes.
    displayNotifyStatusChanged();
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
