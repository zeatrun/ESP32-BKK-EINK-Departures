#include "wifi_manager.h"

// Bit 0 in the event group signals WiFi connected
#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifiEventGroup = nullptr;

void wifiManagerInit(EventGroupHandle_t connectedEventGroup)
{
    s_wifiEventGroup = connectedEventGroup;
}

static void wifiTask(void* /*pvParameters*/)
{
    WiFi.mode(WIFI_STA);

    for (;;)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            // Clear the connected bit while we try to (re)connect
            xEventGroupClearBits(s_wifiEventGroup, WIFI_CONNECTED_BIT);

            Serial.print("[WiFi] Connecting to ");
            Serial.println(WIFI_SSID);

            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

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
                Serial.println("[WiFi] Failed. Retrying...");
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
