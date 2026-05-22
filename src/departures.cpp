#include "departures.h"

SemaphoreHandle_t g_departuresMutex = nullptr;

Departure g_busDepartures[MAX_DEPARTURES]   = {};
int       g_busCount                        = 0;

Departure g_trainDepartures[MAX_DEPARTURES] = {};
int       g_trainCount                      = 0;
bool      g_departuresValid                 = false;

void departuresInit()
{
    g_departuresMutex = xSemaphoreCreateMutex();
    configASSERT(g_departuresMutex);
}
