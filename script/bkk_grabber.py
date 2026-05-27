""" This module connects to BKK API to get stop departure information and
sends it to an MQTT topic. """

import requests
import time
import json
import paho.mqtt.client as mqtt

try:
    from settings import (
        BKK_API_KEY,
        MQTT_BKK_TOPIC,
        MQTT_BROKER,
        MQTT_KEEPALIVE,
        MQTT_PORT,
        STOP_ID_MAV,
        STOP_ID_VOLAN,
    )
except ImportError as exc:
    raise SystemExit(
        "Missing script/settings.py. Copy script/settings_example.py to script/settings.py and fill values."
    ) from exc

MQTT_TOPIC = MQTT_BKK_TOPIC
API_KEY = BKK_API_KEY
API_URL = "https://futar.bkk.hu/api/query/v1/ws/otp/api/where/arrivals-and-departures-for-stop.json"

def parse_departures(data):
    """
    This function parses the input data dictionary. The expectation is the data
    is the info given for a stop. The departure times, destinations and other
    information is returned as the function result.

    Parameters
    ----------
    data : dict
        Input dictionary with JSON data provided by BKK API for a given stop.

    Returns
    -------
    result : dict
        Parsed departures for the given stop.

    """
    entry = data["data"]["entry"]

    stop_times = entry["stopTimes"]
    trips = data["data"]["references"]["trips"]
    routes = data["data"]["references"]["routes"]
    stops = data["data"]["references"]["stops"]

    result = []

    for stop_time in stop_times:
        trip_id = stop_time["tripId"]
        trip = trips.get(trip_id, {})

        route_id = trip.get("routeId")
        route = routes.get(route_id, {})

        line = route.get("shortName", route_id)

        # Departure time (Predicted is the prefered if available.)
        dep = stop_time.get("predictedDepartureTime") or stop_time.get("departureTime")

        if not dep:
            continue

        minutes = int((dep - data["currentTime"] / 1000) / 60)

        result.append({
            # Line ID
            "line"          : line,
            # Route ID
            "routeid"       : trip["routeId"],
            # Route name
            "routeIdText"   : route["description"],
            # Route
            "trip"          : trip_id,       
            # Arrival time in minutes
            "minutes"       : max(minutes, 0),
            # Arrival time timestamp
            "timestamp"     : dep,          
            # Destination
            "destination"   : stop_time["stopHeadsign"],
            # Stop name
            "stopName"      : stops[entry["stopId"]]["name"]
        })

    # Sort by departure time
    result.sort(key=lambda x: x["timestamp"])

    return result

def get_api_json_stop_data(stop_id, api_key):
    """
    This function requests the JSON data for a specific stop specified by
    stop_id, using the given API key, specified in api_key input. The
    requested data is returned.

    Parameters
    ----------
    stop_id : str
        Stop ID string to request from the API.
    api_key : str
        API key to be used for the API request.

    Returns
    -------
    data : dict
        Dictionary parsed from the API request.

    """
    params = {
        "stopId": stop_id,
        "key": api_key
    }

    r = requests.get(API_URL, params=params, timeout=5)
    r.raise_for_status()
    data = r.json()

    return data

def print_stop_departures(arrival_data):
    """
    This function prints the input arrival data for debug purpose.

    Parameters
    ----------
    arrival_data : list
        List of arrival data parsed for a given stop.

    Returns
    -------
    None.

    """
    if  len(arrival_data) != 0:
        print(f"\nStop: {arrival_data[0]['stopName']}\n")
    
        for a in arrival_data:
            print(f"{a['line']} | {a['stopName']} → {a['destination']} in {a['minutes']} min")

def main():
    last_payload = None
    merged_arrivals = []

    json_data = get_api_json_stop_data(STOP_ID_MAV, API_KEY)
    arrivals = parse_departures(json_data)
    merged_arrivals = arrivals
    print_stop_departures(arrivals)

    json_data = get_api_json_stop_data(STOP_ID_VOLAN, API_KEY)
    arrivals = parse_departures(json_data)
    merged_arrivals = merged_arrivals + arrivals
    print_stop_departures(arrivals)

    payload = json.dumps(merged_arrivals)

    if payload != last_payload:
        print("Publishing payload...")
        client.publish(MQTT_TOPIC, payload, retain=True)
        last_payload = payload
        print("Published: ", payload)

if __name__ == "__main__":
    client = mqtt.Client()
    client.connect(MQTT_BROKER, MQTT_PORT, MQTT_KEEPALIVE)

    while True:
        try:
            main()
        except Exception as e:
            print("Error:", e)
        time.sleep(60)
