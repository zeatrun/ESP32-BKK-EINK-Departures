#pragma once

#include "weather.h"

/**
 * @brief Abstract interface for weather data providers.
 *
 * Different weather APIs (Open-Meteo, OpenWeatherMap, etc.) implement this
 * interface to provide a uniform way of fetching and parsing weather data
 * into the common WeatherData struct.
 */
class WeatherProvider
{
public:
    virtual ~WeatherProvider() = default;

    /**
     * Fetch current and forecast weather data and populate the output struct.
     *
     * @param[out] outData  WeatherData struct to fill with fetched data.
     * @return true if fetch and parse was successful, false otherwise.
     */
    virtual bool fetchWeather(WeatherData& outData) = 0;

    /**
     * Human-readable provider name (e.g., "Open-Meteo", "OpenWeatherMap").
     */
    virtual const char* providerName() const = 0;
};
