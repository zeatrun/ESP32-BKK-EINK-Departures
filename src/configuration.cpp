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

bool parseUiDataSourceMode(const String& value, Configuration::DataSourceMode& outMode)
{
    String v = value;
    v.trim();

    if (v.equalsIgnoreCase("0") || v.equalsIgnoreCase("direct_api") || v.equalsIgnoreCase("api"))
    {
        outMode = Configuration::DataSourceMode::DirectApi;
        return true;
    }

    if (v.equalsIgnoreCase("1") || v.equalsIgnoreCase("mqtt"))
    {
        outMode = Configuration::DataSourceMode::Mqtt;
        return true;
    }

    return false;
}

uint8_t dataSourceModeToUiValue(Configuration::DataSourceMode mode)
{
    return (mode == Configuration::DataSourceMode::DirectApi) ? 0U : 1U;
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

    // Use AP+STA mode so we can test WiFi connections while config portal is running
    if (!WiFi.mode(WIFI_AP_STA))
    {
        Serial.println("[CONFIG] Failed to switch WiFi to AP_STA mode.");
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

    // Serve React bundled assets directly when present.
    m_webServer->serveStatic("/assets", LittleFS, "/config-app/assets");

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
    m_webServer->on("/api/geocode", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleApiGeocodeGet(request);
    });
    m_webServer->on("/api/wifi-scan", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleApiWifiScanGet(request);
    });
    m_webServer->on("/api/wifi-test", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleApiWifiTestPost(request);
    });
    m_webServer->on("/api/config/save", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleApiConfigSavePost(request);
    });
    m_webServer->on("/api/config/reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleApiConfigResetPost(request);
    });
    m_webServer->on("/api/weather-test", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleApiWeatherTestGet(request);
    });
    m_webServer->on("/api/departures-test", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleApiDeparturesTestPost(request);
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

    // Try to serve React app first
    if (LittleFS.exists("/config-app/index.html"))
    {
        AsyncWebServerResponse* response = request->beginResponse(LittleFS, "/config-app/index.html", "text/html");
        response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        response->addHeader("Pragma", "no-cache");
        response->addHeader("Expires", "0");
        request->send(response);
        return;
    }

    // Fallback to old HTML config page
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
    const String locationLatText = hasRequestArg(request, "location_lat") ? trimCopy(requestArg(request, "location_lat")) : String();
    const String locationLonText = hasRequestArg(request, "location_lon") ? trimCopy(requestArg(request, "location_lon")) : String();
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

    DataSourceMode weatherSourceMode = DataSourceMode::Mqtt;
    if (!parseUiDataSourceMode(weatherDataSourceText, weatherSourceMode))
    {
        sendValidationError(request, "Invalid weather data source.");
        return;
    }

    DataSourceMode departuresSourceMode = DataSourceMode::Mqtt;
    if (!parseUiDataSourceMode(departuresDataSourceText, departuresSourceMode))
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

    const bool weatherUsesDirectApi = weatherSourceMode == DataSourceMode::DirectApi;
    const bool weatherUsesOpenMeteo = weatherApiProviderVal == static_cast<long>(WeatherApiProvider::OpenMeteo);
    if (weatherUsesDirectApi && weatherUsesOpenMeteo)
    {
        if (locationName.isEmpty())
        {
            sendValidationError(request, "Location is required when weather source is Direct API.");
            return;
        }

        if (locationLat == 0.0f && locationLon == 0.0f &&
            !findKnownLocationCoordinates(locationName.c_str(), locationLat, locationLon))
        {
            sendValidationError(request,
                                "Unknown location. Select a suggestion from the list or use one of the built-in cities.");
            return;
        }
    }

    Serial.printf("[CONFIG] Save request parsed: weatherSource=%u departuresSource=%u weatherApi=%ld departuresApi=%ld location='%s'\n",
                  static_cast<unsigned int>(weatherSourceMode),
                  static_cast<unsigned int>(departuresSourceMode),
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
    setWeatherDataSourceMode(weatherSourceMode);
    setDeparturesDataSourceMode(departuresSourceMode);
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
    json += String(static_cast<unsigned int>(dataSourceModeToUiValue(weatherDataSourceMode())));
    json += ",";
    json += "\"departuresDataSource\":";
    json += String(static_cast<unsigned int>(dataSourceModeToUiValue(departuresDataSourceMode())));
    json += ",";
    json += "\"location\":\"";
    json += locationName();
    json += "\"";
    json += "}";
    request->send(200, "application/json", json);
}

void Configuration::handleApiGeocodeGet(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleApiGeocodeGet");

    const String query = hasRequestArg(request, "q") ? trimCopy(requestArg(request, "q")) : String();

    // If WiFi is connected, proxy to Open-Meteo geocoding API
    if (WiFi.status() == WL_CONNECTED && !query.isEmpty())
    {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;

        String url = String("https://geocoding-api.open-meteo.com/v1/search?name=")
                     + urlEncode(query)
                     + "&count=10&language=en&format=json";

        if (http.begin(client, url))
        {
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK)
            {
                String payload = http.getString();
                http.end();
                AsyncWebServerResponse* response = request->beginResponse(200, "application/json", payload);
                response->addHeader("Access-Control-Allow-Origin", "*");
                request->send(response);
                return;
            }
            http.end();
        }
    }

    // Offline fallback: return matching entries from KNOWN_LOCATIONS
    String json = buildKnownLocationsGeocodeJson(query);
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

void Configuration::handleApiWifiScanGet(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleApiWifiScanGet");

    const bool forceRefresh = hasRequestArg(request, "refresh") && requestArg(request, "refresh") == "1";

    auto sendScanResponse = [request](const char* status, int networkCount)
    {
        String json = String("{\"status\":\"") + status + "\",\"networks\":[";
        bool first = true;
        for (int i = 0; i < networkCount; ++i)
        {
            if (!first)
            {
                json += ',';
            }
            first = false;
            json += "{\"ssid\":\"";
            json += WiFi.SSID(i);
            json += "\",\"rssi\":";
            json += WiFi.RSSI(i);
            json += ",\"open\":";
            json += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "true" : "false";
            json += "}";
        }
        json += "]}";

        AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    };

    if (forceRefresh)
    {
        WiFi.scanDelete();
        WiFi.scanNetworks(true, false); // async scan start
        sendScanResponse("scanning", 0);
        return;
    }

    const int scanState = WiFi.scanComplete();

    if (scanState == -1)
    {
        sendScanResponse("scanning", 0);
        return;
    }

    if (scanState >= 0)
    {
        sendScanResponse("done", scanState);
        WiFi.scanDelete();
        return;
    }

    // Failed or not started yet: kick off a new async scan and ask client to poll.
    WiFi.scanDelete();
    WiFi.scanNetworks(true, false);
    sendScanResponse("scanning", 0);
    return;
}


void Configuration::handleApiWifiTestPost(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleApiWifiTestPost");

    // Prefer form/query params; fallback to JSON body for compatibility.
    String ssid = trimCopy(requestArg(request, "ssid"));
    String password = requestArg(request, "password");

    if (ssid.isEmpty() || password.isEmpty())
    {
        if (request->hasArg("plain"))
        {
            String body = request->arg("plain");
            JsonDocument doc;
            if (deserializeJson(doc, body) == DeserializationError::Ok)
            {
                if (ssid.isEmpty())
                {
                    ssid = trimCopy(doc["ssid"].as<String>());
                }
                if (password.isEmpty())
                {
                    password = doc["password"].as<String>();
                }
            }
        }
    }

    if (ssid.isEmpty() || password.isEmpty())
    {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing ssid or password\"}");
        return;
    }

    // Test WiFi connection in STA mode
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    bool connected = false;
    for (int i = 0; i < 20; i++)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            connected = true;
            break;
        }
        delay(250);
    }

    WiFi.disconnect(false); // Keep AP running
    WiFi.mode(WIFI_AP); // Go back to AP only

    String response = connected
        ? String("{\"success\":true,\"ssid\":\"") + ssid + "\",\"message\":\"Connected\"}"
        : String("{\"success\":false,\"ssid\":\"") + ssid + "\",\"message\":\"Connection failed\"}";

    request->send(200, "application/json", response);
}

