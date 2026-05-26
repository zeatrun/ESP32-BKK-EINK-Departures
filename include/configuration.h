#pragma once

#include <Arduino.h>

class WebServer;
class DNSServer;

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

    // ── Config-mode runtime ──────────────────────────────────────────────────

    /**
     * Starts AP + HTTP configuration portal and renders the current settings
     * on the display. Safe to call multiple times.
     */
    bool beginConfigMode();

    /**
     * Processes HTTP clients while running in config mode. Call from loop().
     */
    void handleConfigMode();

    bool isConfigModeActive() const { return m_configModeActive; }

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
    const char* configApSsid() const { return m_apSsid; }
    const char* configApPassword() const { return m_apPassword; }

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

    bool      m_configModeActive          = false;
    char      m_apSsid[32]                = {};
    char      m_apPassword[32]            = {};
    WebServer* m_webServer                = nullptr;
    DNSServer* m_dnsServer                = nullptr;
    bool      m_webRoutesRegistered       = false;
    bool      m_rebootPending             = false;
    uint32_t  m_rebootAtMs                = 0;

    void generateConfigApCredentials();
    void loadDefaults();
    void scheduleReboot(uint32_t delayMs);
    void renderConfigScreen();
    void setupWebServerRoutes();
};

/** Global configuration instance, defined in configuration.cpp. */
extern Configuration g_config;
