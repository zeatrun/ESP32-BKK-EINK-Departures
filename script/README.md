# Script Folder

This folder contains Python helper scripts that work alongside the firmware. They do not run on the ESP32 itself; instead, they run on a separate machine and publish data to the display over MQTT.

## Contents

- `bkk_grabber.py`: fetches BKK departure data and publishes it to the `MQTT_BKK_TOPIC` topic.
- `weather_grabber.py`: fetches Open-Meteo forecast data and publishes it to the `MQTT_WEATHER_TOPIC` topic.
- `settings_example.py`: template file for local configuration.
- `settings.py`: your private local configuration. This file is not tracked by git.

## Prerequisites

- Python 3.10 or newer
- an accessible MQTT broker
- internet access for API calls

Install the required Python packages:

```bash
pip install requests paho-mqtt
```

## Configuration

1. Copy `settings_example.py` to `settings.py`.
2. Fill in your real values.

Windows PowerShell example:

```powershell
Copy-Item .\settings_example.py .\settings.py
```

Important settings in `settings.py`:

- `MQTT_BROKER`, `MQTT_PORT`, `MQTT_KEEPALIVE`: MQTT connection settings.
- `MQTT_BKK_TOPIC`: topic used for transport departure data.
- `MQTT_WEATHER_TOPIC`: topic used for weather data.
- `BKK_API_KEY`: BKK API key.
- `STOP_ID_MAV`, `STOP_ID_VOLAN`: monitored stop identifiers.
- `USE_CITY_LOOKUP`: if `True`, the weather script resolves coordinates from city data.
- `CITY_NAME`, `CITY_COUNTY`, `CITY_COUNTRY_CODE`: city-based location lookup values.
- `LATITUDE`, `LONGITUDE`: direct coordinates if city lookup is disabled.
- `WEATHER_REFRESH_SECONDS`: weather refresh interval in seconds.

## Running

Open a terminal in this folder and run the script you need.

Transport departures:

```bash
python bkk_grabber.py
```

Weather forecast:

```bash
python weather_grabber.py
```

## How They Work

### `bkk_grabber.py`

- requests departures for the two configured stops from the BKK API
- merges the results
- publishes them as a JSON payload over MQTT
- repeats every 60 seconds
- only republishes when the payload changes

### `weather_grabber.py`

- optionally resolves coordinates from a city name using the Open-Meteo geocoding API
- requests current weather and a multi-day forecast
- builds a compact JSON payload
- repeats according to `WEATHER_REFRESH_SECONDS`
- only republishes when the payload changes

## Typical Usage

If the firmware is running in MQTT mode, you can start one or both scripts on a separate machine. The display then receives the data from the MQTT broker.

## Troubleshooting

- If you see `Missing script/settings.py`, the local configuration file is missing.
- If nothing is published, check the MQTT broker address and topic names.
- If BKK requests fail, verify the API key and stop IDs.
- If weather requests fail, verify the city name or coordinates.
- Both scripts print current status and raised exceptions to the console.