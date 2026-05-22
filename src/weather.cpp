#include "weather.h"

SemaphoreHandle_t g_weatherMutex = nullptr;
WeatherData       g_weatherData  = {};
bool              g_weatherValid = false;

void weatherInit()
{
    g_weatherMutex = xSemaphoreCreateMutex();
    configASSERT(g_weatherMutex);
}
