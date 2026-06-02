#pragma once

#include "departures.h"
#include "departures_provider.h"
#include <freertos/task.h>

/**
 * @brief Departures data manager - handles periodic fetching from a departures provider.
 *
 * Runs a FreeRTOS task that:
 *  - Periodically fetches departures data using the configured provider
 *  - Updates the global g_departuresData with thread-safe access
 *  - Handles connection state and error handling
 */
class DeparturesManager
{
public:
    DeparturesManager() = default;
    ~DeparturesManager();

    /**
     * Initialize the departures manager with a provider and fetch interval.
     *
     * @param provider      DeparturesProvider instance (must remain valid while manager is active)
     * @param intervalMs    Fetch interval in milliseconds (default 10 seconds)
     */
    void init(DeparturesProvider* provider, uint32_t intervalMs = 10000);

    /**
     * Start the departures fetch task. Safe to call multiple times.
     */
    void start();

    /**
     * Stop the departures fetch task.
     */
    void stop();

    /**
     * Manually trigger a departures fetch (called periodically by task).
     */
    void fetchNow();

    /**
     * Get current connection/health state.
     */
    bool isConnected() const;

private:
    DeparturesProvider* m_provider = nullptr;
    uint32_t m_intervalMs = 10000;
    TaskHandle_t m_taskHandle = nullptr;
    bool m_running = false;
    bool m_connected = false;

    /**
     * FreeRTOS task entry point (static wrapper).
     */
    static void taskEntry(void* pvParameters);

    /**
     * Main task loop.
     */
    void taskLoop();
};

/**
 * Global departures manager instance.
 */
extern DeparturesManager g_departuresManager;
