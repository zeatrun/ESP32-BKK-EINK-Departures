#include <Arduino.h>
#include <WiFi.h>
#include "startup_manager.h"
#include "configuration.h"
#include "wifi_manager.h"
#include "data_source_manager.h"
#include "departures.h"
#include "weather.h"
#include "display_manager.h"
#include "time_manager.h"
#include "battery_monitor.h"

// ── Shared networking resources ──────────────────────────────────────────────
// espClient is shared between the WiFi and MQTT subsystems.
// s_clientMutex guards every call into PubSubClient.
static WiFiClient        espClient;
static SemaphoreHandle_t s_clientMutex    = nullptr;
static EventGroupHandle_t s_wifiEventGroup = nullptr;

// ── setup ────────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    delay(2000);
    Serial.println("7.3\" E-Paper Departures and Weather Display");

    // ── Boot mode detection ───────────────────────────────────────────────────
    StartupManager::detect();
    g_config.load();

    if (StartupManager::isConfigMode())
    {
        if (!g_config.beginConfigMode())
        {
            Serial.println("[MAIN] Config mode failed to start.");
        }
        Serial.println("[MAIN] Config mode active.");
        return;
    }

    // ── E-Paper initialisation  ──────────────────────────────────────────────
    displayBegin();
    displayTaskStart();

    // ── Synchronisation primitives ───────────────────────────────────────────
    departuresInit();          // creates g_departuresMutex
    weatherInit();             // creates g_weatherMutex
    s_clientMutex    = xSemaphoreCreateMutex();
    s_wifiEventGroup = xEventGroupCreate();

    configASSERT(s_clientMutex);
    configASSERT(s_wifiEventGroup);

    // ── WiFi manager (must be inited before MQTT so the event group is ready) ─
    wifiManagerInit(s_wifiEventGroup);
    wifiTaskStart();

    // ── Data source manager (MQTT or direct API) ────────────────────────────
    dataSourceManagerInit(s_wifiEventGroup, espClient, s_clientMutex);
    dataSourceManagerStart();

    // ── Time manager ──────────────────────────────────────────────────────────
    timeManagerInit(s_wifiEventGroup);
    timeManagerStart();

    // ── Battery monitor (starts monitoring after init) ───────────────────────
    batteryMonitorInit();
}

// ── loop ─────────────────────────────────────────────────────────────────────
// Both networking tasks run on Core 0; keep loop() (Core 1) free for display
// or other sensor work.
void loop()
{
    if (StartupManager::isConfigMode())
    {
        g_config.handleConfigMode();
        delay(5);
        return;
    }

    // Placeholder: later we can call this only on data-changed events.
    // displayRenderFromGlobals();
    vTaskDelay(pdMS_TO_TICKS(1000));
}