void Configuration::handleApiConfigSavePost(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleApiConfigSavePost");

    JsonDocument doc;
    bool hasJsonBody = false;
    if (request->hasArg("plain"))
    {
        DeserializationError error = deserializeJson(doc, request->arg("plain"));
        if (!error)
        {
            hasJsonBody = true;
        }
    }

    auto getField = [&](const char* key) -> String
    {
        String value = trimCopy(requestArg(request, key));
        if (!value.isEmpty())
        {
            return value;
        }
        if (hasJsonBody)
        {
            return trimCopy(doc[key].as<String>());
        }
        return String();
    };

    const String wifiSsid = getField("wifi_ssid");
    const String wifiPassword = getField("wifi_password");
    const String mqttServer = getField("mqtt_server");
    const String mqttPortText = getField("mqtt_port");
    const String depTopic = getField("mqtt_departures_topic");
    const String weatherTopic = getField("mqtt_weather_topic");
    const String timezone = getField("timezone");
    const String weatherDataSourceText = getField("weather_data_source");
    const String departuresDataSourceText = getField("departures_data_source");
    const String weatherApiProviderText = getField("weather_api_provider");
    const String departuresApiProviderText = getField("departures_api_provider");
    const String locationName = getField("location_name");
    const String locationLatText = getField("location_lat");
    const String locationLonText = getField("location_lon");
    const String bkkApiKey = getField("bkk_api_key");
    const String busStopId = getField("bus_stop_id");
    const String trainStopId = getField("train_stop_id");

    if (wifiSsid.isEmpty() || wifiPassword.isEmpty())
    {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing WiFi credentials\"}");
        return;
    }

    const long port = mqttPortText.toInt();
    if (port <= 0 || port > 65535)
    {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid MQTT port\"}");
        return;
    }

    float locationLat = 0.0f;
    float locationLon = 0.0f;
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

    // If no explicit coordinates came from UI, allow fallback by known city names.
    if (locationLat == 0.0f && locationLon == 0.0f && !locationName.isEmpty())
    {
        float knownLat = 0.0f;
        float knownLon = 0.0f;
        if (findKnownLocationCoordinates(locationName.c_str(), knownLat, knownLon))
        {
            locationLat = knownLat;
            locationLon = knownLon;
        }
    }

    setWifiSsid(wifiSsid.c_str());
    setWifiPassword(wifiPassword.c_str());
    setMqttServer(mqttServer.c_str());
    setMqttPort(static_cast<uint16_t>(port));
    setMqttTopicDepartures(depTopic.c_str());
    setMqttTopicWeather(weatherTopic.c_str());
    setTimezone(timezone.c_str());

    DataSourceMode weatherSourceMode = DataSourceMode::Mqtt;
    if (!parseUiDataSourceMode(weatherDataSourceText, weatherSourceMode))
    {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid weather data source\"}");
        return;
    }

    DataSourceMode departuresSourceMode = DataSourceMode::Mqtt;
    if (!parseUiDataSourceMode(departuresDataSourceText, departuresSourceMode))
    {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid departures data source\"}");
        return;
    }

    setWeatherDataSourceMode(weatherSourceMode);
    setDeparturesDataSourceMode(departuresSourceMode);
    setWeatherApiProvider(static_cast<WeatherApiProvider>(weatherApiProviderText.toInt()));
    setDeparturesApiProvider(static_cast<DeparturesApiProvider>(departuresApiProviderText.toInt()));
    setLocationName(locationName.c_str());
    setLocationCoordinates(locationLat, locationLon);
    setBkkApiKey(bkkApiKey.c_str());
    setBusStopId(busStopId.c_str());
    setTrainStopId(trainStopId.c_str());

    save();
    renderConfigScreen();

    request->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration saved\"}");
}

