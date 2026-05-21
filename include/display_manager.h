#pragma once

#include <Arduino.h>
#include "departures.h"

#define EINK_BLACK 0XF
#define EINK_WHITE 0X0
#define EINK_BLUE 0XD
#define EINK_YELLOW 0XB
#define EINK_GREEN 0X2
#define EINK_RED 0X6

#define STATUS_SECTION_X 0
#define STATUS_SECTION_Y 0
#define WEATHER_SECTION_X 0
#define WAEATHER_SECTION_Y 60
#define BUS_SECTION_X 0
#define BUS_SECTION_Y 300
#define TRAIN_SECTION_X 0
#define TRAIN_SECTION_Y 550

// Display RTOS
void displayTaskStart();

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
void displayEmptyBackground();
