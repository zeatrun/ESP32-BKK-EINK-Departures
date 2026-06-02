#pragma once

#include "weather.h"
#include "weather_provider.h"
#include <freertos/task.h>

/**
 * @brief Weather data manager - handles periodic fetching from a weather provider.
 *
 * Runs a FreeRTOS task that:
 *  - Periodically fetches weather data using the configured provider
 *  - Updates the global g_weatherData with thread-safe access
 *  - Handles connection state and error handling
 */
class WeatherManager
{
public:
    WeatherManager() = default;
    ~WeatherManager();

    /**
     * Initialize the weather manager with a provider and fetch interval.
     *
     * @param provider      WeatherProvider instance (must remain valid while manager is active)
     * @param intervalMs    Fetch interval in milliseconds (default 5 minutes)
     */
    void init(WeatherProvider* provider, uint32_t intervalMs = 300000);

    /**
     * Start the weather fetch task. Safe to call multiple times.
     */
    void start();

    /**
     * Stop the weather fetch task.
     */
    void stop();

    /**
     * Manually trigger a weather fetch (called periodically by task).
     */
    void fetchNow();

    /**
     * Get current connection/health state.
     */
    bool isConnected() const;

private:
    WeatherProvider* m_provider = nullptr;
    uint32_t m_intervalMs = 300000;
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
 * Global weather manager instance.
 */
extern WeatherManager g_weatherManager;
