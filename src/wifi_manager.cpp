#include "wifi_manager.h"
#include "configuration.h"

// Bit 0 in the event group signals WiFi connected
#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifiEventGroup = nullptr;
static volatile uint8_t s_lastDisconnectReason = 0;
static bool s_eventHandlerRegistered = false;

namespace
{
const char* wlStatusToString(wl_status_t status)
{
    switch (status)
    {
        case WL_NO_SHIELD: return "WL_NO_SHIELD";
        case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
        case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
        case WL_CONNECTED: return "WL_CONNECTED";
        case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
        case WL_DISCONNECTED: return "WL_DISCONNECTED";
        default: return "WL_UNKNOWN";
    }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
    {
        s_lastDisconnectReason = info.wifi_sta_disconnected.reason;
    }
}
}

void wifiManagerInit(EventGroupHandle_t connectedEventGroup)
{
    s_wifiEventGroup = connectedEventGroup;
}

static void wifiTask(void* /*pvParameters*/)
{
    if (!s_eventHandlerRegistered)
    {
        WiFi.onEvent(onWiFiEvent);
        s_eventHandlerRegistered = true;
    }

    WiFi.mode(WIFI_STA);

    for (;;)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            // Clear the connected bit while we try to (re)connect
            xEventGroupClearBits(s_wifiEventGroup, WIFI_CONNECTED_BIT);

            const char* wifiSsid = g_config.wifiSsid();
            const char* wifiPassword = g_config.wifiPassword();
            if (wifiSsid == nullptr || wifiSsid[0] == '\0')
            {
                wifiSsid = WIFI_SSID;
                wifiPassword = WIFI_PASSWORD;
                Serial.println("[WiFi] Runtime config SSID empty, falling back to compile-time settings");
            }

            Serial.print("[WiFi] Connecting to ");
            Serial.println(wifiSsid);

            WiFi.begin(wifiSsid, wifiPassword);

            // Wait up to 10 s in 500 ms ticks
            uint8_t attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 20)
            {
                vTaskDelay(pdMS_TO_TICKS(500));
                Serial.print(".");
                ++attempts;
            }
            Serial.println();

            if (WiFi.status() == WL_CONNECTED)
            {
                randomSeed(micros());
                Serial.print("[WiFi] Connected. IP: ");
                Serial.println(WiFi.localIP());
                // Signal other tasks that WiFi is up
                xEventGroupSetBits(s_wifiEventGroup, WIFI_CONNECTED_BIT);
            }
            else
            {
                const wl_status_t status = WiFi.status();
                const wifi_err_reason_t reason = static_cast<wifi_err_reason_t>(s_lastDisconnectReason);
                Serial.printf("[WiFi] Failed. status=%s(%d) reason=%s. Retrying...\n",
                              wlStatusToString(status),
                              static_cast<int>(status),
                              WiFi.disconnectReasonName(reason));
                WiFi.disconnect(true);
                vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_DELAY_MS));
            }
        }
        else
        {
            // WiFi is up – check again every 5 s
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}

void wifiTaskStart()
{
    // Pin to Core 0; networking stack is thread-safe on ESP32
    xTaskCreatePinnedToCore(
        wifiTask,
        "WiFiTask",
        4096,
        nullptr,
        1,          // priority
        nullptr,
        0           // core 0
    );
}

bool wifiIsConnected()
{
    return WiFi.status() == WL_CONNECTED;
}
