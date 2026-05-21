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
        g_epaper.setRotation(3);
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

void displayEmptyBackground()
{
    // Container rectangles
    g_epaper.fillSmoothRoundRect(
        WEATHER_SECTION_X + 5, 
        WAEATHER_SECTION_Y, 
        170, 
        220 - 5, 
        8, 
        EINK_BLUE);
    g_epaper.drawRoundRect(
        WEATHER_SECTION_X + 185, 
        WAEATHER_SECTION_Y, 
        90, 
        220 - 5, 
        8, 
        EINK_BLACK);
    g_epaper.drawRoundRect(
        WEATHER_SECTION_X + 285, 
        WAEATHER_SECTION_Y, 
        90, 
        220 - 5, 
        8, 
        EINK_BLACK);
    g_epaper.drawRoundRect(
        WEATHER_SECTION_X + 385, 
        WAEATHER_SECTION_Y, 
        90, 
        220 - 5, 
        8, 
        EINK_BLACK);
    
    g_epaper.fillSmoothRoundRect(
        BUS_SECTION_X + 5, 
        BUS_SECTION_Y, 
        470, 
        240, 
        10, 
        EINK_YELLOW);
    g_epaper.fillSmoothRoundRect(
        BUS_SECTION_X + 9, 
        BUS_SECTION_Y + 40, 
        462, 
        196, 
        6, 
        EINK_WHITE);

    g_epaper.fillSmoothRoundRect(
        TRAIN_SECTION_X + 5, 
        TRAIN_SECTION_Y, 
        470, 
        240, 
        10, 
        EINK_BLUE);
    g_epaper.fillSmoothRoundRect(
        TRAIN_SECTION_X + 9, 
        TRAIN_SECTION_Y + 40, 
        462, 
        196, 
        6, 
        EINK_WHITE);

    // Horizontal dividers
    g_epaper.drawLine(WEATHER_SECTION_X + 190, WAEATHER_SECTION_Y + 150, WEATHER_SECTION_X + 270, WAEATHER_SECTION_Y + 150, EINK_BLACK);
    g_epaper.drawLine(WEATHER_SECTION_X + 290, WAEATHER_SECTION_Y + 150, WEATHER_SECTION_X + 370, WAEATHER_SECTION_Y + 150, EINK_BLACK);
    g_epaper.drawLine(WEATHER_SECTION_X + 390, WAEATHER_SECTION_Y + 150, WEATHER_SECTION_X + 470, WAEATHER_SECTION_Y + 150, EINK_BLACK);
    g_epaper.drawLine(WEATHER_SECTION_X + 10, WAEATHER_SECTION_Y + 170, WEATHER_SECTION_X + 170, WAEATHER_SECTION_Y + 170, EINK_WHITE);

    // Create text
    g_epaper.setTextSize(2);
    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_BLACK, EINK_WHITE, true);
    g_epaper.setTextFont(2);
    g_epaper.drawString("Holnap", WEATHER_SECTION_X + 210, WAEATHER_SECTION_Y + 16);

    g_epaper.setTextSize(2);
    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_WHITE, EINK_BLUE, true);
    g_epaper.drawString("MA", WEATHER_SECTION_X + 15, WAEATHER_SECTION_Y + 5);

    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_BLACK, EINK_YELLOW, true);
    g_epaper.drawString("BUSZMEGALLO", BUS_SECTION_X + 40, BUS_SECTION_Y + 5);

    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_WHITE, EINK_BLUE, true);
    g_epaper.drawString("VONAT MEGALLO", TRAIN_SECTION_X + 40, TRAIN_SECTION_Y + 5);

    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_BLACK, EINK_WHITE, true);
    g_epaper.drawString("IDOJARAS ES MENETREND", STATUS_SECTION_X + 20, STATUS_SECTION_Y + 5);

    g_epaper.setTextSize(1);
    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_BLACK, EINK_WHITE, true);
    g_epaper.drawString("Frissitve:", STATUS_SECTION_X + 330, STATUS_SECTION_Y + 40);

    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_WHITE, EINK_BLUE, true);
    g_epaper.drawString("Pilisvorosvar:", TRAIN_SECTION_X + 360, TRAIN_SECTION_Y + 10);

    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_BLACK, EINK_YELLOW, true);
    g_epaper.drawString("Pilisszentivan - PEVDI", BUS_SECTION_X + 330, BUS_SECTION_Y + 10);
}

void displayTask(void* /*pvParameters*/)
{
    // One time setup
    displayFillScreen(EINK_WHITE);
    displayUpdate();
    delay(1000);

    displayEmptyBackground();
    displayUpdate();

    // Continously update if needed
    for(;;)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void displayTaskStart()
{
        // Pin to Core 0; networking stack is thread-safe on ESP32
    xTaskCreatePinnedToCore(
        displayTask,
        "DisplayTask",
        4096,
        nullptr,
        2,          // priority
        nullptr,
        1           // core 1
    );
}