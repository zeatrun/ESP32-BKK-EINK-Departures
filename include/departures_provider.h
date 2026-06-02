#pragma once

#include "departures.h"

/**
 * @brief Abstract interface for departures data providers.
 *
 * Different transit APIs (BKK, etc.) implement this interface to provide
 * a uniform way of fetching and parsing departure data into the common
 * Departure struct arrays.
 */
class DeparturesProvider
{
public:
    virtual ~DeparturesProvider() = default;

    /**
     * Fetch departures for buses and trains.
     *
     * @param[out] outBuses      Departure array for buses
     * @param[out] outBusCount   Number of buses fetched (max MAX_DEPARTURES)
     * @param[out] outTrains     Departure array for trains
     * @param[out] outTrainCount Number of trains fetched (max MAX_DEPARTURES)
     * @return true if fetch and parse was successful, false otherwise.
     */
    virtual bool fetchDepartures(
        Departure* outBuses, int& outBusCount,
        Departure* outTrains, int& outTrainCount
    ) = 0;

    /**
     * Human-readable provider name (e.g., "BKK", "Mock").
     */
    virtual const char* providerName() const = 0;
};
