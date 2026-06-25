#include "configuration.h"
#include "display_manager.h"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <functional>

#include <ElegantOTA.h>

// Pull compile-time defaults from settings.h (or the example fallback).
#if __has_include("settings.h")
#include "settings.h"
#else
#include "settings_example.h"
#endif

// ── Global instance ───────────────────────────────────────────────────────────
Configuration g_config;

namespace
{
constexpr uint16_t CAPTIVE_DNS_PORT = 53;
constexpr char CONFIG_PREF_NS[] = "cfg";
constexpr char CONFIG_PAGE_FS_PATH[] = "/config_page.html";
constexpr bool DUMMY_DEFAULT_USE_MQTT = true;

Configuration::DataSourceMode dataSourceModeFromStoredBool(bool useMqtt)
{
    return useMqtt ? Configuration::DataSourceMode::Mqtt
                   : Configuration::DataSourceMode::DirectApi;
}

const char* dataSourceModeToString(Configuration::DataSourceMode mode)
{
    return (mode == Configuration::DataSourceMode::Mqtt) ? "mqtt" : "direct_api";
}

String trimCopy(const String& in)
{
    String out = in;
    out.trim();
    return out;
}

bool isAsciiPrintableNoCtrl(const String& value)
{
    for (size_t i = 0; i < value.length(); ++i)
    {
        const char c = value[i];
        if (static_cast<unsigned char>(c) < 32U || static_cast<unsigned char>(c) > 126U)
        {
            return false;
        }
    }
    return true;
}

bool isValidHostOrIpv4(const String& host)
{
    if (host.isEmpty() || host.length() > 63)
    {
        return false;
    }

    IPAddress ip;
    if (ip.fromString(host))
    {
        return true;
    }

    if (host[0] == '.' || host[0] == '-' || host[host.length() - 1] == '.' || host[host.length() - 1] == '-')
    {
        return false;
    }

    bool hasAlphaNum = false;
    char prev = '\0';
    for (size_t i = 0; i < host.length(); ++i)
    {
        const char c = host[i];
        const bool isAlphaNum = ((c >= 'a' && c <= 'z') ||
                                 (c >= 'A' && c <= 'Z') ||
                                 (c >= '0' && c <= '9'));
        if (!(isAlphaNum || c == '.' || c == '-'))
        {
            return false;
        }
        if ((c == '.' || c == '-') && prev == c)
        {
            return false;
        }
        if (isAlphaNum)
        {
            hasAlphaNum = true;
        }
        prev = c;
    }

    return hasAlphaNum;
}

bool isValidMqttTopic(const String& topic)
{
    if (topic.isEmpty() || topic.length() > 127)
    {
        return false;
    }

    for (size_t i = 0; i < topic.length(); ++i)
    {
        const char c = topic[i];
        if (c == '\r' || c == '\n' || c == '\t' || c == '\0')
        {
            return false;
        }
    }

    return true;
}

struct TimezoneOption
{
    const char* value;
    const char* labelEn;
    const char* labelHu;
};

struct KnownLocation
{
    const char* name;
    const char* admin1;
    const char* country;
    float latitude;
    float longitude;
};

constexpr TimezoneOption TIMEZONE_OPTIONS[] = {
    {"UTC0", "UTC", "UTC"},
    {"CET-1CEST,M3.5.0,M10.5.0/3", "Budapest (CET/CEST)", "Budapest (CET/CEST)"},
    {"GMT0BST,M3.5.0/1,M10.5.0", "London (GMT/BST)", "London (GMT/BST)"},
    {"CET-1CEST,M3.5.0,M10.5.0/3", "Berlin (CET/CEST)", "Berlin (CET/CEST)"},
    {"EST5EDT,M3.2.0,M11.1.0", "New York (EST/EDT)", "New York (EST/EDT)"},
    {"PST8PDT,M3.2.0,M11.1.0", "Los Angeles (PST/PDT)", "Los Angeles (PST/PDT)"},
    {"JST-9", "Tokyo (JST)", "Tokio (JST)"},
    {"CST-8", "Shanghai (CST)", "Sanghaj (CST)"}
};

constexpr KnownLocation KNOWN_LOCATIONS[] = {
    {"Budapest", "Pest", "Hungary", 47.4979f, 19.0402f},
    {"Pilisvorosvar", "Pest", "Hungary", 47.6108f, 18.9133f},
    {"Pilisszentivan", "Pest", "Hungary", 47.6130f, 18.9080f},
    {"Esztergom", "Komarom-Esztergom", "Hungary", 47.7857f, 18.7521f},
    {"Gyor", "Gyor-Moson-Sopron", "Hungary", 47.6875f, 17.6504f},
    {"Szeged", "Csongrad-Csanad", "Hungary", 46.2530f, 20.1414f},
    {"Debrecen", "Hajdu-Bihar", "Hungary", 47.5316f, 21.6273f},
    {"Pecs", "Baranya", "Hungary", 46.0727f, 18.2323f},
    {"Miskolc", "Borsod-Abauj-Zemplen", "Hungary", 48.1035f, 20.7784f},
    {"Szekesfehervar", "Fejer", "Hungary", 47.1860f, 18.4221f},
};

bool isAllowedTimezoneValue(const String& tz)
{
    for (const auto& option : TIMEZONE_OPTIONS)
    {
        if (tz == option.value)
        {
            return true;
        }
    }
    return false;
}

bool findKnownLocationCoordinates(const char* city, float& lat, float& lon)
{
    if (!city || strlen(city) == 0)
    {
        return false;
    }

    for (const auto& knownLocation : KNOWN_LOCATIONS)
    {
        if (strcasecmp(city, knownLocation.name) == 0)
        {
            lat = knownLocation.latitude;
            lon = knownLocation.longitude;
            return true;
        }
    }

    return false;
}

String urlEncode(const String& input)
{
    String out;
    out.reserve(input.length() * 3);

    const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < input.length(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        const bool isAlphaNum = (c >= 'a' && c <= 'z') ||
                                (c >= 'A' && c <= 'Z') ||
                                (c >= '0' && c <= '9');
        if (isAlphaNum || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out += static_cast<char>(c);
        }
        else if (c == ' ')
        {
            out += "%20";
        }
        else
        {
            out += '%';
            out += hex[(c >> 4) & 0x0F];
            out += hex[c & 0x0F];
        }
    }

    return out;
}

String buildKnownLocationsGeocodeJson(const String& query)
{
    String needle = query;
    needle.toLowerCase();

    String json;
    json.reserve(700);
    json += "{\"results\":[";

    bool first = true;
    uint8_t added = 0;
    for (const auto& location : KNOWN_LOCATIONS)
    {
        String haystack = String(location.name) + " " + location.admin1 + " " + location.country;
        haystack.toLowerCase();
        if (!needle.isEmpty() && haystack.indexOf(needle) < 0)
        {
            continue;
        }

        if (!first)
        {
            json += ',';
        }
        first = false;

        json += "{\"name\":\"";
        json += location.name;
        json += "\",\"admin1\":\"";
        json += location.admin1;
        json += "\",\"country\":\"";
        json += location.country;
        json += "\",\"latitude\":";
        json += String(location.latitude, 6);
        json += ",\"longitude\":";
        json += String(location.longitude, 6);
        json += "}";

        ++added;
        if (added >= 10)
        {
            break;
        }
    }

    json += "]}";
    return json;
}

String htmlEscape(const char* input)
{
    if (input == nullptr)
    {
        return String();
    }

    String out;
    out.reserve(strlen(input) + 16);
    for (const char* p = input; *p != '\0'; ++p)
    {
        switch (*p)
        {
            case '&': out += F("&amp;"); break;
            case '<': out += F("&lt;"); break;
            case '>': out += F("&gt;"); break;
            case '"': out += F("&quot;"); break;
            case '\'': out += F("&#39;"); break;
            default: out += *p; break;
        }
    }
    return out;
}

bool ensureLittleFsMounted()
{
    static bool initAttempted = false;
    static bool mounted = false;

    if (initAttempted)
    {
        return mounted;
    }

    initAttempted = true;
    mounted = LittleFS.begin(false);
    if (mounted)
    {
        Serial.printf("[CONFIG] LittleFS mounted. total=%u used=%u\n",
                      static_cast<unsigned int>(LittleFS.totalBytes()),
                      static_cast<unsigned int>(LittleFS.usedBytes()));
    }
    else
    {
        Serial.println("[CONFIG] LittleFS mount failed.");
    }

    return mounted;
}

String loadConfigPageTemplateFromFs()
{
    if (!ensureLittleFsMounted())
    {
        return String();
    }

    File file = LittleFS.open(CONFIG_PAGE_FS_PATH, "r");
    if (!file)
    {
        Serial.printf("[CONFIG] Missing HTML template in LittleFS: %s\n", CONFIG_PAGE_FS_PATH);
        return String();
    }

    String html = file.readString();
    file.close();
    return html;
}

String buildConfigPage(const Configuration& cfg)
{
    String timezoneOptions;
    timezoneOptions.reserve(1200);
    for (const auto& option : TIMEZONE_OPTIONS)
    {
        timezoneOptions += F("<option value='");
        timezoneOptions += htmlEscape(option.value);
        timezoneOptions += F("'");
        if (strcmp(option.value, cfg.timezone()) == 0)
        {
            timezoneOptions += F(" selected");
        }
        timezoneOptions += F(">\n");
        timezoneOptions += htmlEscape(option.labelEn);
        timezoneOptions += F("</option>");
    }

    String html = loadConfigPageTemplateFromFs();
    if (html.isEmpty())
    {
        return F("<!doctype html><html><body style='font-family:Verdana,sans-serif;margin:16px'><h2>Configuration page missing</h2><p>Upload filesystem image and ensure /config_page.html is present.</p></body></html>");
    }

    html.replace("{{AP_SSID}}", htmlEscape(cfg.configApSsid()));
    html.replace("{{AP_PASSWORD}}", htmlEscape(cfg.configApPassword()));
    html.replace("{{WIFI_SSID}}", htmlEscape(cfg.wifiSsid()));
    html.replace("{{WIFI_PASSWORD}}", htmlEscape(cfg.wifiPassword()));
    html.replace("{{MQTT_SERVER}}", htmlEscape(cfg.mqttServer()));
    html.replace("{{MQTT_PORT}}", String(static_cast<unsigned int>(cfg.mqttPort())));
    html.replace("{{MQTT_DEPARTURES_TOPIC}}", htmlEscape(cfg.mqttTopicDepartures()));
    html.replace("{{MQTT_WEATHER_TOPIC}}", htmlEscape(cfg.mqttTopicWeather()));
    html.replace("{{TIMEZONE_OPTIONS}}", timezoneOptions);
    
    html.replace("{{WEATHER_DATA_SOURCE_MQTT}}", cfg.weatherDataSourceMode() == Configuration::DataSourceMode::Mqtt ? "selected" : "");
    html.replace("{{WEATHER_DATA_SOURCE_API}}", cfg.weatherDataSourceMode() == Configuration::DataSourceMode::DirectApi ? "selected" : "");
    html.replace("{{WEATHER_API_OPENMETEO}}", cfg.weatherApiProvider() == Configuration::WeatherApiProvider::OpenMeteo ? "selected" : "");
    html.replace("{{WEATHER_API_OWM}}", cfg.weatherApiProvider() == Configuration::WeatherApiProvider::OpenWeatherMap ? "selected" : "");
    html.replace("{{DEPARTURES_DATA_SOURCE_MQTT}}", cfg.departuresDataSourceMode() == Configuration::DataSourceMode::Mqtt ? "selected" : "");
    html.replace("{{DEPARTURES_DATA_SOURCE_API}}", cfg.departuresDataSourceMode() == Configuration::DataSourceMode::DirectApi ? "selected" : "");
    html.replace("{{DEPARTURES_API_BKK}}", cfg.departuresApiProvider() == Configuration::DeparturesApiProvider::Bkk ? "selected" : "");
    html.replace("{{DEPARTURES_API_MOCK}}", cfg.departuresApiProvider() == Configuration::DeparturesApiProvider::MockData ? "selected" : "");
    
    html.replace("{{LOCATION_NAME}}", htmlEscape(cfg.locationName()));
    {
        char latBuf[16], lonBuf[16];
        snprintf(latBuf, sizeof(latBuf), "%.6f", cfg.locationLat());
        snprintf(lonBuf, sizeof(lonBuf), "%.6f", cfg.locationLon());
        html.replace("{{LOCATION_LAT}}", latBuf);
        html.replace("{{LOCATION_LON}}", lonBuf);
    }
    html.replace("{{BKK_API_KEY}}", htmlEscape(cfg.bkkApiKey()));
    html.replace("{{BUS_STOP_ID}}", htmlEscape(cfg.busStopId()));
    html.replace("{{TRAIN_STOP_ID}}", htmlEscape(cfg.trainStopId()));

    // Backward compatibility: if an older filesystem template is still served,
    // inject a visible OTA link block before </body>.
    if (html.indexOf("href='/update'") < 0)
    {
        const char* otaBlock =
            "<div style='margin-top:14px'>"
            "<a href='/update' style='display:inline-block;padding:10px 14px;border-radius:8px;background:#2563eb;color:#fff;font-weight:700;text-decoration:none'>Update firmware</a>"
            "</div>";

        const int bodyClosePos = html.lastIndexOf("</body>");
        if (bodyClosePos >= 0)
        {
            html = html.substring(0, bodyClosePos) + otaBlock + html.substring(bodyClosePos);
        }
        else
        {
            html += otaBlock;
        }
    }
    
    return html;
}

void sendValidationError(AsyncWebServerRequest* request, const char* message)
{
    if (request == nullptr)
    {
        return;
    }

    String html;
    html.reserve(600);
    html += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>Validation error</title></head><body style='font-family:Verdana,sans-serif;margin:16px'>");
    html += F("<h2>Validation error</h2><p>");
    html += htmlEscape(message);
    html += F("</p><p><a href='/'>Back to configuration</a></p></body></html>");
    request->send(400, "text/html", html);
}

void sendPortalRedirect(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    request->redirect("/");
}

const char* httpMethodToString(WebRequestMethodComposite method)
{
    if ((method & HTTP_GET) == HTTP_GET)
    {
        return "GET";
    }
    if ((method & HTTP_POST) == HTTP_POST)
    {
        return "POST";
    }
    if ((method & HTTP_PUT) == HTTP_PUT)
    {
        return "PUT";
    }
    if ((method & HTTP_PATCH) == HTTP_PATCH)
    {
        return "PATCH";
    }
    if ((method & HTTP_DELETE) == HTTP_DELETE)
    {
        return "DELETE";
    }
    if ((method & HTTP_OPTIONS) == HTTP_OPTIONS)
    {
        return "OPTIONS";
    }
    if ((method & HTTP_HEAD) == HTTP_HEAD)
    {
        return "HEAD";
    }
    return "OTHER";
}

String requestArg(AsyncWebServerRequest* request, const char* name)
{
    if (request == nullptr || name == nullptr)
    {
        return String();
    }

    if (request->hasParam(name, true))
    {
        return request->getParam(name, true)->value();
    }
    if (request->hasParam(name))
    {
        return request->getParam(name)->value();
    }

    return String();
}

bool hasRequestArg(AsyncWebServerRequest* request, const char* name)
{
    if (request == nullptr || name == nullptr)
    {
        return false;
    }
    return request->hasParam(name, true) || request->hasParam(name);
}

void logWebRequest(AsyncWebServerRequest* request, const char* handlerName)
{
    if (request == nullptr)
    {
        return;
    }

    Serial.printf("[CONFIG][WEB] %s %s -> %s\n",
                  httpMethodToString(request->method()),
                  request->url().c_str(),
                  handlerName);
}

void sendRebootResponse(AsyncWebServerRequest* request, bool isHu)
{
    if (request == nullptr)
    {
        return;
    }

    if (isHu)
    {
        request->send(200,
                      "text/html",
                      "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Ujrainditas</title></head>"
                      "<body style='font-family:Verdana,sans-serif;margin:16px'><h2>ESP ujrainditas...</h2><p>Par masodperc mulva toltsd ujra az oldalt.</p></body></html>");
    }
    else
    {
        request->send(200,
                      "text/html",
                      "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Reboot</title></head>"
                      "<body style='font-family:Verdana,sans-serif;margin:16px'><h2>ESP rebooting...</h2><p>Reload this page in a few seconds.</p></body></html>");
    }
}
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void Configuration::load()
{
    loadDefaults();

    Preferences prefs;
    if (!prefs.begin(CONFIG_PREF_NS, true))
    {
        Serial.println("[CONFIG] NVS open failed (read), using defaults.");
        return;
    }

    const bool hasStoredConfig = prefs.getBool("has", false);
    if (!hasStoredConfig)
    {
        prefs.end();
        Serial.println("[CONFIG] No stored configuration found, using defaults.");
        return;
    }

    const String wifiSsid = prefs.getString("wifi_ssid", m_wifiSsid);
    const String wifiPassword = prefs.getString("wifi_pwd", m_wifiPassword);
    const String mqttServer = prefs.getString("mqtt_srv", m_mqttServer);
    const uint16_t mqttPort = prefs.getUShort("mqtt_port", m_mqttPort);
    const String depTopic = prefs.getString("mqtt_dep", m_mqttTopicDepartures);
    const String weatherTopic = prefs.getString("mqtt_wth", m_mqttTopicWeather);
    const String timezone = prefs.getString("tz", m_timezone);

    const bool hasWeatherSrcKey = prefs.isKey("wth_src");
    const bool hasDeparturesSrcKey = prefs.isKey("dep_src");
    const bool legacyUseMqtt = prefs.getBool("use_mqtt", DUMMY_DEFAULT_USE_MQTT);
    const uint8_t legacySourceMode = legacyUseMqtt
                                     ? static_cast<uint8_t>(DataSourceMode::Mqtt)
                                     : static_cast<uint8_t>(DataSourceMode::DirectApi);

    const uint8_t weatherDataSourceMode = hasWeatherSrcKey
                                          ? prefs.getUChar("wth_src", static_cast<uint8_t>(DataSourceMode::Mqtt))
                                          : legacySourceMode;
    const uint8_t departuresDataSourceMode = hasDeparturesSrcKey
                                             ? prefs.getUChar("dep_src", static_cast<uint8_t>(DataSourceMode::Mqtt))
                                             : legacySourceMode;

    const uint8_t weatherApiProvider = prefs.getUChar("wth_api", static_cast<uint8_t>(WeatherApiProvider::OpenMeteo));
    const uint8_t departuresApiProvider = prefs.getUChar("dep_api", static_cast<uint8_t>(DeparturesApiProvider::Bkk));
    const String locationName = prefs.getString("location", m_locationName);
    const float locationLat = prefs.getFloat("loc_lat", 0.0f);
    const float locationLon = prefs.getFloat("loc_lon", 0.0f);
    const String bkkApiKey = prefs.getString("bkk_key", m_bkkApiKey);
    const bool hasBusStopKey = prefs.isKey("bus_stop");
    const bool hasTrainStopKey = prefs.isKey("train_stop");
    const String legacyBkkStopId = prefs.getString("bkk_stop", "");
    const String busStopId = hasBusStopKey ? prefs.getString("bus_stop", m_busStopId) : legacyBkkStopId;
    const String trainStopId = hasTrainStopKey ? prefs.getString("train_stop", m_trainStopId) : legacyBkkStopId;
    prefs.end();

    if (!hasWeatherSrcKey || !hasDeparturesSrcKey)
    {
        Serial.printf("[CONFIG] Legacy migration: use_mqtt=%s -> weatherSource=%u departuresSource=%u\n",
                      legacyUseMqtt ? "true" : "false",
                      static_cast<unsigned int>(weatherDataSourceMode),
                      static_cast<unsigned int>(departuresDataSourceMode));
    }
    if ((!hasBusStopKey || !hasTrainStopKey) && legacyBkkStopId.length() > 0)
    {
        Serial.printf("[CONFIG] Legacy migration: bkk_stop='%s' copied to bus/train stop\n", legacyBkkStopId.c_str());
    }

    strlcpy(m_wifiSsid, wifiSsid.c_str(), sizeof(m_wifiSsid));
    strlcpy(m_wifiPassword, wifiPassword.c_str(), sizeof(m_wifiPassword));
    strlcpy(m_mqttServer, mqttServer.c_str(), sizeof(m_mqttServer));
    m_mqttPort = mqttPort;
    strlcpy(m_mqttTopicDepartures, depTopic.c_str(), sizeof(m_mqttTopicDepartures));
    strlcpy(m_mqttTopicWeather, weatherTopic.c_str(), sizeof(m_mqttTopicWeather));
    strlcpy(m_timezone, timezone.c_str(), sizeof(m_timezone));
    m_weatherDataSourceMode = static_cast<DataSourceMode>(weatherDataSourceMode);
    m_departuresDataSourceMode = static_cast<DataSourceMode>(departuresDataSourceMode);
    m_weatherApiProvider = static_cast<WeatherApiProvider>(weatherApiProvider);
    m_departuresApiProvider = static_cast<DeparturesApiProvider>(departuresApiProvider);
    strlcpy(m_locationName, locationName.c_str(), sizeof(m_locationName));
    m_locationLat = locationLat;
    m_locationLon = locationLon;
    strlcpy(m_bkkApiKey, bkkApiKey.c_str(), sizeof(m_bkkApiKey));
    strlcpy(m_busStopId, busStopId.c_str(), sizeof(m_busStopId));
    strlcpy(m_trainStopId, trainStopId.c_str(), sizeof(m_trainStopId));

    Serial.println("[CONFIG] Configuration loaded from NVS.");
    Serial.printf("[CONFIG] Loaded values: wifiSsid='%s' wifiPassword='%s' mqttServer='%s' mqttPort=%u depTopic='%s' weatherTopic='%s' tz='%s'\n",
                  m_wifiSsid,
                  m_wifiPassword,
                  m_mqttServer,
                  static_cast<unsigned int>(m_mqttPort),
                  m_mqttTopicDepartures,
                  m_mqttTopicWeather,
                  m_timezone);
    Serial.printf("[CONFIG] Data sources: weather=%u departures=%u location='%s' busStop='%s' trainStop='%s'\n",
                  static_cast<unsigned int>(m_weatherDataSourceMode),
                  static_cast<unsigned int>(m_departuresDataSourceMode),
                  m_locationName,
                  m_busStopId,
                  m_trainStopId);
}

void Configuration::loadDefaults()
{
    strlcpy(m_wifiSsid,               SETTINGS_WIFI_SSID,              sizeof(m_wifiSsid));
    strlcpy(m_wifiPassword,           SETTINGS_WIFI_PASSWORD,          sizeof(m_wifiPassword));
    strlcpy(m_mqttServer,             SETTINGS_MQTT_SERVER,            sizeof(m_mqttServer));
    m_mqttPort = SETTINGS_MQTT_PORT;
    strlcpy(m_mqttTopicDepartures,    SETTINGS_MQTT_TOPIC_DEPARTURES,  sizeof(m_mqttTopicDepartures));
    strlcpy(m_mqttTopicWeather,       SETTINGS_MQTT_TOPIC_WEATHER,     sizeof(m_mqttTopicWeather));
    m_weatherDataSourceMode = DataSourceMode::Mqtt;
    m_departuresDataSourceMode = DataSourceMode::Mqtt;
    
#if defined(SETTINGS_BKK_API_KEY) && defined(SETTINGS_BKK_STOP_ID)
    strlcpy(m_bkkApiKey,              SETTINGS_BKK_API_KEY,            sizeof(m_bkkApiKey));
    strlcpy(m_busStopId,              SETTINGS_BKK_STOP_ID,            sizeof(m_busStopId));
#endif
    
    // Timezone default is already set by the member initialiser; do not overwrite
    // unless a stored setting is available in the future.
}

void Configuration::save()
{
    Preferences prefs;
    if (!prefs.begin(CONFIG_PREF_NS, false))
    {
        Serial.println("[CONFIG] NVS open failed (write), save aborted.");
        return;
    }

    prefs.putString("wifi_ssid", m_wifiSsid);
    prefs.putString("wifi_pwd", m_wifiPassword);
    prefs.putString("mqtt_srv", m_mqttServer);
    prefs.putUShort("mqtt_port", m_mqttPort);
    prefs.putString("mqtt_dep", m_mqttTopicDepartures);
    prefs.putString("mqtt_wth", m_mqttTopicWeather);
    prefs.putString("tz", m_timezone);
    prefs.putUChar("wth_src", static_cast<uint8_t>(m_weatherDataSourceMode));
    prefs.putUChar("dep_src", static_cast<uint8_t>(m_departuresDataSourceMode));
    prefs.putUChar("wth_api", static_cast<uint8_t>(m_weatherApiProvider));
    prefs.putUChar("dep_api", static_cast<uint8_t>(m_departuresApiProvider));
    prefs.putString("location", m_locationName);
    prefs.putFloat("loc_lat", m_locationLat);
    prefs.putFloat("loc_lon", m_locationLon);
    prefs.putString("bkk_key", m_bkkApiKey);
    prefs.putString("bus_stop", m_busStopId);
    prefs.putString("train_stop", m_trainStopId);
    prefs.putBool("has", true);
    prefs.end();

    Serial.printf("[CONFIG] Configuration saved to NVS. weatherSource=%u departuresSource=%u weatherApi=%u departuresApi=%u\n",
                  static_cast<unsigned int>(m_weatherDataSourceMode),
                  static_cast<unsigned int>(m_departuresDataSourceMode),
                  static_cast<unsigned int>(m_weatherApiProvider),
                  static_cast<unsigned int>(m_departuresApiProvider));
    Serial.printf("[CONFIG] Saved location='%s' busStop='%s' trainStop='%s'\n",
                  m_locationName,
                  m_busStopId,
                  m_trainStopId);
}

void Configuration::scheduleReboot(uint32_t delayMs)
{
    m_rebootPending = true;
    m_rebootAtMs = millis() + delayMs;
    Serial.printf("[CONFIG] Reboot scheduled in %lu ms.\n", static_cast<unsigned long>(delayMs));
}

bool Configuration::beginConfigMode()
{
    if (m_configModeActive)
    {
        return true;
    }

    generateConfigApCredentials();

    // Make AP startup deterministic by clearing any residual STA/AP state first.
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
    delay(50);

    if (!WiFi.mode(WIFI_AP))
    {
        Serial.println("[CONFIG] Failed to switch WiFi to AP mode.");
        return false;
    }

    delay(50);

    const bool apStarted = WiFi.softAP(m_apSsid, m_apPassword);
    if (!apStarted)
    {
        Serial.println("[CONFIG] Failed to start AP mode.");
        return false;
    }

    uint8_t apIpPolls = 0;
    while (WiFi.softAPIP() == INADDR_NONE && apIpPolls < 40)
    {
        delay(25);
        ++apIpPolls;
    }

    if (m_webServer == nullptr)
    {
        m_webServer = new AsyncWebServer(80);
    }

    if (m_webServer == nullptr)
    {
        Serial.println("[CONFIG] Failed to allocate AsyncWebServer.");
        return false;
    }

    if (!m_webRoutesRegistered)
    {
        setupWebServerRoutes();
        ElegantOTA.begin(m_webServer);
        ElegantOTA.onStart([]() {
            Serial.println("[CONFIG][OTA] OTA update started.");
        });
        ElegantOTA.onProgress([](size_t current, size_t final) {
            static unsigned long lastLogMs = 0;
            const unsigned long nowMs = millis();
            if (nowMs - lastLogMs >= 1000)
            {
                lastLogMs = nowMs;
                Serial.printf("[CONFIG][OTA] Progress: %u / %u bytes\n",
                              static_cast<unsigned int>(current),
                              static_cast<unsigned int>(final));
            }
        });
        ElegantOTA.onEnd([](bool success) {
            Serial.printf("[CONFIG][OTA] OTA update finished: %s\n", success ? "success" : "failed");
        });
        m_webRoutesRegistered = true;
    }

    m_webServer->begin();

    if (m_dnsServer == nullptr)
    {
        m_dnsServer = new DNSServer();
    }

    if (m_dnsServer != nullptr)
    {
        m_dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
        m_dnsServer->start(CAPTIVE_DNS_PORT, "*", WiFi.softAPIP());
        Serial.println("[CONFIG] Captive DNS server started.");
    }
    else
    {
        Serial.println("[CONFIG] Failed to allocate DNSServer.");
    }

    const IPAddress ip = WiFi.softAPIP();
    Serial.printf("[CONFIG] AP started. SSID=%s, PASS=%s, IP=%s\n",
                  m_apSsid,
                  m_apPassword,
                  ip.toString().c_str());

    m_configModeActive = true;
    displayBegin();
    renderConfigScreen();
    return true;
}

void Configuration::handleConfigMode()
{
    if (!m_configModeActive)
    {
        return;
    }

    if (m_dnsServer != nullptr)
    {
        m_dnsServer->processNextRequest();
    }

    ElegantOTA.loop();

    if (m_rebootPending && static_cast<int32_t>(millis() - m_rebootAtMs) >= 0)
    {
        Serial.println("[CONFIG] Rebooting ESP now.");
        delay(50);
        ESP.restart();
    }
}

// ── Mutators ──────────────────────────────────────────────────────────────────

void Configuration::setWifiSsid(const char* ssid)
{
    if (ssid != nullptr)
    {
        strlcpy(m_wifiSsid, ssid, sizeof(m_wifiSsid));
    }
}

void Configuration::setWifiPassword(const char* password)
{
    if (password != nullptr)
    {
        strlcpy(m_wifiPassword, password, sizeof(m_wifiPassword));
    }
}

void Configuration::setMqttServer(const char* server)
{
    if (server != nullptr)
    {
        strlcpy(m_mqttServer, server, sizeof(m_mqttServer));
    }
}

void Configuration::setMqttPort(uint16_t port)
{
    m_mqttPort = port;
}

void Configuration::setMqttTopicDepartures(const char* topic)
{
    if (topic != nullptr)
    {
        strlcpy(m_mqttTopicDepartures, topic, sizeof(m_mqttTopicDepartures));
    }
}

void Configuration::setMqttTopicWeather(const char* topic)
{
    if (topic != nullptr)
    {
        strlcpy(m_mqttTopicWeather, topic, sizeof(m_mqttTopicWeather));
    }
}

void Configuration::setTimezone(const char* tz)
{
    if (tz != nullptr)
    {
        strlcpy(m_timezone, tz, sizeof(m_timezone));
    }
}

void Configuration::setWeatherDataSourceMode(DataSourceMode mode)
{
    m_weatherDataSourceMode = mode;
}

void Configuration::setDeparturesDataSourceMode(DataSourceMode mode)
{
    m_departuresDataSourceMode = mode;
}

void Configuration::setWeatherApiProvider(WeatherApiProvider provider)
{
    m_weatherApiProvider = provider;
}

void Configuration::setDeparturesApiProvider(DeparturesApiProvider provider)
{
    m_departuresApiProvider = provider;
}

void Configuration::setLocationName(const char* location)
{
    if (location != nullptr)
    {
        strlcpy(m_locationName, location, sizeof(m_locationName));
    }
}

void Configuration::setLocationCoordinates(float lat, float lon)
{
    m_locationLat = lat;
    m_locationLon = lon;
}

void Configuration::setBkkApiKey(const char* key)
{
    if (key != nullptr)
    {
        strlcpy(m_bkkApiKey, key, sizeof(m_bkkApiKey));
    }
}

void Configuration::setBusStopId(const char* stopId)
{
    if (stopId != nullptr)
    {
        strlcpy(m_busStopId, stopId, sizeof(m_busStopId));
    }
}

void Configuration::setTrainStopId(const char* stopId)
{
    if (stopId != nullptr)
    {
        strlcpy(m_trainStopId, stopId, sizeof(m_trainStopId));
    }
}

bool Configuration::resolveLocationCoordinates(const char* city, float& lat, float& lon)
{
    // If coordinates were stored from the geocoding autocomplete, use them directly.
    if (m_locationLat != 0.0f || m_locationLon != 0.0f)
    {
        lat = m_locationLat;
        lon = m_locationLon;
        return true;
    }

    return findKnownLocationCoordinates(city, lat, lon);
}

void Configuration::generateConfigApCredentials()
{
    const uint64_t chipId = ESP.getEfuseMac();
    const uint32_t lo = static_cast<uint32_t>(chipId & 0xFFFFFFULL);
    const uint32_t hi = static_cast<uint32_t>((chipId >> 24) & 0xFFFFFFULL);

    snprintf(m_apSsid, sizeof(m_apSsid), "ESP32CFG-%06X", static_cast<unsigned int>(lo));
    snprintf(m_apPassword,
             sizeof(m_apPassword),
             "CFG%06X%06X",
             static_cast<unsigned int>(hi),
             static_cast<unsigned int>(lo));
}

void Configuration::renderConfigScreen()
{
    Serial.printf("[CONFIG] Render values: wifiSsid='%s' wifiPassword='%s' mqttServer='%s' mqttPort=%u depTopic='%s' weatherTopic='%s' apSsid='%s' apPassword='%s'\n",
                  wifiSsid(),
                  wifiPassword(),
                  mqttServer(),
                  static_cast<unsigned int>(mqttPort()),
                  mqttTopicDepartures(),
                  mqttTopicWeather(),
                  configApSsid(),
                  configApPassword());

    displayShowConfigurationScreen(wifiSsid(),
                                   wifiPassword(),
                                   mqttServer(),
                                   mqttPort(),
                                   mqttTopicDepartures(),
                                   mqttTopicWeather(),
                                   configApSsid(),
                                   configApPassword());
}

void Configuration::setupWebServerRoutes()
{
    if (m_webServer == nullptr)
    {
        return;
    }

    m_webServer->on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleRootGet(request);
    });

    // Common captive portal probes across major clients.
    m_webServer->on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleCaptiveProbeGet(request);
    });
    m_webServer->on("/gen_204", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleCaptiveProbeGet(request);
    });
    m_webServer->on("/hotspot-detect.html", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleCaptiveProbeGet(request);
    });
    m_webServer->on("/ncsi.txt", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleCaptiveProbeGet(request);
    });
    m_webServer->on("/connecttest.txt", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleCaptiveProbeGet(request);
    });
    m_webServer->on("/redirect", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleCaptiveProbeGet(request);
    });

    m_webServer->on("/save", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleSavePost(request);
    });
    m_webServer->on("/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleRebootPost(request);
    });
    m_webServer->on("/reboot-now", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleRebootNowGet(request);
    });
    m_webServer->on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleApiSettingsGet(request);
    });

    m_webServer->onNotFound([this](AsyncWebServerRequest* request) {
        handleNotFound(request);
    });
}

