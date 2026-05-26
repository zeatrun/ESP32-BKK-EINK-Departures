#include "configuration.h"
#include "display_manager.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

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
    html.reserve(9000);
    html += F("<!doctype html><html><head><meta charset='utf-8'>");
    html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>ESP32 Config</title>");
    html += F("<style>body{font-family:Verdana,sans-serif;margin:16px;background:#f8fafc;color:#111827;}"
              "h1{font-size:1.2rem;margin:0 0 10px 0;}label{display:block;margin:10px 0 4px;font-weight:600;}"
              "input,select{width:100%;padding:10px;border:1px solid #cbd5e1;border-radius:8px;}"
              ".hint{font-size:.82rem;color:#475569;margin-top:3px;}"
              "button{margin-top:14px;padding:10px 14px;border:0;border-radius:8px;background:#0f766e;color:#fff;font-weight:700;}"
              "button.secondary{background:#b91c1c;}"
              ".actions{display:flex;gap:10px;flex-wrap:wrap;}"
              ".card{max-width:720px;margin:0 auto;background:#fff;padding:16px;border-radius:12px;box-shadow:0 2px 10px rgba(0,0,0,0.08);position:relative;}"
              ".meta{font-size:.9rem;color:#334155;margin-bottom:10px;}"
              ".lang-switch{position:absolute;top:12px;right:12px;display:flex;align-items:center;gap:6px;}"
              ".lang-switch button{margin-top:0;padding:4px 8px;font-size:.8rem;background:#e2e8f0;color:#0f172a;}"
              ".lang-switch button.active{background:#0f766e;color:#fff;}</style></head><body>");
    html += F("<div class='card'>");
    html += F("<div class='lang-switch'><button type='button' id='lang-en'>EN</button><span>|</span><button type='button' id='lang-hu'>HU</button></div>");
    html += F("<h1 id='title-main'>ESP32 Configuration</h1>");
    html += F("<div class='meta'><span id='label-ap-ssid'>AP SSID:</span> <b>");
    html += htmlEscape(cfg.configApSsid());
    html += F("</b><br><span id='label-ap-password'>AP Password:</span> <b>");
    html += htmlEscape(cfg.configApPassword());
    html += F("</b></div>");
    html += F("<form method='post' action='/save'>");
    html += F("<input type='hidden' id='ui_lang_save' name='ui_lang' value='en'>");
    html += F("<label id='label-wifi-ssid' for='wifi_ssid'>WiFi SSID</label><input id='wifi_ssid' name='wifi_ssid' value='");
    html += htmlEscape(cfg.wifiSsid());
    html += F("' required minlength='1' maxlength='63' autocomplete='off'>");
    html += F("<div class='hint' id='hint-wifi-ssid'>1-63 characters</div>");
    html += F("<label id='label-wifi-password' for='wifi_password'>WiFi Password</label><input id='wifi_password' type='password' name='wifi_password' value='");
    html += htmlEscape(cfg.wifiPassword());
    html += F("' required minlength='8' maxlength='63' autocomplete='off'>");
    html += F("<div class='hint' id='hint-wifi-password'>8-63 characters (WPA/WPA2)</div>");
    html += F("<label id='label-mqtt-server' for='mqtt_server'>MQTT Server IP/Host</label><input id='mqtt_server' name='mqtt_server' value='");
    html += htmlEscape(cfg.mqttServer());
    html += F("' required maxlength='63' pattern='[A-Za-z0-9.-]+' autocomplete='off'>");
    html += F("<div class='hint' id='hint-mqtt-server'>IPv4 or hostname</div>");
    html += F("<label id='label-mqtt-port' for='mqtt_port'>MQTT Port</label><input id='mqtt_port' type='number' name='mqtt_port' value='");
    html += String(static_cast<unsigned int>(cfg.mqttPort()));
    html += F("' min='1' max='65535' required>");
    html += F("<label id='label-mqtt-departures-topic' for='mqtt_departures_topic'>MQTT Departures Topic</label><input id='mqtt_departures_topic' name='mqtt_departures_topic' value='");
    html += htmlEscape(cfg.mqttTopicDepartures());
    html += F("' required maxlength='127' autocomplete='off'>");
    html += F("<label id='label-mqtt-weather-topic' for='mqtt_weather_topic'>MQTT Weather Topic</label><input id='mqtt_weather_topic' name='mqtt_weather_topic' value='");
    html += htmlEscape(cfg.mqttTopicWeather());
    html += F("' required maxlength='127' autocomplete='off'>");
    html += F("<label id='label-timezone' for='timezone'>Timezone</label>");
    html += F("<select id='timezone' name='timezone' required>");
    for (const auto& option : TIMEZONE_OPTIONS)
    {
        html += F("<option value='");
        html += htmlEscape(option.value);
        html += F("' data-en='");
        html += htmlEscape(option.labelEn);
        html += F("' data-hu='");
        html += htmlEscape(option.labelHu);
        html += F("'");
        if (strcmp(option.value, cfg.timezone()) == 0)
        {
            html += F(" selected");
        }
        html += F("></option>");
    }
    html += F("</select>");
    html += F("<div class='actions'>");
    html += F("<button type='submit' id='btn-save'>Save</button>");
    html += F("</div></form>");
    html += F("<form method='post' action='/reboot' id='reboot-form'>");
    html += F("<input type='hidden' id='ui_lang_reboot' name='ui_lang' value='en'>");
    html += F("<div class='actions'><button class='secondary' type='submit' id='btn-reboot'>Reboot ESP</button></div>");
    html += F("</form>");
    html += F("</div>");
    html += F("<script>(function(){"
              "const dict={"
              "en:{title:'ESP32 Configuration',apSsid:'AP SSID:',apPassword:'AP Password:',wifiSsid:'WiFi SSID',wifiSsidHint:'1-63 characters',wifiPassword:'WiFi Password',wifiPasswordHint:'8-63 characters (WPA/WPA2)',mqttServer:'MQTT Server IP/Host',mqttServerHint:'IPv4 or hostname',mqttPort:'MQTT Port',mqttDep:'MQTT Departures Topic',mqttWeather:'MQTT Weather Topic',timezone:'Timezone',save:'Save',reboot:'Reboot ESP',confirmReboot:'Restart ESP now?'},"
              "hu:{title:'ESP32 Beállítás',apSsid:'AP SSID:',apPassword:'AP jelszó:',wifiSsid:'WiFi SSID',wifiSsidHint:'1-63 karakter',wifiPassword:'WiFi jelszó',wifiPasswordHint:'8-63 karakter (WPA/WPA2)',mqttServer:'MQTT szerver IP/host',mqttServerHint:'IPv4 cím vagy hostnév',mqttPort:'MQTT port',mqttDep:'MQTT indulások téma',mqttWeather:'MQTT időjárás téma',timezone:'Időzóna',save:'Mentés',reboot:'ESP újraindítása',confirmReboot:'Biztosan újraindítod az ESP-t?'}"
              "};"
              "const nodes={title:document.getElementById('title-main'),apSsid:document.getElementById('label-ap-ssid'),apPassword:document.getElementById('label-ap-password'),wifiSsid:document.getElementById('label-wifi-ssid'),wifiSsidHint:document.getElementById('hint-wifi-ssid'),wifiPassword:document.getElementById('label-wifi-password'),wifiPasswordHint:document.getElementById('hint-wifi-password'),mqttServer:document.getElementById('label-mqtt-server'),mqttServerHint:document.getElementById('hint-mqtt-server'),mqttPort:document.getElementById('label-mqtt-port'),mqttDep:document.getElementById('label-mqtt-departures-topic'),mqttWeather:document.getElementById('label-mqtt-weather-topic'),timezone:document.getElementById('label-timezone'),save:document.getElementById('btn-save'),reboot:document.getElementById('btn-reboot')};"
              "const enBtn=document.getElementById('lang-en');const huBtn=document.getElementById('lang-hu');const rebootForm=document.getElementById('reboot-form');const langSave=document.getElementById('ui_lang_save');const langReboot=document.getElementById('ui_lang_reboot');"
              "function setLang(lang){const t=dict[lang]||dict.en;Object.keys(nodes).forEach(k=>{if(nodes[k])nodes[k].textContent=t[k];});const tzSel=document.getElementById('timezone');if(tzSel){Array.from(tzSel.options).forEach(o=>{const key=(lang==='hu'?'hu':'en');o.textContent=o.dataset[key]||o.value;});}if(rebootForm)rebootForm.dataset.confirm=t.confirmReboot;if(langSave)langSave.value=lang;if(langReboot)langReboot.value=lang;enBtn.classList.toggle('active',lang==='en');huBtn.classList.toggle('active',lang==='hu');try{localStorage.setItem('cfg_lang',lang);}catch(e){}}"
              "if(enBtn)enBtn.addEventListener('click',()=>setLang('en'));if(huBtn)huBtn.addEventListener('click',()=>setLang('hu'));"
              "if(rebootForm)rebootForm.addEventListener('submit',function(ev){const msg=this.dataset.confirm||'Restart ESP now?';if(!confirm(msg)){ev.preventDefault();}});"
              "let initial='en';try{initial=localStorage.getItem('cfg_lang')||'';}catch(e){}if(initial!=='en'&&initial!=='hu'){const n=(navigator.language||'en').toLowerCase();initial=n.startsWith('hu')?'hu':'en';}setLang(initial);"
              "})();</script>");
    html += F("</body></html>");
    return html;
}

