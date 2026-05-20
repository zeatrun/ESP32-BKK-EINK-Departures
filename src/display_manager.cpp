#include "display_manager.h"

#include "driver.h"
#include "TFT_eSPI.h"

static EPaper g_epaper;
static SemaphoreHandle_t g_displayMutex = nullptr;

static bool takeDisplayMutex(TickType_t timeoutTicks)
{
    if (g_displayMutex == nullptr)
    {
        g_displayMutex = xSemaphoreCreateMutex();
        configASSERT(g_displayMutex);
    }
    return xSemaphoreTake(g_displayMutex, timeoutTicks) == pdTRUE;
}

void displayBegin()
{
    if (takeDisplayMutex(pdMS_TO_TICKS(200)))
    {
        g_epaper.begin();
        xSemaphoreGive(g_displayMutex);
    }
}

void displayFillScreen(uint16_t color)
{
    if (takeDisplayMutex(pdMS_TO_TICKS(200)))
    {
        g_epaper.fillScreen(color);
        xSemaphoreGive(g_displayMutex);
    }
}

void displayUpdate()
{
    if (takeDisplayMutex(pdMS_TO_TICKS(200)))
    {
        g_epaper.update();
        xSemaphoreGive(g_displayMutex);
    }
}

void displaySleep()
{
    if (takeDisplayMutex(pdMS_TO_TICKS(200)))
    {
        g_epaper.sleep();
        xSemaphoreGive(g_displayMutex);
    }
}

void displayWake()
{
    if (takeDisplayMutex(pdMS_TO_TICKS(200)))
    {
        g_epaper.wake();
        xSemaphoreGive(g_displayMutex);
    }
}

void displayClearWhite()
{
    displayFillScreen(TFT_WHITE);
    displayUpdate();
}

void displayDrawHeader(const char* title)
{
    if (takeDisplayMutex(pdMS_TO_TICKS(200)))
    {
        // Skeleton only: keep it simple now, layout will be finalized later.
        g_epaper.setTextColor(TFT_BLACK, TFT_WHITE, true);
        g_epaper.setTextSize(2);
        g_epaper.setCursor(10, 10);
        g_epaper.print(title != nullptr ? title : "Departures");
        xSemaphoreGive(g_displayMutex);
    }
}

void displayRenderDepartures(const Departure* trains,
                             int              trainCount,
                             const Departure* buses,
                             int              busCount)
{
    // Skeleton for future UI rendering from parsed JSON data.
    // Intended direction:
    // 1) clear or region-clear
    // 2) draw header + two sections (trains, buses)
    // 3) show line / destination / minutes

    (void)trains;
    (void)trainCount;
    (void)buses;
    (void)busCount;

    if (takeDisplayMutex(pdMS_TO_TICKS(200)))
    {
        g_epaper.fillScreen(TFT_WHITE);
        g_epaper.setTextColor(TFT_BLACK, TFT_WHITE, true);
        g_epaper.setTextSize(2);
        g_epaper.setCursor(10, 10);
        g_epaper.print("Live Departures");
        g_epaper.setCursor(10, 50);
        g_epaper.setTextSize(1);
        g_epaper.print("TODO: render trains and buses");
        g_epaper.update();
        xSemaphoreGive(g_displayMutex);
    }
}

void displayRenderFromGlobals()
{
    Departure trainCopy[MAX_DEPARTURES] = {};
    Departure busCopy[MAX_DEPARTURES]   = {};
    int trainCount = 0;
    int busCount   = 0;

    if (xSemaphoreTake(g_departuresMutex, pdMS_TO_TICKS(200)) == pdTRUE)
    {
        trainCount = g_trainCount;
        if (trainCount > MAX_DEPARTURES) trainCount = MAX_DEPARTURES;

        busCount = g_busCount;
        if (busCount > MAX_DEPARTURES) busCount = MAX_DEPARTURES;

        for (int i = 0; i < trainCount; ++i) trainCopy[i] = g_trainDepartures[i];
        for (int i = 0; i < busCount; ++i)   busCopy[i]   = g_busDepartures[i];

        xSemaphoreGive(g_departuresMutex);
    }

    displayRenderDepartures(trainCopy, trainCount, busCopy, busCount);
}
