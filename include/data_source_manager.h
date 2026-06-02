#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "configuration.h"

/**
 * Runtime selector between MQTT ingestion and direct API ingestion.
 *
 * The selected mode comes from Configuration (loaded at startup).
 */
void dataSourceManagerInit(EventGroupHandle_t connectedEventGroup,
                           WiFiClient&        espClient,
                           SemaphoreHandle_t  clientMutex);

/**
 * Starts the selected data-source backend task(s).
 */
void dataSourceManagerStart();

/**
 * Returns true when the active backend is considered connected/healthy.
 */
bool dataSourceManagerIsConnected();

/**
 * Returns the local time of the last successful payload/update.
 */
bool dataSourceManagerGetLastUpdateTime(struct tm* outTime);

/**
 * Returns the currently active startup-selected data source mode.
 */
Configuration::DataSourceMode dataSourceManagerMode();