void sendValidationError(WebServer& server, const char* message)
{
    String html;
    html.reserve(600);
    html += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>Validation error</title></head><body style='font-family:Verdana,sans-serif;margin:16px'>");
    html += F("<h2>Validation error</h2><p>");
    html += htmlEscape(message);
    html += F("</p><p><a href='/'>Back to configuration</a></p></body></html>");
    server.send(400, "text/html", html);
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
    prefs.end();

    strlcpy(m_wifiSsid, wifiSsid.c_str(), sizeof(m_wifiSsid));
    strlcpy(m_wifiPassword, wifiPassword.c_str(), sizeof(m_wifiPassword));
    strlcpy(m_mqttServer, mqttServer.c_str(), sizeof(m_mqttServer));
    m_mqttPort = mqttPort;
    strlcpy(m_mqttTopicDepartures, depTopic.c_str(), sizeof(m_mqttTopicDepartures));
    strlcpy(m_mqttTopicWeather, weatherTopic.c_str(), sizeof(m_mqttTopicWeather));
    strlcpy(m_timezone, timezone.c_str(), sizeof(m_timezone));

    Serial.println("[CONFIG] Configuration loaded from NVS.");
    Serial.printf("[CONFIG] Loaded values: wifiSsid='%s' wifiPassword='%s' mqttServer='%s' mqttPort=%u depTopic='%s' weatherTopic='%s' tz='%s'\n",
                  m_wifiSsid,
                  m_wifiPassword,
                  m_mqttServer,
                  static_cast<unsigned int>(m_mqttPort),
                  m_mqttTopicDepartures,
                  m_mqttTopicWeather,
                  m_timezone);
}

