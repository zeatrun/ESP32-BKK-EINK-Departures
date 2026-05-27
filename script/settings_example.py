"""Template settings for helper publisher scripts.

Copy this file to settings.py and fill in your real/private values.
"""

# MQTT
MQTT_BROKER = "192.168.0.138"
MQTT_PORT = 1883
MQTT_KEEPALIVE = 60
MQTT_BKK_TOPIC = "bkk/stop"
MQTT_WEATHER_TOPIC = "weather/forecast"

# BKK API
BKK_API_KEY = "YOUR_BKK_API_KEY"
STOP_ID_MAV = "BKK_005501438_0"
STOP_ID_VOLAN = "volan_778557_2"

# Weather location
# If True, weather_grabber resolves location from city data.
# If False, it uses LATITUDE and LONGITUDE directly.
USE_CITY_LOOKUP = True
CITY_NAME = "Pilisszentivan"
CITY_COUNTY = "Pest"
CITY_COUNTRY_CODE = "HU"
LATITUDE = 47.6130
LONGITUDE = 18.9080

# Weather refresh interval in seconds.
WEATHER_REFRESH_SECONDS = 300
