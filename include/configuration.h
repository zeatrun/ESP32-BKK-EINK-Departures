#pragma once

#include <Arduino.h>

/**
 * @brief Runtime configuration holder.
 *
 * Placeholder class — later this will:
 *  - Load/save config from NVS or LittleFS (JSON).
 *  - Expose a web server on AP mode so the user can set WiFi/MQTT credentials
 *    through a simple HTML form.
 *
 * For now it only wraps the compile-time defaults from settings.h so the rest
 * of the codebase can be transitioned to use getters instead of raw #defines.
 */
class Configuration
{
public:
    Configuration() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /**
     * Load configuration. Currently returns the compile-time defaults.
     * Future: read from NVS / LittleFS.
     */
    void load();

    /**
     * Save configuration to persistent storage.
     * Not yet implemented.
     */
    void save();

    // ── Accessors ─────────────────────────────────────────────────────────────

    const char* wifiSsid()     const { return m_wifiSsid; }
    const char* wifiPassword() const { return m_wifiPassword; }

    const char* mqttServer()  const { return m_mqttServer; }
    uint16_t    mqttPort()    const { return m_mqttPort; }

    const char* mqttTopicDepartures() const { return m_mqttTopicDepartures; }
    const char* mqttTopicWeather()    const { return m_mqttTopicWeather; }

    /**
     * POSIX timezone string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3" for Budapest.
     * Passed directly to configTzTime() / setenv("TZ", ...).
     */
    const char* timezone() const { return m_timezone; }

    // ── Mutators (used by the config-mode web server) ─────────────────────────

    void setWifiSsid(const char* ssid);
    void setWifiPassword(const char* password);
    void setMqttServer(const char* server);
    void setMqttPort(uint16_t port);
    void setMqttTopicDepartures(const char* topic);
    void setMqttTopicWeather(const char* topic);
    void setTimezone(const char* tz);

private:
    char     m_wifiSsid[64]               = {};
    char     m_wifiPassword[64]           = {};
    char     m_mqttServer[64]             = {};
    uint16_t m_mqttPort                   = 1883;
    char     m_mqttTopicDepartures[128]   = {};
    char     m_mqttTopicWeather[128]      = {};
    // POSIX TZ string — default: Central European Time with automatic DST.
    char     m_timezone[64]               = "CET-1CEST,M3.5.0,M10.5.0/3";
};

/** Global configuration instance, defined in configuration.cpp. */
extern Configuration g_config;
