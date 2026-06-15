#pragma once

#include <Arduino.h>
#include "ClearSky_64x48.h"
#include "Overcast_64x48.h"
#include "PartlyCloudy_64x48.h"
#include "MainlyClear_64x48.h"
#include "Fog_Light_64x48.h"
#include "Fog_Dense_64x48.h"
#include "Drizzle_64x48.h"
#include "Freezing_Drizzle_64x48.h"
#include "Light_Medium_Rain_64x48.h"
#include "Heavy_Rain_64x48.h"
#include "Freezing_Rain_64x48.h"
#include "Light_Medium_Snow_64x48.h"
#include "Heavy_Snow_64x48.h"
#include "Snow_Grains_64x48.h"
#include "Thunderstorm_64x48.h"
#include "Thunderstorm_Slight_Heavy_Hail_64x48.h"
#include "Bus_128x128.h"
#include "Bus_32x32.h"
#include "Train_128x128.h"
#include "Train_32x32.h"

// 9x12 monochrome metric sprites (one uint16_t per row, left-aligned to width 9)
constexpr int SPRITE_WEATHER_ICON_WIDTH  = 9;
constexpr int SPRITE_WEATHER_ICON_HEIGHT = 12;

constexpr uint16_t SPRITE_UMBRELLA_8X8[SPRITE_WEATHER_ICON_HEIGHT] = {
    0b000010000,
    0b001111100,
    0b011111110,
    0b111111111,
    0b111111111,
    0b101010101,
    0b000010000,
    0b000010000,
    0b000010000,
    0b000010000,
    0b001010000,
    0b000100000,
};

constexpr uint16_t SPRITE_DROP_8X8[SPRITE_WEATHER_ICON_HEIGHT] = {
    0b000010000,
    0b000010000,
    0b000101000,
    0b001000100,
    0b010000010,
    0b100000001,
    0b100000001,
    0b100000001,
    0b100000001,
    0b010000010,
    0b001000100,
    0b000111000,
};

constexpr uint16_t SPRITE_WIND_8X8[SPRITE_WEATHER_ICON_HEIGHT] = {
    0b000000000,
    0b011000000,
    0b001011111,
    0b111010001,
    0b000000001,
    0b111111111,
    0b000000000,
    0b111111110,
    0b000000010,
    0b000010010,
    0b000011110,
    0b000000000,
};
