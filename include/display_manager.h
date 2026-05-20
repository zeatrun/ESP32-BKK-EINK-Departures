#pragma once

#include <Arduino.h>
#include "departures.h"

// Basic display lifecycle
void displayBegin();
void displayFillScreen(uint16_t color);
void displayUpdate();
void displaySleep();
void displayWake();

// Generic drawing helpers
void displayClearWhite();
void displayDrawHeader(const char* title);

// Skeletons for upcoming JSON/departure rendering
void displayRenderDepartures(const Departure* trains,
                             int              trainCount,
                             const Departure* buses,
                             int              busCount);
void displayRenderFromGlobals();
