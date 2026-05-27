"""Fetch weather forecast from Open-Meteo and publish it to MQTT."""

import json
import time
from datetime import datetime, timezone

import paho.mqtt.client as mqtt
import requests

try:
    from settings import (
        CITY_COUNTRY_CODE,
        CITY_COUNTY,
        CITY_NAME,
        LATITUDE,
        LONGITUDE,
        MQTT_BROKER,
        MQTT_KEEPALIVE,
        MQTT_PORT,
        MQTT_WEATHER_TOPIC,
        USE_CITY_LOOKUP,
        WEATHER_REFRESH_SECONDS,
    )
except ImportError as exc:
    raise SystemExit(
        "Missing script/settings.py. Copy script/settings_example.py to script/settings.py and fill values."
    ) from exc

MQTT_TOPIC = MQTT_WEATHER_TOPIC
REFRESH_SECONDS = WEATHER_REFRESH_SECONDS

# Open-Meteo API endpoints
OPEN_METEO_FORECAST_URL = "https://api.open-meteo.com/v1/forecast"
OPEN_METEO_GEOCODING_URL = "https://geocoding-api.open-meteo.com/v1/search"

# Request timeout
HTTP_TIMEOUT_SECONDS = 10


def resolve_city_to_coordinates(city_name, county=None, country_code=None):
    """Resolve city name to coordinates using Open-Meteo geocoding API."""
    params = {
        "name": city_name,
        "count": 5,
        "language": "hu",
        "format": "json",
    }

    if country_code:
        params["countryCode"] = country_code

    response = requests.get(OPEN_METEO_GEOCODING_URL, params=params, timeout=HTTP_TIMEOUT_SECONDS)
    response.raise_for_status()
    data = response.json()

    results = data.get("results", [])
    if not results:
        raise ValueError(f"No geocoding result for city: {city_name}")

    if county:
        county_lower = county.lower()
        for item in results:
            admin1 = (item.get("admin1") or "").lower()
            if county_lower in admin1:
                return {
                    "name": item.get("name", city_name),
                    "admin1": item.get("admin1", ""),
                    "country": item.get("country", ""),
                    "latitude": item["latitude"],
                    "longitude": item["longitude"],
                    "timezone": item.get("timezone", "auto"),
                }

    first = results[0]
    return {
        "name": first.get("name", city_name),
        "admin1": first.get("admin1", ""),
        "country": first.get("country", ""),
        "latitude": first["latitude"],
        "longitude": first["longitude"],
        "timezone": first.get("timezone", "auto"),
    }


def get_forecast_json(latitude, longitude, timezone_name="auto"):
    """Request current and daily forecast from Open-Meteo."""
    params = {
        "latitude": latitude,
        "longitude": longitude,
        "timezone": timezone_name,
        "current": "temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m,wind_direction_10m",
        "daily": "weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,precipitation_probability_max,sunrise,sunset",
        "forecast_days": 4,
    }

    response = requests.get(OPEN_METEO_FORECAST_URL, params=params, timeout=HTTP_TIMEOUT_SECONDS)
    response.raise_for_status()
    return response.json()


def parse_forecast_payload(api_data, location_info):
    """Create compact, display-friendly MQTT payload."""
    current = api_data.get("current", {})
    daily = api_data.get("daily", {})

    daily_times = daily.get("time", [])
    daily_codes = daily.get("weather_code", [])
    daily_tmax = daily.get("temperature_2m_max", [])
    daily_tmin = daily.get("temperature_2m_min", [])
    daily_precip = daily.get("precipitation_sum", [])
    daily_pop = daily.get("precipitation_probability_max", [])

    days = []
    for idx in range(min(4, len(daily_times))):
        days.append(
            {
                "date": daily_times[idx],
                "weatherCode": daily_codes[idx] if idx < len(daily_codes) else None,
                "tempMaxC": daily_tmax[idx] if idx < len(daily_tmax) else None,
                "tempMinC": daily_tmin[idx] if idx < len(daily_tmin) else None,
                "precipMm": daily_precip[idx] if idx < len(daily_precip) else None,
                "precipProbMax": daily_pop[idx] if idx < len(daily_pop) else None,
            }
        )

    payload = {
        "source": "open-meteo",
        "publishedAtUtc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "location": {
            "name": location_info.get("name", ""),
            "admin1": location_info.get("admin1", ""),
            "country": location_info.get("country", ""),
            "latitude": location_info["latitude"],
            "longitude": location_info["longitude"],
            "timezone": api_data.get("timezone", location_info.get("timezone", "")),
        },
        "current": {
            "time": current.get("time"),
            "temperatureC": current.get("temperature_2m"),
            "apparentTemperatureC": current.get("apparent_temperature"),
            "relativeHumidity": current.get("relative_humidity_2m"),
            "weatherCode": current.get("weather_code"),
            "windSpeedKmh": current.get("wind_speed_10m"),
            "windDirectionDeg": current.get("wind_direction_10m"),
            "isDay": current.get("is_day"),
        },
        "daily": days,
    }

    return payload


def print_forecast_debug(payload):
    """Print short weather status in console for debugging."""
    location = payload.get("location", {})
    current = payload.get("current", {})
    daily = payload.get("daily", [])

    print("\nWeather update")
    print(f"Location: {location.get('name', '')}, {location.get('admin1', '')} ({location.get('country', '')})")
    print(
        "Current: "
        f"{current.get('temperatureC')} C, "
        f"feels {current.get('apparentTemperatureC')} C, "
        f"RH {current.get('relativeHumidity')}%, "
        f"wind {current.get('windSpeedKmh')} km/h"
    )

    if daily:
        today = daily[0]
        print(
            "Today: "
            f"min {today.get('tempMinC')} C, "
            f"max {today.get('tempMaxC')} C, "
            f"precip {today.get('precipMm')} mm"
        )


def get_location_info():
    """Resolve configuration to a location info object."""
    if USE_CITY_LOOKUP:
        return resolve_city_to_coordinates(CITY_NAME, CITY_COUNTY, CITY_COUNTRY_CODE)

    return {
        "name": "Configured coordinates",
        "admin1": "",
        "country": CITY_COUNTRY_CODE,
        "latitude": LATITUDE,
        "longitude": LONGITUDE,
        "timezone": "auto",
    }


def run_once(client, location_info, last_payload):
    """Fetch, parse, and publish weather payload once."""
    api_data = get_forecast_json(
        location_info["latitude"],
        location_info["longitude"],
        location_info.get("timezone", "auto"),
    )

    payload_obj = parse_forecast_payload(api_data, location_info)
    payload_json = json.dumps(payload_obj, ensure_ascii=False)

    print_forecast_debug(payload_obj)

    if payload_json != last_payload:
        print("Publishing weather payload...")
        client.publish(MQTT_TOPIC, payload_json, retain=True)
        print("Published to weather/forecast")
        return payload_json

    print("No weather change; publish skipped.")
    return last_payload


def main():
    location_info = get_location_info()

    print(
        "Starting weather grabber for "
        f"{location_info.get('name', '')} "
        f"({location_info['latitude']}, {location_info['longitude']})"
    )

    client = mqtt.Client()
    client.connect(MQTT_BROKER, MQTT_PORT, MQTT_KEEPALIVE)

    last_payload = None

    while True:
        try:
            last_payload = run_once(client, location_info, last_payload)
        except Exception as exc:
            print("Error:", exc)

        time.sleep(REFRESH_SECONDS)


if __name__ == "__main__":
    main()
