#pragma once

#include "departures_provider.h"
#include <WiFi.h>

/**
 * @brief BKK (Budapest Public Transport) departures provider.
 *
 * Fetches departure data from the BKK API endpoint:
 *   https://futar.bkk.hu/api/query/v1/ws/otp/api/where/arrivals-and-departures-for-stop.json
 *
 * Supports:
 *  - Multiple stop IDs (configurable)
 *  - Automatic separation of buses and trains by route type
 *  - Predictive vs scheduled departure time handling
 */
class BkkDeparturesProvider : public DeparturesProvider
{
public:
    /**
     * Initialize the BKK provider with API credentials and stop ID.
     *
     * @param apiKey   BKK API key
     * @param stopId   Stop ID (e.g., "BKK_005501438_0")
     */
    BkkDeparturesProvider(const char* apiKey, const char* stopId);

    /**
     * Fetch departures from BKK API for the configured stop.
     */
    bool fetchDepartures(
        Departure* outBuses, int& outBusCount,
        Departure* outTrains, int& outTrainCount
    ) override;

    const char* providerName() const override { return "BKK"; }

    /**
     * Update the stop ID (allows switching stops at runtime).
     */
    void setStopId(const char* stopId);

private:
    char m_apiKey[128];
    char m_stopId[64];

    /**
     * Parse BKK API JSON response into departures arrays.
     */
    bool parseBkkResponse(
        const String& jsonStr,
        Departure* outBuses, int& outBusCount,
        Departure* outTrains, int& outTrainCount
    );

    /**
     * Determine if a route is a bus or train based on route description/type.
     */
    static bool isTrainRoute(const char* description);
};