void Configuration::handleApiConfigResetPost(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleApiConfigResetPost");

    // Clear all stored preferences
    Preferences prefs;
    prefs.begin(CONFIG_PREF_NS, false);
    prefs.clear();
    prefs.end();

    loadDefaults();

    request->send(200, "application/json", "{\"success\":true,\"message\":\"Factory reset complete\"}");
    scheduleReboot(2000);
}

void Configuration::handleApiWeatherTestGet(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleApiWeatherTestGet");

    if (!request->hasParam("lat") || !request->hasParam("lon"))
    {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing latitude or longitude parameters\"}");
        return;
    }

    String lat = request->getParam("lat")->value();
    String lon = request->getParam("lon")->value();

    // Parse and validate coordinates
    float latitude = lat.toFloat();
    float longitude = lon.toFloat();

    if (latitude < -90.0f || latitude > 90.0f || longitude < -180.0f || longitude > 180.0f)
    {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid coordinates\"}");
        return;
    }

    // Try real HTTP call to Open-Meteo if WiFi is connected
    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;
        String url = String("http://api.open-meteo.com/v1/forecast?latitude=") +
                     String(latitude, 4) + "&longitude=" + String(longitude, 4) +
                     "&current_weather=true&forecast_days=1";
        http.begin(url);
        int httpCode = http.GET();
        http.end();

        if (httpCode == 200)
        {
            request->send(200, "application/json",
                "{\"success\":true,\"message\":\"Weather data fetched successfully\"}");
        }
        else
        {
            String response = String("{\"success\":false,\"message\":\"Open-Meteo returned HTTP ") +
                              String(httpCode) + "}";
            request->send(200, "application/json", response);
        }
        return;
    }

    // No WiFi — just validate format
    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Coordinates valid (no WiFi to verify live data)\"}" );
}

