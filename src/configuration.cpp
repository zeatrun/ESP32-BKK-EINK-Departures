#include "configuration.h"
#include "display_manager.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

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

String buildConfigPage(const Configuration& cfg)
{
    String html;
    html.reserve(3200);
    html += F("<!doctype html><html><head><meta charset='utf-8'>");
    html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>ESP32 Config</title>");
    html += F("<style>body{font-family:Verdana,sans-serif;margin:16px;background:#f8fafc;color:#111827;}"
              "h1{font-size:1.2rem;}label{display:block;margin:10px 0 4px;font-weight:600;}"
              "input{width:100%;padding:10px;border:1px solid #cbd5e1;border-radius:8px;}"
              "button{margin-top:14px;padding:10px 14px;border:0;border-radius:8px;background:#0f766e;color:#fff;font-weight:700;}"
              ".card{max-width:720px;margin:0 auto;background:#fff;padding:16px;border-radius:12px;box-shadow:0 2px 10px rgba(0,0,0,0.08);}"
              ".meta{font-size:.9rem;color:#334155;margin-bottom:10px;}</style></head><body>");
    html += F("<div class='card'><h1>ESP32 Configuration</h1>");
    html += F("<div class='meta'>AP SSID: <b>");
    html += htmlEscape(cfg.configApSsid());
    html += F("</b><br>AP Password: <b>");
    html += htmlEscape(cfg.configApPassword());
    html += F("</b></div>");
    html += F("<form method='post' action='/save'>");
    html += F("<label>WiFi SSID</label><input name='wifi_ssid' value='");
    html += htmlEscape(cfg.wifiSsid());
    html += F("'>");
    html += F("<label>WiFi Password</label><input name='wifi_password' value='");
    html += htmlEscape(cfg.wifiPassword());
    html += F("'>");
    html += F("<label>MQTT Server IP/Host</label><input name='mqtt_server' value='");
    html += htmlEscape(cfg.mqttServer());
    html += F("'>");
    html += F("<label>MQTT Port</label><input name='mqtt_port' value='");
    html += String(static_cast<unsigned int>(cfg.mqttPort()));
    html += F("'>");
    html += F("<label>MQTT Departures Topic</label><input name='mqtt_departures_topic' value='");
    html += htmlEscape(cfg.mqttTopicDepartures());
    html += F("'>");
    html += F("<label>MQTT Weather Topic</label><input name='mqtt_weather_topic' value='");
    html += htmlEscape(cfg.mqttTopicWeather());
    html += F("'>");
    html += F("<label>Timezone (POSIX TZ)</label><input name='timezone' value='");
    html += htmlEscape(cfg.timezone());
    html += F("'>");
    html += F("<button type='submit'>Save</button></form></div></body></html>");
    return html;
}

void sendPortalRedirect(WebServer& server)
{
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "Redirecting to portal");
}
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void Configuration::load()
{
    // TODO: try to read a JSON blob from NVS / LittleFS.
    // For now just copy the compile-time defaults.
    strlcpy(m_wifiSsid,               SETTINGS_WIFI_SSID,              sizeof(m_wifiSsid));
    strlcpy(m_wifiPassword,           SETTINGS_WIFI_PASSWORD,          sizeof(m_wifiPassword));
    strlcpy(m_mqttServer,             SETTINGS_MQTT_SERVER,            sizeof(m_mqttServer));
    m_mqttPort = SETTINGS_MQTT_PORT;
    strlcpy(m_mqttTopicDepartures,    SETTINGS_MQTT_TOPIC_DEPARTURES,  sizeof(m_mqttTopicDepartures));
    strlcpy(m_mqttTopicWeather,       SETTINGS_MQTT_TOPIC_WEATHER,     sizeof(m_mqttTopicWeather));
    // Timezone default is already set by the member initialiser; do not overwrite
    // unless a stored setting is available in the future.

    Serial.println("[CONFIG] Configuration loaded (compile-time defaults).");
    Serial.printf("[CONFIG] Loaded values: wifiSsid='%s' wifiPassword='%s' mqttServer='%s' mqttPort=%u depTopic='%s' weatherTopic='%s' tz='%s'\n",
                  m_wifiSsid,
                  m_wifiPassword,
                  m_mqttServer,
                  static_cast<unsigned int>(m_mqttPort),
                  m_mqttTopicDepartures,
                  m_mqttTopicWeather,
                  m_timezone);
}

void Configuration::save()
{
    // TODO: serialise to NVS / LittleFS.
    Serial.println("[CONFIG] Configuration save – not yet implemented.");
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
        m_webServer = new WebServer(80);
    }

    if (m_webServer == nullptr)
    {
        Serial.println("[CONFIG] Failed to allocate WebServer.");
        return false;
    }

    if (!m_webRoutesRegistered)
    {
        setupWebServerRoutes();
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
    if (!m_configModeActive || m_webServer == nullptr)
    {
        return;
    }

    if (m_dnsServer != nullptr)
    {
        m_dnsServer->processNextRequest();
    }

    m_webServer->handleClient();
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

    m_webServer->on("/", HTTP_GET, [this]() {
        m_webServer->send(200, "text/html", buildConfigPage(*this));
    });

    // Common captive portal probes across major clients.
    m_webServer->on("/generate_204", HTTP_GET, [this]() { sendPortalRedirect(*m_webServer); });
    m_webServer->on("/gen_204", HTTP_GET, [this]() { sendPortalRedirect(*m_webServer); });
    m_webServer->on("/hotspot-detect.html", HTTP_GET, [this]() { sendPortalRedirect(*m_webServer); });
    m_webServer->on("/ncsi.txt", HTTP_GET, [this]() { sendPortalRedirect(*m_webServer); });
    m_webServer->on("/connecttest.txt", HTTP_GET, [this]() { sendPortalRedirect(*m_webServer); });
    m_webServer->on("/redirect", HTTP_GET, [this]() { sendPortalRedirect(*m_webServer); });

    m_webServer->on("/save", HTTP_POST, [this]() {
        if (m_webServer->hasArg("wifi_ssid"))
        {
            setWifiSsid(m_webServer->arg("wifi_ssid").c_str());
        }
        if (m_webServer->hasArg("wifi_password"))
        {
            setWifiPassword(m_webServer->arg("wifi_password").c_str());
        }
        if (m_webServer->hasArg("mqtt_server"))
        {
            setMqttServer(m_webServer->arg("mqtt_server").c_str());
        }
        if (m_webServer->hasArg("mqtt_port"))
        {
            const long port = m_webServer->arg("mqtt_port").toInt();
            if (port > 0 && port <= 65535)
            {
                setMqttPort(static_cast<uint16_t>(port));
            }
        }
        if (m_webServer->hasArg("mqtt_departures_topic"))
        {
            setMqttTopicDepartures(m_webServer->arg("mqtt_departures_topic").c_str());
        }
        if (m_webServer->hasArg("mqtt_weather_topic"))
        {
            setMqttTopicWeather(m_webServer->arg("mqtt_weather_topic").c_str());
        }
        if (m_webServer->hasArg("timezone"))
        {
            setTimezone(m_webServer->arg("timezone").c_str());
        }

        save();
        renderConfigScreen();

        m_webServer->sendHeader("Location", "/", true);
        m_webServer->send(303, "text/plain", "Saved");
    });

    m_webServer->on("/api/settings", HTTP_GET, [this]() {
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
        json += "\"";
        json += "}";
        m_webServer->send(200, "application/json", json);
    });

    m_webServer->onNotFound([this]() {
        sendPortalRedirect(*m_webServer);
    });
}
