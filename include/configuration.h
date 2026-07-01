#pragma once

#include <Arduino.h>

class AsyncWebServer;
class AsyncWebServerRequest;
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
    enum class DataSourceMode : uint8_t
    {
        Mqtt = 0,
        DirectApi = 1,
    };

    enum class WeatherApiProvider : uint8_t
    {
        OpenMeteo = 0,
        OpenWeatherMap = 1,
    };

    enum class DeparturesApiProvider : uint8_t
    {
        Bkk = 0,
        MockData = 1,
    };

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
    
    // Separate data source modes for weather and departures
    DataSourceMode weatherDataSourceMode() const { return m_weatherDataSourceMode; }
    DataSourceMode departuresDataSourceMode() const { return m_departuresDataSourceMode; }
    bool useWeatherMqtt() const { return m_weatherDataSourceMode == DataSourceMode::Mqtt; }
    bool useDeparturesMqtt() const { return m_departuresDataSourceMode == DataSourceMode::Mqtt; }

    WeatherApiProvider weatherApiProvider() const { return m_weatherApiProvider; }
    DeparturesApiProvider departuresApiProvider() const { return m_departuresApiProvider; }
    
    const char* locationName() const { return m_locationName; }
    const char* bkkApiKey() const { return m_bkkApiKey; }
    const char* busStopId() const { return m_busStopId; }
    const char* trainStopId() const { return m_trainStopId; }

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
    void setWeatherDataSourceMode(DataSourceMode mode);
    void setDeparturesDataSourceMode(DataSourceMode mode);
    void setWeatherApiProvider(WeatherApiProvider provider);
    void setDeparturesApiProvider(DeparturesApiProvider provider);
    void setLocationName(const char* location);
    void setLocationCoordinates(float lat, float lon);
    void setBkkApiKey(const char* key);
    void setBusStopId(const char* stopId);
    void setTrainStopId(const char* stopId);

    float locationLat() const { return m_locationLat; }
    float locationLon() const { return m_locationLon; }

    /**
     * Helper: resolve city name to latitude/longitude.
     * First checks stored coordinates (set when a city is selected from the
     * geocoding autocomplete on the config page). Falls back to a static
     * lookup table for the handful of pre-defined Hungarian cities.
     * Returns true if coordinates could be determined, false otherwise.
     */
    bool resolveLocationCoordinates(const char* city, float& lat, float& lon);

private:
    char     m_wifiSsid[64]               = {};
    char     m_wifiPassword[64]           = {};
    char     m_mqttServer[64]             = {};
    uint16_t m_mqttPort                   = 1883;
    char     m_mqttTopicDepartures[128]   = {};
    char     m_mqttTopicWeather[128]      = {};
    DataSourceMode m_weatherDataSourceMode = DataSourceMode::Mqtt;
    DataSourceMode m_departuresDataSourceMode = DataSourceMode::Mqtt;
    WeatherApiProvider m_weatherApiProvider = WeatherApiProvider::OpenMeteo;
    DeparturesApiProvider m_departuresApiProvider = DeparturesApiProvider::Bkk;
    char     m_locationName[48]           = {}; // e.g., "Budapest"
    float    m_locationLat                = 0.0f; // 0 = not set (use static table)
    float    m_locationLon                = 0.0f;
    char     m_bkkApiKey[128]             = {};
    char     m_busStopId[64]              = {};
    char     m_trainStopId[64]            = {};
    // POSIX TZ string — default: Central European Time with automatic DST.
    char     m_timezone[64]               = "CET-1CEST,M3.5.0,M10.5.0/3";

    bool      m_configModeActive          = false;
    char      m_apSsid[32]                = {};
    char      m_apPassword[32]            = {};
    AsyncWebServer* m_webServer           = nullptr;
    DNSServer* m_dnsServer                = nullptr;
    bool      m_webRoutesRegistered       = false;
    bool      m_rebootPending             = false;
    uint32_t  m_rebootAtMs                = 0;

    void generateConfigApCredentials();
    void loadDefaults();
    void scheduleReboot(uint32_t delayMs);
    void renderConfigScreen();
    void setupWebServerRoutes();
    void handleRootGet(AsyncWebServerRequest* request);
    void handleCaptiveProbeGet(AsyncWebServerRequest* request);
    void handleSavePost(AsyncWebServerRequest* request);
    void handleRebootPost(AsyncWebServerRequest* request);
    void handleRebootNowGet(AsyncWebServerRequest* request);
    void handleApiSettingsGet(AsyncWebServerRequest* request);
    void handleApiGeocodeGet(AsyncWebServerRequest* request);
    void handleApiWifiScanGet(AsyncWebServerRequest* request);
    void handleApiWifiTestPost(AsyncWebServerRequest* request);
    void handleApiConfigSavePost(AsyncWebServerRequest* request);
    void handleApiConfigResetPost(AsyncWebServerRequest* request);
    void handleApiWeatherTestGet(AsyncWebServerRequest* request);
    void handleApiDeparturesTestPost(AsyncWebServerRequest* request);
    void handleNotFound(AsyncWebServerRequest* request);
};

/** Global configuration instance, defined in configuration.cpp. */
extern Configuration g_config;
