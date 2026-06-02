#pragma once

#include "weather_provider.h"
#include <WiFi.h>

/**
 * @brief Open-Meteo weather data provider.
 *
 * Fetches weather data from the Open-Meteo API (https://open-meteo.com/).
 * No API key required, but respects rate limits.
 *
 * Supports:
 *  - Current weather (temperature, humidity, wind, weather code, etc.)
 *  - Daily forecast (up to 4 days)
 *  - Automatic timezone detection
 */
class OpenMeteoWeatherProvider : public WeatherProvider
{
public:
    /**
     * Initialize the provider with location coordinates and optional timezone.
     *
     * @param latitude      Location latitude (-90 to 90)
     * @param longitude     Location longitude (-180 to 180)
     * @param timezone      POSIX timezone string (e.g., "auto" or "Europe/Budapest")
     */
    OpenMeteoWeatherProvider(float latitude, float longitude, const char* timezone = "auto");

    /**
     * Fetch weather data from Open-Meteo API.
     */
    bool fetchWeather(WeatherData& outData) override;

    const char* providerName() const override { return "Open-Meteo"; }

private:
    float m_latitude;
    float m_longitude;
    char m_timezone[40];

    /**
     * Parse the Open-Meteo JSON response into WeatherData struct.
     */
    bool parseOpenMeteoResponse(const String& jsonStr, WeatherData& outData);
};