void Configuration::handleApiDeparturesTestPost(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleApiDeparturesTestPost");

    // Prefer form/query params; fallback to JSON body.
    String apiKey = trimCopy(requestArg(request, "apiKey"));
    String busStopId = trimCopy(requestArg(request, "busStopId"));
    String trainStopId = trimCopy(requestArg(request, "trainStopId"));

    if (apiKey.isEmpty() || (busStopId.isEmpty() && trainStopId.isEmpty()))
    {
        if (request->hasArg("plain"))
        {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, request->arg("plain"));
            if (!error)
            {
                if (apiKey.isEmpty())
                {
                    apiKey = trimCopy(doc["apiKey"].as<String>());
                }
                if (busStopId.isEmpty())
                {
                    busStopId = trimCopy(doc["busStopId"].as<String>());
                }
                if (trainStopId.isEmpty())
                {
                    trainStopId = trimCopy(doc["trainStopId"].as<String>());
                }
            }
        }
    }

    if (apiKey.length() == 0 || (busStopId.length() == 0 && trainStopId.length() == 0))
    {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing API key or stop IDs\"}");
        return;
    }

    // Try real BKK API call if WiFi is connected
    if (WiFi.status() == WL_CONNECTED && busStopId.length() > 0)
    {
        HTTPClient http;
        String url = String("https://futar.bkk.hu/api/query/v1/ws/otp/api/where/arrivals-and-departures-for-stop.json?stopId=") +
                     busStopId + "&minutesBefore=0&minutesAfter=30&key=" + apiKey;
        http.begin(url);
        int httpCode = http.GET();
        http.end();

        if (httpCode == 200)
        {
            request->send(200, "application/json",
                "{\"success\":true,\"message\":\"BKK API connection successful\"}");
        }
        else if (httpCode == 401)
        {
            request->send(200, "application/json",
                "{\"success\":false,\"message\":\"BKK API: invalid API key (401)\"}");
        }
        else if (httpCode == 404)
        {
            request->send(200, "application/json",
                "{\"success\":false,\"message\":\"BKK API: stop not found (404)\"}");
        }
        else
        {
            String errResp = String("{\"success\":false,\"message\":\"BKK API returned HTTP ") +
                             String(httpCode) + "}";
            request->send(200, "application/json", errResp);
        }
        return;
    }

    // No WiFi - just validate format
    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Inputs valid (no WiFi to verify live data)\"}");
}
void Configuration::handleNotFound(AsyncWebServerRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    logWebRequest(request, "handleNotFound");

    // Try to serve static files from React app
    String path = request->url();
    String filePath = "/config-app" + path;
    
    // Try with exact path first
    if (LittleFS.exists(filePath))
    {
        // Determine content type by file extension
        const char* contentType = "text/plain";
        if (filePath.endsWith(".html")) contentType = "text/html";
        else if (filePath.endsWith(".js")) contentType = "application/javascript";
        else if (filePath.endsWith(".css")) contentType = "text/css";
        else if (filePath.endsWith(".json")) contentType = "application/json";
        else if (filePath.endsWith(".woff2")) contentType = "font/woff2";
        else if (filePath.endsWith(".svg")) contentType = "image/svg+xml";
        
        AsyncWebServerResponse* response = request->beginResponse(LittleFS, filePath, contentType);
        request->send(response);
        return;
    }

    // Fallback to captive portal redirect
    sendPortalRedirect(request);
}