void Configuration::handleRootGet(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleRootGet");

    AsyncWebServerResponse* response = request->beginResponse(200, "text/html", buildConfigPage(*this));
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    request->send(response);
}

void Configuration::handleCaptiveProbeGet(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleCaptiveProbeGet");
    sendPortalRedirect(request);
}

void Configuration::handleSavePost(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleSavePost");

    if (!(hasRequestArg(request, "wifi_ssid") &&
          hasRequestArg(request, "wifi_password") &&
          hasRequestArg(request, "mqtt_server") &&
          hasRequestArg(request, "mqtt_port") &&
          hasRequestArg(request, "mqtt_departures_topic") &&
          hasRequestArg(request, "mqtt_weather_topic") &&
          hasRequestArg(request, "timezone") &&
          hasRequestArg(request, "weather_data_source") &&
          hasRequestArg(request, "departures_data_source") &&
          hasRequestArg(request, "weather_api_provider") &&
          hasRequestArg(request, "departures_api_provider") &&
          hasRequestArg(request, "location_name")))
    {
        sendValidationError(request, "Missing required fields.");
        return;
    }

    const String wifiSsid = trimCopy(requestArg(request, "wifi_ssid"));
    const String wifiPassword = trimCopy(requestArg(request, "wifi_password"));
    const String mqttServer = trimCopy(requestArg(request, "mqtt_server"));
    const String mqttPortText = trimCopy(requestArg(request, "mqtt_port"));
    const String depTopic = trimCopy(requestArg(request, "mqtt_departures_topic"));
    const String weatherTopic = trimCopy(requestArg(request, "mqtt_weather_topic"));
    const String timezone = trimCopy(requestArg(request, "timezone"));
    const String weatherDataSourceText = trimCopy(requestArg(request, "weather_data_source"));
    const String departuresDataSourceText = trimCopy(requestArg(request, "departures_data_source"));
    const String weatherApiProviderText = trimCopy(requestArg(request, "weather_api_provider"));
    const String departuresApiProviderText = trimCopy(requestArg(request, "departures_api_provider"));
    const String locationName = trimCopy(requestArg(request, "location_name"));
    const String bkkApiKey = trimCopy(requestArg(request, "bkk_api_key"));
    const String busStopId = trimCopy(requestArg(request, "bus_stop_id"));
    const String trainStopId = trimCopy(requestArg(request, "train_stop_id"));

    if (wifiSsid.isEmpty() || wifiSsid.length() > (sizeof(m_wifiSsid) - 1) || !isAsciiPrintableNoCtrl(wifiSsid))
    {
        sendValidationError(request, "Invalid WiFi SSID (1-63 printable ASCII chars).");
        return;
    }

    if (wifiPassword.length() < 8 || wifiPassword.length() > (sizeof(m_wifiPassword) - 1) || !isAsciiPrintableNoCtrl(wifiPassword))
    {
        sendValidationError(request, "Invalid WiFi password (8-63 printable ASCII chars).");
        return;
    }

    if (!isValidHostOrIpv4(mqttServer))
    {
        sendValidationError(request, "Invalid MQTT server host/IP.");
        return;
    }

    const long port = mqttPortText.toInt();
    if (port <= 0 || port > 65535)
    {
        sendValidationError(request, "Invalid MQTT port (1-65535).");
        return;
    }

    if (!isValidMqttTopic(depTopic))
    {
        sendValidationError(request, "Invalid departures MQTT topic.");
        return;
    }

    if (!isValidMqttTopic(weatherTopic))
    {
        sendValidationError(request, "Invalid weather MQTT topic.");
        return;
    }

    if (!isAllowedTimezoneValue(timezone))
    {
        sendValidationError(request, "Invalid timezone selection.");
        return;
    }

    if (locationName.length() > (sizeof(m_locationName) - 1))
    {
        sendValidationError(request, "Invalid location name (1-48 chars).");
        return;
    }

    float locationLat = 0.0f, locationLon = 0.0f;
    if (!locationLatText.isEmpty() && !locationLonText.isEmpty())
    {
        const float parsedLat = locationLatText.toFloat();
        const float parsedLon = locationLonText.toFloat();
        if (parsedLat >= -90.0f && parsedLat <= 90.0f &&
            parsedLon >= -180.0f && parsedLon <= 180.0f)
        {
            locationLat = parsedLat;
            locationLon = parsedLon;
        }
    }

    const long weatherDataSourceVal = weatherDataSourceText.toInt();
    if (weatherDataSourceVal < 0 || weatherDataSourceVal > 1)
    {
        sendValidationError(request, "Invalid weather data source.");
        return;
    }

    const long departuresDataSourceVal = departuresDataSourceText.toInt();
    if (departuresDataSourceVal < 0 || departuresDataSourceVal > 1)
    {
        sendValidationError(request, "Invalid departures data source.");
        return;
    }

    const long weatherApiProviderVal = weatherApiProviderText.toInt();
    if (weatherApiProviderVal < 0 || weatherApiProviderVal > 1)
    {
        sendValidationError(request, "Invalid weather API provider.");
        return;
    }

    const long departuresApiProviderVal = departuresApiProviderText.toInt();
    if (departuresApiProviderVal < 0 || departuresApiProviderVal > 1)
    {
        sendValidationError(request, "Invalid departures API provider.");
        return;
    }

    const bool weatherUsesDirectApi = weatherDataSourceVal == static_cast<long>(DataSourceMode::DirectApi);
    const bool weatherUsesOpenMeteo = weatherApiProviderVal == static_cast<long>(WeatherApiProvider::OpenMeteo);
    if (weatherUsesDirectApi && weatherUsesOpenMeteo)
    {
        if (locationName.isEmpty())
        {
            sendValidationError(*m_webServer, "Location is required when weather source is Direct API.");
            return;
        }

        if (locationLat == 0.0f && locationLon == 0.0f &&
            !findKnownLocationCoordinates(locationName.c_str(), locationLat, locationLon))
        {
            sendValidationError(*m_webServer,
                                "Unknown location. Select a suggestion from the list or use one of the built-in cities.");
            return;
        }
    }

    Serial.printf("[CONFIG] Save request parsed: weatherSource=%ld departuresSource=%ld weatherApi=%ld departuresApi=%ld location='%s'\n",
                  weatherDataSourceVal,
                  departuresDataSourceVal,
                  weatherApiProviderVal,
                  departuresApiProviderVal,
                  locationName.c_str());

    setWifiSsid(wifiSsid.c_str());
    setWifiPassword(wifiPassword.c_str());
    setMqttServer(mqttServer.c_str());
    setMqttPort(static_cast<uint16_t>(port));
    setMqttTopicDepartures(depTopic.c_str());
    setMqttTopicWeather(weatherTopic.c_str());
    setTimezone(timezone.c_str());
    setWeatherDataSourceMode(static_cast<DataSourceMode>(weatherDataSourceVal));
    setDeparturesDataSourceMode(static_cast<DataSourceMode>(departuresDataSourceVal));
    setWeatherApiProvider(static_cast<WeatherApiProvider>(weatherApiProviderVal));
    setDeparturesApiProvider(static_cast<DeparturesApiProvider>(departuresApiProviderVal));
    setLocationName(locationName.c_str());
    setLocationCoordinates(locationLat, locationLon);
    setBkkApiKey(bkkApiKey.c_str());
    setBusStopId(busStopId.c_str());
    setTrainStopId(trainStopId.c_str());

    save();
    renderConfigScreen();

    request->redirect("/");
}

