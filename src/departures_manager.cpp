#include "departures_manager.h"
#include "data_layer.h"
#include <freertos/portmacro.h>
#include <WiFi.h>

namespace
{
constexpr uint32_t DEPARTURES_SUCCESS_INTERVAL_MS = 120000; // 2 minutes after successful update
}

DeparturesManager g_departuresManager;

DeparturesManager::~DeparturesManager()
{
    stop();
}

void DeparturesManager::init(DeparturesProvider* provider, uint32_t intervalMs)
{
    m_provider = provider;
    m_intervalMs = intervalMs;
    Serial.printf("[DEPARTURES_MANAGER] Initialized with provider '%s', interval=%lu ms\n",
                  provider ? provider->providerName() : "null",
                  static_cast<unsigned long>(intervalMs));
}

void DeparturesManager::start()
{
    if (m_taskHandle != nullptr)
    {
        Serial.println("[DEPARTURES_MANAGER] Task already running");
        return;
    }

    if (m_provider == nullptr)
    {
        Serial.println("[DEPARTURES_MANAGER] No provider configured, cannot start");
        return;
    }

    m_running = true;
    const char taskName[] = "departures";
    // HTTPS + JSON parsing for BKK can require more stack on ESP32.
    const uint32_t stackSize = 12288;
    const UBaseType_t priority = tskIDLE_PRIORITY + 2; // Higher priority than weather

    if (xTaskCreate(taskEntry, taskName, stackSize, this, priority, &m_taskHandle) != pdPASS)
    {
        Serial.println("[DEPARTURES_MANAGER] Failed to create task");
        m_taskHandle = nullptr;
        m_running = false;
    }
    else
    {
        Serial.println("[DEPARTURES_MANAGER] Task started");
    }
}

void DeparturesManager::stop()
{
    if (m_taskHandle == nullptr)
    {
        return;
    }

    m_running = false;
    vTaskDelete(m_taskHandle);
    m_taskHandle = nullptr;
    Serial.println("[DEPARTURES_MANAGER] Task stopped");
}

void DeparturesManager::fetchNow()
{
    if (m_provider == nullptr)
    {
        Serial.println("[DEPARTURES_MANAGER] No provider configured");
        return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[DEPARTURES_MANAGER] Skipping fetch: WiFi not connected");
        m_connected = false;
        return;
    }

    Departure tempBuses[MAX_DEPARTURES];
    Departure tempTrains[MAX_DEPARTURES];
    int busCount = 0;
    int trainCount = 0;

    Serial.printf("[DEPARTURES_MANAGER] Fetching departures from %s...\n", m_provider->providerName());

    if (!m_provider->fetchDepartures(tempBuses, busCount, tempTrains, trainCount))
    {
        Serial.println("[DEPARTURES_MANAGER] Fetch failed");
        m_connected = false;
        return;
    }

    if (!g_dataLayer.applyDepartures(tempBuses, busCount, tempTrains, trainCount))
    {
        Serial.println("[DEPARTURES_MANAGER] DataLayer apply failed");
        m_connected = false;
        return;
    }

    m_connected = true;
    Serial.printf("[DEPARTURES_MANAGER] Departures updated: %d buses, %d trains\n", busCount, trainCount);
}

bool DeparturesManager::isConnected() const
{
    return m_connected;
}

void DeparturesManager::taskEntry(void* pvParameters)
{
    DeparturesManager* pThis = static_cast<DeparturesManager*>(pvParameters);
    pThis->taskLoop();
}

void DeparturesManager::taskLoop()
{
    uint32_t lastFetchMs = millis() - m_intervalMs;
    Serial.printf("[DEPARTURES_MANAGER] Task loop entered. interval=%lu ms, first fetch immediate\n",
                  static_cast<unsigned long>(m_intervalMs));

    while (m_running)
    {
        uint32_t nowMs = millis();
        uint32_t elapsedMs = nowMs - lastFetchMs;
        const uint32_t targetInterval = m_connected ? DEPARTURES_SUCCESS_INTERVAL_MS : m_intervalMs;

        if (elapsedMs >= targetInterval)
        {
            Serial.printf("[DEPARTURES_MANAGER] Trigger fetch. elapsed=%lu ms target=%lu ms connected=%s\n",
                          static_cast<unsigned long>(elapsedMs),
                          static_cast<unsigned long>(targetInterval),
                          m_connected ? "true" : "false");
            fetchNow();
            lastFetchMs = nowMs;
        }

        // Sleep a bit to avoid busy loop
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    Serial.println("[DEPARTURES_MANAGER] Task loop exiting");
}
