#pragma once

#include <Arduino.h>
#include <WiFi.h>

// User-specific settings are read from settings.h (ignored by git).
// If it is missing, fall back to settings_example.h so the project still builds.
#if __has_include("settings.h")
#include "settings.h"
#else
#warning "settings.h not found; using settings_example.h with dummy credentials. Please copy settings_example.h to settings.h and fill in your real credentials."
#include "settings_example.h"
#endif

// WiFi credentials come from settings defines.
#define WIFI_SSID     SETTINGS_WIFI_SSID
#define WIFI_PASSWORD SETTINGS_WIFI_PASSWORD

// Reconnect delay in ms
#define WIFI_RECONNECT_DELAY_MS 5000

/**
 * Initialise the WiFi manager.
 * Stores the event-group handle used to signal CONNECTED state.
 * Must be called before wifiTaskStart().
 *
 * @param connectedEventGroup  FreeRTOS EventGroupHandle_t; bit 0 is set when WiFi is up.
 */
void wifiManagerInit(EventGroupHandle_t connectedEventGroup);

/**
 * Spawn the WiFi management task on Core 0.
 * The task keeps WiFi alive and re-connects on drop.
 */
void wifiTaskStart();

/**
 * Returns true if WiFi is currently connected.
 */
bool wifiIsConnected();