void Configuration::loadDefaults()
{
    strlcpy(m_wifiSsid,               SETTINGS_WIFI_SSID,              sizeof(m_wifiSsid));
    strlcpy(m_wifiPassword,           SETTINGS_WIFI_PASSWORD,          sizeof(m_wifiPassword));
    strlcpy(m_mqttServer,             SETTINGS_MQTT_SERVER,            sizeof(m_mqttServer));
    m_mqttPort = SETTINGS_MQTT_PORT;
    strlcpy(m_mqttTopicDepartures,    SETTINGS_MQTT_TOPIC_DEPARTURES,  sizeof(m_mqttTopicDepartures));
    strlcpy(m_mqttTopicWeather,       SETTINGS_MQTT_TOPIC_WEATHER,     sizeof(m_mqttTopicWeather));
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
    prefs.putBool("has", true);
    prefs.end();

    Serial.println("[CONFIG] Configuration saved to NVS.");
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
        if (!(m_webServer->hasArg("wifi_ssid") &&
              m_webServer->hasArg("wifi_password") &&
              m_webServer->hasArg("mqtt_server") &&
              m_webServer->hasArg("mqtt_port") &&
              m_webServer->hasArg("mqtt_departures_topic") &&
              m_webServer->hasArg("mqtt_weather_topic") &&
              m_webServer->hasArg("timezone")))
        {
            sendValidationError(*m_webServer, "Missing required fields.");
            return;
        }

        const String wifiSsid = trimCopy(m_webServer->arg("wifi_ssid"));
        const String wifiPassword = trimCopy(m_webServer->arg("wifi_password"));
        const String mqttServer = trimCopy(m_webServer->arg("mqtt_server"));
        const String mqttPortText = trimCopy(m_webServer->arg("mqtt_port"));
        const String depTopic = trimCopy(m_webServer->arg("mqtt_departures_topic"));
        const String weatherTopic = trimCopy(m_webServer->arg("mqtt_weather_topic"));
        const String timezone = trimCopy(m_webServer->arg("timezone"));

        if (wifiSsid.isEmpty() || wifiSsid.length() > (sizeof(m_wifiSsid) - 1) || !isAsciiPrintableNoCtrl(wifiSsid))
        {
            sendValidationError(*m_webServer, "Invalid WiFi SSID (1-63 printable ASCII chars).");
            return;
        }

        if (wifiPassword.length() < 8 || wifiPassword.length() > (sizeof(m_wifiPassword) - 1) || !isAsciiPrintableNoCtrl(wifiPassword))
        {
            sendValidationError(*m_webServer, "Invalid WiFi password (8-63 printable ASCII chars).");
            return;
        }

        if (!isValidHostOrIpv4(mqttServer))
        {
            sendValidationError(*m_webServer, "Invalid MQTT server host/IP.");
            return;
        }

        const long port = mqttPortText.toInt();
        if (port <= 0 || port > 65535)
        {
            sendValidationError(*m_webServer, "Invalid MQTT port (1-65535).");
            return;
        }

        if (!isValidMqttTopic(depTopic))
        {
            sendValidationError(*m_webServer, "Invalid departures MQTT topic.");
            return;
        }

        if (!isValidMqttTopic(weatherTopic))
        {
            sendValidationError(*m_webServer, "Invalid weather MQTT topic.");
            return;
        }

        if (!isAllowedTimezoneValue(timezone))
        {
            sendValidationError(*m_webServer, "Invalid timezone selection.");
            return;
        }

        setWifiSsid(wifiSsid.c_str());
        setWifiPassword(wifiPassword.c_str());
        setMqttServer(mqttServer.c_str());
        setMqttPort(static_cast<uint16_t>(port));
        setMqttTopicDepartures(depTopic.c_str());
        setMqttTopicWeather(weatherTopic.c_str());
        setTimezone(timezone.c_str());

        save();
        renderConfigScreen();

        m_webServer->sendHeader("Location", "/", true);
        m_webServer->send(303, "text/plain", "Saved");
    });

    m_webServer->on("/reboot", HTTP_POST, [this]() {
        const String uiLang = m_webServer->hasArg("ui_lang") ? m_webServer->arg("ui_lang") : "en";
        const bool isHu = uiLang.equalsIgnoreCase("hu");

        if (isHu)
        {
            m_webServer->send(200,
                              "text/html",
                              "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Ujrainditas</title></head>"
                              "<body style='font-family:Verdana,sans-serif;margin:16px'><h2>ESP ujrainditas...</h2><p>Par masodperc mulva toltsd ujra az oldalt.</p></body></html>");
        }
        else
        {
            m_webServer->send(200,
                              "text/html",
                              "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Reboot</title></head>"
                              "<body style='font-family:Verdana,sans-serif;margin:16px'><h2>ESP rebooting...</h2><p>Reload this page in a few seconds.</p></body></html>");
        }

        scheduleReboot(1200);
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
