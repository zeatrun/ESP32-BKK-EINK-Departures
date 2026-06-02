#include "bkk_departures_provider.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <algorithm>
#include <cstring>
#include <cctype>

namespace
{
constexpr int MAX_REASONABLE_DEPARTURE_MINUTES = 180; // 3 hours

int64_t normalizeEpochMs(int64_t value)
{
    if (value <= 0)
    {
        return 0;
    }

    // Heuristic: epoch seconds are ~1e9, epoch milliseconds are ~1e12.
    if (value < 100000000000LL)
    {
        return value * 1000LL;
    }

    return value;
}

void sanitizeHeadsign(const char* rawHeadsign, char* out, size_t outSize)
{
    if (out == nullptr || outSize == 0)
    {
        return;
    }

    out[0] = '\0';
    if (rawHeadsign == nullptr)
    {
        return;
    }

    const char* start = rawHeadsign;
    // Some feeds prefix destination with time like "15:33 Esztergom".
    if (isdigit(static_cast<unsigned char>(start[0]))
        && isdigit(static_cast<unsigned char>(start[1]))
        && start[2] == ':'
        && isdigit(static_cast<unsigned char>(start[3]))
        && isdigit(static_cast<unsigned char>(start[4])))
    {
        start += 5;
        while (*start == ' ' || *start == '-' || *start == '|')
        {
            ++start;
        }
    }

    strlcpy(out, start, outSize);
}
}

BkkDeparturesProvider::BkkDeparturesProvider(const char* apiKey, const char* stopId)
{
    if (apiKey != nullptr)
    {
        strlcpy(m_apiKey, apiKey, sizeof(m_apiKey));
    }
    if (stopId != nullptr)
    {
        strlcpy(m_stopId, stopId, sizeof(m_stopId));
    }
}

void BkkDeparturesProvider::setStopId(const char* stopId)
{
    if (stopId != nullptr)
    {
        strlcpy(m_stopId, stopId, sizeof(m_stopId));
    }
}

bool BkkDeparturesProvider::fetchDepartures(
    Departure* outBuses, int& outBusCount,
    Departure* outTrains, int& outTrainCount)
{
    if (strlen(m_apiKey) == 0 || strlen(m_stopId) == 0)
    {
        Serial.println("[BKK] API key or stop ID not configured");
        outBusCount = 0;
        outTrainCount = 0;
        return false;
    }

    HTTPClient http;

    const size_t keyLen = strlen(m_apiKey);
    const bool hasKey = keyLen > 0;
    const char* keyTail = hasKey && keyLen > 4 ? (m_apiKey + keyLen - 4) : m_apiKey;
    Serial.printf("[BKK] Request config: stopId='%s' keyLen=%u keyTail='%s'\n",
                  m_stopId,
                  static_cast<unsigned int>(keyLen),
                  hasKey ? keyTail : "");

    // Build BKK API URL
    String url = "https://futar.bkk.hu/api/query/v1/ws/otp/api/where/arrivals-and-departures-for-stop.json?";
    url += "stopId=";
    url += m_stopId;
    url += "&key=";
    url += m_apiKey;

    Serial.printf("[BKK] Fetching from endpoint for stopId='%s'\n", m_stopId);

    if (!http.begin(url))
    {
        Serial.println("[BKK] HTTP begin failed");
        outBusCount = 0;
        outTrainCount = 0;
        return false;
    }

    http.setConnectTimeout(5000);
    http.setTimeout(10000);

    int httpCode = http.GET();
    if (httpCode != 200)
    {
        String errorPayload = http.getString();
        Serial.printf("[BKK] HTTP error: %d, body(160)='%.160s'\n",
                      httpCode,
                      errorPayload.c_str());
        http.end();
        outBusCount = 0;
        outTrainCount = 0;
        return false;
    }

    String payload = http.getString();
    http.end();

    Serial.printf("[BKK] Response (%d bytes): %.100s...\n", payload.length(), payload.c_str());

    return parseBkkResponse(payload, outBuses, outBusCount, outTrains, outTrainCount);
}

bool BkkDeparturesProvider::isTrainRoute(const char* shortName, const char* description)
{
    // Primary classifier: line prefix. Numeric lines are buses, letter-prefixed are trains.
    if (shortName != nullptr && shortName[0] != '\0')
    {
        const unsigned char first = static_cast<unsigned char>(shortName[0]);
        if (isdigit(first))
        {
            return false;
        }
        return true;
    }

    if (!description)
    {
        return false;
    }

    // Fallback heuristic from textual description.
    const String desc(description);
    return desc.indexOf("Z") == 0 || desc.indexOf("Train") >= 0 || desc.indexOf("MAV") >= 0;
}

