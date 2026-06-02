#include "openmeteo_weather_provider.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <cstring>

namespace
{
bool looksLikePosixTz(const char* tz)
{
    if (tz == nullptr || tz[0] == '\0')
    {
        return false;
    }

    // POSIX TZ strings typically include commas and DST rules, e.g. CET-1CEST,M3.5.0,M10.5.0/3
    return strchr(tz, ',') != nullptr || strchr(tz, '/') != nullptr;
}

String urlEncodeSimple(const char* input)
{
    if (input == nullptr)
    {
        return String();
    }

    const char* hex = "0123456789ABCDEF";
    String out;
    out.reserve(strlen(input) * 3);

    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(input); *p != '\0'; ++p)
    {
        const unsigned char c = *p;
        const bool unreserved = (c >= 'A' && c <= 'Z')
                             || (c >= 'a' && c <= 'z')
                             || (c >= '0' && c <= '9')
                             || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved)
        {
            out += static_cast<char>(c);
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
}

OpenMeteoWeatherProvider::OpenMeteoWeatherProvider(float latitude, float longitude, const char* timezone)
    : m_latitude(latitude), m_longitude(longitude)
{
    if (timezone != nullptr)
    {
        strlcpy(m_timezone, timezone, sizeof(m_timezone));
    }
    else
    {
        strlcpy(m_timezone, "auto", sizeof(m_timezone));
    }
}

bool OpenMeteoWeatherProvider::fetchWeather(WeatherData& outData)
{
    HTTPClient http;

    const char* tzForApi = m_timezone;
    if (looksLikePosixTz(m_timezone))
    {
        tzForApi = "auto";
        Serial.printf("[WEATHER] POSIX timezone '%s' detected, using Open-Meteo timezone='auto'\n", m_timezone);
    }

    const String encodedTz = urlEncodeSimple(tzForApi);

    // Build Open-Meteo API URL
    String url = "https://api.open-meteo.com/v1/forecast?";
    url += "latitude=";
    url += m_latitude;
    url += "&longitude=";
    url += m_longitude;
    url += "&timezone=";
    url += encodedTz;
    url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m,wind_direction_10m";
    url += "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,precipitation_probability_max";
    url += "&forecast_days=4";

    Serial.printf("[WEATHER] OpenMeteo request: lat=%.4f lon=%.4f tz='%s' (api='%s')\n",
                  m_latitude,
                  m_longitude,
                  m_timezone,
                  tzForApi);
    Serial.printf("[WEATHER] OpenMeteo fetch from: %s\n", url.c_str());

    if (!http.begin(url))
    {
        Serial.println("[WEATHER] OpenMeteo HTTP begin failed");
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != 200)
    {
        String errorPayload = http.getString();
        Serial.printf("[WEATHER] OpenMeteo HTTP error: %d, body(120)='%.120s'\n",
                      httpCode,
                      errorPayload.c_str());
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    Serial.printf("[WEATHER] OpenMeteo response (%d bytes): %.100s...\n", payload.length(), payload.c_str());

    return parseOpenMeteoResponse(payload, outData);
}

bool OpenMeteoWeatherProvider::parseOpenMeteoResponse(const String& jsonStr, WeatherData& outData)
{
    // Use ArduinoJson to parse the response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error)
    {
        Serial.printf("[WEATHER] JSON parse error: %s\n", error.c_str());
        return false;
    }

    // Clear output struct
    memset(&outData, 0, sizeof(outData));

    // Set source and published time
    strlcpy(outData.source, "open-meteo", sizeof(outData.source));
    time_t now = time(nullptr);
    struct tm* timeinfo = gmtime(&now);
    strftime(outData.publishedAtUtc, sizeof(outData.publishedAtUtc), "%Y-%m-%dT%H:%M:%SZ", timeinfo);

    // Parse location (we don't have explicit location data from this API, so use coordinates)
    strlcpy(outData.locationName, "Location", sizeof(outData.locationName));
    outData.latitude = doc["latitude"] | m_latitude;
    outData.longitude = doc["longitude"] | m_longitude;
    strlcpy(outData.timezone, doc["timezone"].as<const char*>() ? doc["timezone"].as<const char*>() : m_timezone, sizeof(outData.timezone));

    // Parse current weather
    JsonObject current = doc["current"];
    if (current)
    {
        // Current time
        const char* currentTime = current["time"];
        if (currentTime)
        {
            strlcpy(outData.currentTime, currentTime, sizeof(outData.currentTime));
        }

        outData.temperatureC = current["temperature_2m"] | 0.0f;
        outData.apparentTemperatureC = current["apparent_temperature"] | 0.0f;
        outData.relativeHumidity = current["relative_humidity_2m"] | 0;
        outData.weatherCode = current["weather_code"] | 0;
        outData.windSpeedKmh = current["wind_speed_10m"] | 0.0f;
        outData.windDirectionDeg = current["wind_direction_10m"] | 0;
        outData.isDay = current["is_day"] | 1;

        Serial.printf("[WEATHER] Current: T=%.1f°C, RH=%d%%, WS=%.1f km/h, WC=%d\n",
                      outData.temperatureC,
                      outData.relativeHumidity,
                      outData.windSpeedKmh,
                      outData.weatherCode);
    }

    // Parse daily forecast (up to 4 days)
    JsonArray dailyTimes = doc["daily"]["time"];
    JsonArray dailyWeatherCodes = doc["daily"]["weather_code"];
    JsonArray dailyTempMax = doc["daily"]["temperature_2m_max"];
    JsonArray dailyTempMin = doc["daily"]["temperature_2m_min"];
    JsonArray dailyPrecip = doc["daily"]["precipitation_sum"];
    JsonArray dailyPrecipProb = doc["daily"]["precipitation_probability_max"];

    if (dailyTimes)
    {
        int dailyCount = 0;
        for (int i = 0; i < dailyTimes.size() && i < MAX_WEATHER_DAYS; ++i)
        {
            const char* dateStr = dailyTimes[i].as<const char*>();
            if (!dateStr)
                continue;

            strlcpy(outData.daily[i].date, dateStr, sizeof(outData.daily[i].date));
            outData.daily[i].weatherCode = dailyWeatherCodes[i] | 0;
            outData.daily[i].tempMaxC = dailyTempMax[i] | 0.0f;
            outData.daily[i].tempMinC = dailyTempMin[i] | 0.0f;
            outData.daily[i].precipMm = dailyPrecip[i] | 0.0f;
            outData.daily[i].precipProbMax = dailyPrecipProb[i] | 0;

            Serial.printf("[WEATHER] Daily[%d]: %s, T=%.1f-%.1f°C, P=%.1fmm, ProbMax=%d%%\n",
                          i,
                          dateStr,
                          outData.daily[i].tempMinC,
                          outData.daily[i].tempMaxC,
                          outData.daily[i].precipMm,
                          outData.daily[i].precipProbMax);

            ++dailyCount;
        }
        outData.dailyCount = dailyCount;
    }

    Serial.printf("[WEATHER] OpenMeteo parsing success: %d daily entries\n", outData.dailyCount);
    return true;
}
