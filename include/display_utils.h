#pragma once

#include <Arduino.h>
#include "TFT_eSPI.h"

namespace displayutil
{
void drawMonochromeSprite(EPaper& display,
                          int x,
                          int y,
                          const uint16_t* rows,
                          int width,
                          int height,
                          int scale,
                          uint16_t color);

void drawArrowIcon(EPaper& display, int x, int y, bool up, uint16_t color);
void drawArrowIconScaled(EPaper& display, int x, int y, bool up, uint16_t color, int scale);

void formatDepartureTime(unsigned long unixTimestamp, char* out, size_t outSize);
void formatDepartureEtaMinutes(int minutes, char* out, size_t outSize);
void formatTempWithDegree(float tempValue, bool valid, char* out, size_t outSize);
void getHungarianWeekdayLabel(const char* isoDate, char* out, size_t outSize);
}