#pragma once

#include <Arduino.h>
#include <freertos/semphr.h>

// Maximum number of stored departures per category
#define MAX_DEPARTURES 5

/**
 * A single scheduled departure parsed from the MQTT JSON payload.
 *
 * Classification rule:
 *   - line[0] is a digit  → bus   (e.g. "830")
 *   - line[0] is a letter → train (e.g. "Z72", "S76")
 */
struct Departure
{
    char          line[8];         // e.g. "Z72", "830"
    char          routeIdText[96]; // e.g. "Esztergom | Budapest-Nyugati"
    char          destination[72]; // e.g. "Budapest-Nyugati"
    char          stopName[72];    // e.g. "Pilisvörösvár"
    int           minutes;         // minutes until departure
    unsigned long timestamp;       // Unix timestamp of scheduled departure
};

/**
 * Mutex that protects g_busDepartures / g_busCount and
 * g_trainDepartures / g_trainCount from concurrent access.
 *
 * Always take this mutex before reading OR writing any of the four globals.
 */
extern SemaphoreHandle_t g_departuresMutex;

extern Departure g_busDepartures[MAX_DEPARTURES];
extern int       g_busCount;

extern Departure g_trainDepartures[MAX_DEPARTURES];
extern int       g_trainCount;
extern bool      g_departuresValid;

/**
 * Create the mutex.  Must be called once before any task starts.
 */
void departuresInit();
