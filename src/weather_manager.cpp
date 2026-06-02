#include "weather_manager.h"
#include "data_layer.h"
#include <freertos/portmacro.h>
#include <WiFi.h>

namespace
{
constexpr uint32_t WEATHER_SUCCESS_INTERVAL_MS = 120000; // 2 minutes after successful update
constexpr uint32_t WEATHER_RETRY_INTERVAL_MS = 15000; // Fast retry after failure
}

WeatherManager g_weatherManager;

WeatherManager::~WeatherManager()
{
    stop();
}

void WeatherManager::init(WeatherProvider* provider, uint32_t intervalMs)
{
    m_provider = provider;
    m_intervalMs = intervalMs;
    Serial.printf("[WEATHER_MANAGER] Initialized with provider '%s', interval=%lu ms\n",
                  provider ? provider->providerName() : "null",
                  static_cast<unsigned long>(intervalMs));
}

void WeatherManager::start()
{
    if (m_taskHandle != nullptr)
    {
        Serial.println("[WEATHER_MANAGER] Task already running");
        return;
    }

    if (m_provider == nullptr)
    {
        Serial.println("[WEATHER_MANAGER] No provider configured, cannot start");
        return;
    }

    m_running = true;
    const char taskName[] = "weather";
    // HTTPS + JSON parsing can require significantly more stack on ESP32.
    const uint32_t stackSize = 12288;
    const UBaseType_t priority = tskIDLE_PRIORITY + 1;

    if (xTaskCreate(taskEntry, taskName, stackSize, this, priority, &m_taskHandle) != pdPASS)
    {
        Serial.println("[WEATHER_MANAGER] Failed to create task");
        m_taskHandle = nullptr;
        m_running = false;
    }
    else
    {
        Serial.println("[WEATHER_MANAGER] Task started");
    }
}

void WeatherManager::stop()
{
    if (m_taskHandle == nullptr)
    {
        return;
    }

    m_running = false;
    vTaskDelete(m_taskHandle);
    m_taskHandle = nullptr;
    Serial.println("[WEATHER_MANAGER] Task stopped");
}

void WeatherManager::fetchNow()
{
    if (m_provider == nullptr)
    {
        Serial.println("[WEATHER_MANAGER] No provider configured");
        return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[WEATHER_MANAGER] Skipping fetch: WiFi not connected");
        m_connected = false;
        return;
    }

    WeatherData tempData = {};

    Serial.printf("[WEATHER_MANAGER] Fetching weather from %s...\n", m_provider->providerName());

    if (!m_provider->fetchWeather(tempData))
    {
        Serial.println("[WEATHER_MANAGER] Fetch failed");
        m_connected = false;
        return;
    }

    if (!g_dataLayer.applyWeather(tempData))
    {
        Serial.println("[WEATHER_MANAGER] DataLayer apply failed");
        m_connected = false;
        return;
    }

    m_connected = true;
    Serial.printf("[WEATHER_MANAGER] Weather updated successfully. Temp=%.1f°C, Code=%d\n",
                  tempData.temperatureC,
                  tempData.weatherCode);
}

bool WeatherManager::isConnected() const
{
    return m_connected;
}

void WeatherManager::taskEntry(void* pvParameters)
{
    WeatherManager* pThis = static_cast<WeatherManager*>(pvParameters);
    pThis->taskLoop();
}

void WeatherManager::taskLoop()
{
    uint32_t lastFetchMs = millis() - m_intervalMs;
    Serial.printf("[WEATHER_MANAGER] Task loop entered. interval=%lu ms, first fetch immediate\n",
                  static_cast<unsigned long>(m_intervalMs));

    while (m_running)
    {
        uint32_t nowMs = millis();
        uint32_t elapsedMs = nowMs - lastFetchMs;
        const uint32_t targetInterval = m_connected ? WEATHER_SUCCESS_INTERVAL_MS : WEATHER_RETRY_INTERVAL_MS;

        if (elapsedMs >= targetInterval)
        {
            Serial.printf("[WEATHER_MANAGER] Trigger fetch. elapsed=%lu ms target=%lu ms connected=%s\n",
                          static_cast<unsigned long>(elapsedMs),
                          static_cast<unsigned long>(targetInterval),
                          m_connected ? "true" : "false");
            fetchNow();
            lastFetchMs = nowMs;
        }

        // Sleep a bit to avoid busy loop
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    Serial.println("[WEATHER_MANAGER] Task loop exiting");
}
