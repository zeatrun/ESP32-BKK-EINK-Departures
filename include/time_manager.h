#pragma once

#include <Arduino.h>
#include <freertos/event_groups.h>
#include <time.h>

/**
 * Initialise the time manager.
 * Must be called before timeManagerStart().
 *
 * @param wifiEventGroup  The shared EventGroupHandle_t from wifi_manager.
 *                        The task watches WIFI_CONNECTED_BIT to trigger
 *                        NTP sync and forced re-sync on reconnect.
 */
void timeManagerInit(EventGroupHandle_t wifiEventGroup);

/**
 * Spawn the time task on Core 0.
 */
void timeManagerStart();

/**
 * Returns true if at least one successful NTP sync has occurred.
 * After the first sync the ESP's internal clock keeps counting even
 * without WiFi, so this stays true unless a full restart happens.
 */
bool timeManagerIsSynced();

/**
 * Returns true when WiFi has been absent for more than 2 minutes since
 * the last known-good NTP sync.  Clears automatically when WiFi returns
 * and a new sync completes.
 */
bool timeManagerIsSyncLost();

/**
 * Copy the current local time (UTC) into *outTime.
 * Returns false if the clock has never been synced or outTime is nullptr.
 */
bool timeManagerGetTime(struct tm* outTime);
