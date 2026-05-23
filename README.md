# ESP32 BKK E-Ink Departures Board

ESP32 firmware for a 7.3 inch color e-paper board that shows:
- public transport departures over MQTT
- weather forecast over MQTT
- connection/status information

The project is built with PlatformIO and runs on Seeed XIAO ESP32S3.

## What This Project Does

The board acts as an always-on information display:
- connects to Wi-Fi
- subscribes to MQTT topics for departures and weather
- stores and compares incoming payloads
- refreshes the display only when needed (plus periodic policy in display task)

It is designed for low-noise updates on e-paper and for robust operation with reconnecting Wi-Fi/MQTT tasks.

## Hardware and Stack

- MCU: Seeed XIAO ESP32S3
- Display: 7.3 inch Seeed e-paper board (EE04 combo config)
- Framework: Arduino (on ESP-IDF/FreeRTOS runtime)
- Build system: PlatformIO
- MQTT client: PubSubClient
- JSON parser: ArduinoJson
- Graphics driver: Seeed_GFX (local library in lib/Seeed_GFX-master)

## Project Structure

- [src/main.cpp](src/main.cpp): startup, task initialization order
- [src/wifi_manager.cpp](src/wifi_manager.cpp): Wi-Fi connect/reconnect task
- [src/mqtt_manager.cpp](src/mqtt_manager.cpp): MQTT connect/subscribe, payload parsing, shared data update
- [src/display_manager.cpp](src/display_manager.cpp): rendering, layout, refresh logic
- [src/time_manager.cpp](src/time_manager.cpp): time sync / time services
- [src/departures.cpp](src/departures.cpp): global departures storage
- [src/weather.cpp](src/weather.cpp): global weather storage
- [include/settings_example.h](include/settings_example.h): configuration template
- [script/bkk_grabber.py](script/bkk_grabber.py): helper publisher for Hungarian public transport departures
- [script/weather_grabber.py](script/weather_grabber.py): helper publisher for Open-Meteo weather
- [gen](gen): generated image headers (included via compiler include path)

## Configuration

1. Create a local settings header:
- copy [include/settings_example.h](include/settings_example.h) to [include/settings.h](include/settings.h)
- set your real values for:
	- Wi-Fi SSID/password
	- MQTT server and port
	- departures topic
	- weather topic

2. Verify serial/upload ports in [platformio.ini](platformio.ini):
- upload_port
- monitor_port

## Build and Flash

From project root:

1. Build
	 - platformio run
2. Upload
	 - platformio run --target upload
3. Serial monitor
	 - platformio device monitor

Or use the predefined PlatformIO tasks in VS Code.

## MQTT Topics and Payloads

Configured in [include/mqtt_manager.h](include/mqtt_manager.h) through settings macros.

### Departures Topic

Expected JSON payload: array of objects, for example:

```json
[
	{
		"line": "931",
		"routeIdText": "Budapest - Pilisszentiván",
		"destination": "Szell Kalman ter",
		"stopName": "Pilisszentivan, Kossuth Lajos utca",
		"minutes": 7,
		"timestamp": 1716460200
	}
]
```

### Weather Topic

Expected JSON payload: object with location/current/daily blocks, for example:

```json
{
	"source": "open-meteo",
	"publishedAtUtc": "2026-05-23T10:20:00Z",
	"location": {
		"name": "Pilisszentivan",
		"admin1": "Pest",
		"country": "Hungary",
		"latitude": 47.613,
		"longitude": 18.908,
		"timezone": "Europe/Budapest"
	},
	"current": {
		"time": "2026-05-23T12:00",
		"temperatureC": 22.1,
		"apparentTemperatureC": 22.6,
		"relativeHumidity": 51,
		"weatherCode": 2,
		"windSpeedKmh": 15.2,
		"windDirectionDeg": 247,
		"isDay": 1
	},
	"daily": [
		{
			"date": "2026-05-23",
			"weatherCode": 2,
			"tempMaxC": 24.3,
			"tempMinC": 13.8,
			"precipMm": 0.0,
			"precipProbMax": 15
		}
	]
}
```

## Display Palette

Defined in [include/display_manager.h](include/display_manager.h):
- EINK_BLACK = 0xF
- EINK_WHITE = 0x0
- EINK_BLUE = 0xD
- EINK_YELLOW = 0xB
- EINK_GREEN = 0x2
- EINK_RED = 0x6

## Helper Data Publisher Scripts

### BKK publisher

[script/bkk_grabber.py](script/bkk_grabber.py) fetches departures from BKK API and publishes to MQTT.

Notes:
- contains API key and broker values as in-file constants
- publishes retained payloads

### Weather publisher

[script/weather_grabber.py](script/weather_grabber.py) fetches Open-Meteo weather and publishes to MQTT.

Notes:
- supports city lookup mode and fixed coordinate mode
- publishes retained payloads
- refresh interval is configurable in script constants

## Generated Graphics Headers

The build is configured to include headers from [gen](gen) using -Igen in [platformio.ini](platformio.ini).

This allows direct includes like:

```cpp
#include "PartlyCloudy_WhiteBG_64x48.h"
```

## Custom Fonts (UTF-8 Hungarian)

The display layer supports UTF-8 Hungarian text with custom Noto Sans smooth fonts loaded from SPIFFS.

### Supported font files

Place these files in [data](data) and upload filesystem image:
- NotoSansHU12.vlw
- NotoSansHU16.vlw
- NotoSansHU24.vlw
- NotoSansHU32.vlw

If some sizes are missing, the firmware uses fallback order:
- 16 -> 24 -> 12 -> 32

### Generate `.vlw` fonts

Use the included Processing tool:
- [lib/Seeed_GFX-master/Tools/Create_Smooth_Font/Create_font/Create_font.pde](lib/Seeed_GFX-master/Tools/Create_Smooth_Font/Create_font/Create_font.pde)

Recommended settings for this project:
- font family: Noto Sans
- sizes: 12, 16, 24, 32 (generate one file per size)
- unicode blocks: Basic Latin, Latin-1 Supplement, Latin Extended-A
- anti-aliasing: disabled (`smooth = false`) for cleaner single-color glyph edges on e-ink

### Upload to device

Filesystem type is configured as SPIFFS in [platformio.ini](platformio.ini).

From project root:

1. Upload font files to SPIFFS
	- `platformio run -t uploadfs`
2. Upload firmware
	- `platformio run -t upload`

### Runtime behavior

- If no Noto font files are found in SPIFFS, the firmware falls back to built-in fonts.
- UTF-8 text rendering is enabled in display initialization.

## Troubleshooting

- No MQTT updates on display:
	- verify broker IP/port and topic names in settings
	- check serial monitor for MQTT parse/connect logs
- Board does not connect to Wi-Fi:
	- verify SSID/password
	- ensure 2.4 GHz network availability
- Build errors after config changes:
	- check that [include/settings.h](include/settings.h) exists and is valid

## License

This project is licensed under the MIT License.
See [LICENSE](LICENSE) for the full text.

## Third-Party Code

This repository includes third-party code under [lib/Seeed_GFX-master](lib/Seeed_GFX-master).
Please review and comply with the upstream license files in that directory.