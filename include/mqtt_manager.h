#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include "weather.h"
#include "settings.h"

// Broker settings
#define MQTT_SERVER SETTINGS_MQTT_SERVER
#define MQTT_PORT SETTINGS_MQTT_PORT
#define MQTT_SUB_TOPIC SETTINGS_MQTT_TOPIC_DEPARTURES
#define MQTT_WEATHER_TOPIC SETTINGS_MQTT_TOPIC_WEATHER

// PubSubClient RX/TX buffer – must fit the largest expected JSON payload
#define MQTT_BUFFER_SIZE 2048

// Reconnect interval when broker is unreachable
#define MQTT_RECONNECT_DELAY_MS 5000

/**
 * Initialise the MQTT manager.
 *
 * @param connectedEventGroup  The same EventGroupHandle_t used by wifi_manager;
 *                             the MQTT task waits on WIFI_CONNECTED_BIT before connecting.
 * @param espClient            Shared WiFiClient instance (must outlive all tasks).
 * @param clientMutex          Mutex that guards access to PubSubClient (shared resource).
 */
void mqttManagerInit(EventGroupHandle_t connectedEventGroup,
                     WiFiClient&        espClient,
                     SemaphoreHandle_t  clientMutex);

/**
 * Spawn the MQTT management task on Core 0.
 */
void mqttTaskStart();

/**
 * Returns true if MQTT client is currently connected to the broker.
 */
bool mqttManagerIsConnected();

/**
 * Returns the local time of the last successfully parsed MQTT payload.
 *
 * @param outTime  Destination tm struct.
 * @return true if at least one valid MQTT payload was received.
 */
bool mqttManagerGetLastUpdateTime(struct tm* outTime);