void Configuration::handleRebootPost(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleRebootPost");
    Serial.println("[CONFIG] /reboot requested (POST).");

    const String uiLang = requestArg(request, "ui_lang");
    const bool isHu = uiLang.equalsIgnoreCase("hu");

    sendRebootResponse(request, isHu);

    delay(350);
    ESP.restart();
}

void Configuration::handleRebootNowGet(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleRebootNowGet");
    Serial.println("[CONFIG] /reboot-now requested (GET fallback).");
    const String uiLang = requestArg(request, "ui_lang");
    const bool isHu = uiLang.equalsIgnoreCase("hu");

    sendRebootResponse(request, isHu);

    delay(350);
    ESP.restart();
}

void Configuration::handleApiSettingsGet(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleApiSettingsGet");

    String json;
    json.reserve(512);
    json += "{";
    json += "\"wifiSsid\":\"";
    json += wifiSsid();
    json += "\",";
    json += "\"mqttServer\":\"";
    json += mqttServer();
    json += "\",";
    json += "\"mqttPort\":";
    json += String(static_cast<unsigned int>(mqttPort()));
    json += ",";
    json += "\"mqttDeparturesTopic\":\"";
    json += mqttTopicDepartures();
    json += "\",";
    json += "\"mqttWeatherTopic\":\"";
    json += mqttTopicWeather();
    json += "\",";
    json += "\"timezone\":\"";
    json += timezone();
    json += "\",";
    json += "\"weatherDataSource\":";
    json += String(static_cast<unsigned int>(weatherDataSourceMode()));
    json += ",";
    json += "\"departuresDataSource\":";
    json += String(static_cast<unsigned int>(departuresDataSourceMode()));
    json += ",";
    json += "\"location\":\"";
    json += locationName();
    json += "\"";
    json += "}";
    request->send(200, "application/json", json);
}

void Configuration::handleNotFound(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleNotFound");
    sendPortalRedirect(request);
}