bool BkkDeparturesProvider::parseBkkResponse(
    const String& jsonStr,
    Departure* outBuses, int& outBusCount,
    Departure* outTrains, int& outTrainCount)
{
    // Clear output arrays
    outBusCount = 0;
    outTrainCount = 0;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error)
    {
        Serial.printf("[BKK] JSON parse error: %s\n", error.c_str());
        return false;
    }

    // Get data -> entry -> stopTimes
    JsonObject entry = doc["data"]["entry"];
    JsonArray stopTimes = entry["stopTimes"];

    if (!stopTimes)
    {
        Serial.println("[BKK] No stopTimes in response");
        return false;
    }

    // Get references for lookups
    JsonObject references = doc["data"]["references"];
    JsonObject trips = references["trips"];
    JsonObject routes = references["routes"];
    JsonObject stops = references["stops"];
    int64_t currentTimeMsRaw = doc["currentTime"].is<int64_t>()
                                   ? doc["currentTime"].as<int64_t>()
                                   : static_cast<int64_t>(millis());
    const int64_t currentTimeMs = normalizeEpochMs(currentTimeMsRaw);

    // Parse each stop time
    for (JsonObject stopTime : stopTimes)
    {
        if (outBusCount >= MAX_DEPARTURES && outTrainCount >= MAX_DEPARTURES)
        {
            break; // Full
        }

        // Get departure time (predictedDepartureTime preferred, fallback to departureTime)
        int64_t depTimeMs = 0;
        if (stopTime["predictedDepartureTime"].is<int64_t>())
        {
            depTimeMs = stopTime["predictedDepartureTime"].as<int64_t>();
        }
        else if (stopTime["departureTime"].is<int64_t>())
        {
            depTimeMs = stopTime["departureTime"].as<int64_t>();
        }

        depTimeMs = normalizeEpochMs(depTimeMs);

        if (depTimeMs == 0)
        {
            continue; // No departure time
        }

        // Calculate minutes to departure
        int64_t minutesToDep = (depTimeMs - currentTimeMs) / (60LL * 1000LL);
        if (minutesToDep < 0)
        {
            minutesToDep = 0;
        }

        if (minutesToDep > MAX_REASONABLE_DEPARTURE_MINUTES)
        {
            Serial.printf("[BKK] Skipping unrealistic departure: minutes=%lld\n",
                          static_cast<long long>(minutesToDep));
            continue;
        }

        // Get trip and route info
        const char* tripId = stopTime["tripId"];
        if (!tripId)
            continue;

        JsonObject trip = trips[tripId];
        const char* routeId = trip["routeId"];
        if (!routeId)
        {
            continue;
        }
        JsonObject route = routes[routeId];

        const char* shortName = route["shortName"];
        const char* description = route["description"];
        const char* stopHeadsign = stopTime["stopHeadsign"];
        const char* tripHeadsign = trip["tripHeadsign"];

        char cleanHeadsign[72] = {0};
        sanitizeHeadsign((tripHeadsign && tripHeadsign[0] != '\0') ? tripHeadsign : stopHeadsign,
                         cleanHeadsign,
                         sizeof(cleanHeadsign));

        if (!shortName || cleanHeadsign[0] == '\0')
            continue;

        // Get stop name
        const char* stopIdStr = entry["stopId"];
        JsonObject stopObj = stops[stopIdStr];
        const char* stopName = stopObj["name"];
        if (!stopName)
        {
            stopName = "Unknown";
        }

        // Determine if bus or train and add to appropriate array
        Departure dep = {};
        strlcpy(dep.line, shortName, sizeof(dep.line));
        strlcpy(dep.routeIdText, description ? description : "", sizeof(dep.routeIdText));
        strlcpy(dep.destination, cleanHeadsign, sizeof(dep.destination));
        strlcpy(dep.stopName, stopName, sizeof(dep.stopName));
        dep.minutes = static_cast<int>(minutesToDep);
        dep.timestamp = static_cast<unsigned long>(depTimeMs / 1000LL);

        if (isTrainRoute(shortName, description))
        {
            if (outTrainCount < MAX_DEPARTURES)
            {
                outTrains[outTrainCount++] = dep;
                Serial.printf("[BKK] Added train: %s -> %s in %d min\n",
                              shortName, cleanHeadsign, dep.minutes);
            }
        }
        else
        {
            if (outBusCount < MAX_DEPARTURES)
            {
                outBuses[outBusCount++] = dep;
                Serial.printf("[BKK] Added bus: %s -> %s in %d min\n",
                              shortName, cleanHeadsign, dep.minutes);
            }
        }
    }

    // Sort both arrays by departure time
    auto compareByTime = [](const Departure& a, const Departure& b) {
        return a.timestamp < b.timestamp;
    };

    std::sort(outBuses, outBuses + outBusCount, compareByTime);
    std::sort(outTrains, outTrains + outTrainCount, compareByTime);

    Serial.printf("[BKK] Parsing success: %d buses, %d trains\n", outBusCount, outTrainCount);
    return true;
}
