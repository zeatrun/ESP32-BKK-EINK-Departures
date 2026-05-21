#include "display_manager.h"
#include "displaySprites.h"

#include "driver.h"
#include "TFT_eSPI.h"

static EPaper g_epaper;
static SemaphoreHandle_t g_displayMutex = nullptr;
static TaskHandle_t g_displayTaskHandle = nullptr;

static void drawSprite(int x,
                       int y,
                       const uint16_t* rows,
                       int width,
                       int height,
                       int scale,
                       uint16_t color)
{
    if (rows == nullptr || width <= 0 || height <= 0 || scale <= 0)
    {
        return;
    }

    for (int row = 0; row < height; ++row)
    {
        uint16_t bits = rows[row];
        for (int col = 0; col < width; ++col)
        {
            const uint16_t mask = static_cast<uint16_t>(1U << (width - 1 - col));
            if ((bits & mask) == 0)
            {
                continue;
            }

            if (scale == 1)
            {
                g_epaper.drawPixel(x + col, y + row, color);
            }
            else
            {
                g_epaper.fillRect(x + (col * scale), y + (row * scale), scale, scale, color);
            }
        }
    }
}

static void drawSleepingIcon(int centerX, int centerY, const uint16_t* spriteRows, uint16_t spriteColor)
{
    constexpr int iconScale = 2;
    const int iconW = SPRITE_ICON_WIDTH * iconScale;
    const int iconH = SPRITE_ICON_HEIGHT * iconScale;
    const int iconX = centerX - (iconW / 2);
    const int iconY = centerY - (iconH / 2) + 8;

    drawSprite(iconX,
               iconY,
               spriteRows,
               SPRITE_ICON_WIDTH,
               SPRITE_ICON_HEIGHT,
               iconScale,
               spriteColor);

    g_epaper.setTextColor(spriteColor, EINK_WHITE, true);
    g_epaper.setTextDatum(TL_DATUM);
    g_epaper.setTextSize(1);
    g_epaper.drawString("z", iconX + iconW - 2, iconY - 2);
    g_epaper.drawString("z", iconX + iconW + 8, iconY - 12);
    g_epaper.drawString("z", iconX + iconW + 17, iconY - 20);
}
static Departure s_trainCopy[MAX_DEPARTURES] = {};
static Departure s_busCopy[MAX_DEPARTURES]   = {};

static void drawBoldText(const char* text, int x, int y)
{
    if (text == nullptr || text[0] == '\0')
    {
        return;
    }

    g_epaper.drawString(text, x, y);
    g_epaper.drawString(text, x + 1, y);
}

static void drawBoldTextRight(const char* text, int rightX, int y)
{
    if (text == nullptr || text[0] == '\0')
    {
        return;
    }

    g_epaper.setTextDatum(MR_DATUM);
    g_epaper.drawString(text, rightX, y);
    g_epaper.drawString(text, rightX - 1, y);
    g_epaper.setTextDatum(TL_DATUM);
}

static void drawClockIcon(int centerX, int centerY, uint16_t color)
{
    constexpr int radius = 6;

    // Clock face
    g_epaper.drawCircle(centerX, centerY, radius, color);
    g_epaper.fillCircle(centerX, centerY, 1, color);

    // Minute hand to 12 o'clock
    g_epaper.drawLine(centerX, centerY, centerX, centerY - 4, color);

    // Hour hand to ~4 o'clock
    g_epaper.drawLine(centerX, centerY, centerX + 3, centerY + 2, color);
}

static void ellipsizeToWidth(const char* src, char* out, size_t outSize, int maxWidthPx)
{
    if (out == nullptr || outSize == 0)
    {
        return;
    }

    out[0] = '\0';
    if (src == nullptr || src[0] == '\0' || maxWidthPx <= 0)
    {
        return;
    }

    strlcpy(out, src, outSize);
    if (g_epaper.textWidth(out) <= maxWidthPx)
    {
        return;
    }

    const char* dots = "...";
    const int dotsWidth = g_epaper.textWidth(dots);
    if (dotsWidth > maxWidthPx)
    {
        out[0] = '\0';
        return;
    }

    size_t len = strlen(out);
    while (len > 0)
    {
        out[--len] = '\0';

        char candidate[128] = {0};
        strlcpy(candidate, out, sizeof(candidate));
        strlcat(candidate, dots, sizeof(candidate));

        if (g_epaper.textWidth(candidate) <= maxWidthPx)
        {
            strlcpy(out, candidate, outSize);
            return;
        }
    }

    strlcpy(out, dots, outSize);
}

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

void displayLineData(const Departure* departures, int count, int x, int y, uint16_t color)
{
    constexpr int rectX      = 15;
    constexpr int rectW      = 60;
    constexpr int rectH      = 30;
    constexpr int rectRadius = 5;
    constexpr int rowStep    = 38;
    constexpr int rectYOffset = 5;
    constexpr int textLeftPadding = 8;
    constexpr int textRightPadding = 10;
    constexpr int rightPanelWidth = 470;
    constexpr int iconRadius = 6;
    constexpr int iconPaddingRight = 8;
    constexpr int iconGapFromText = 8;
    constexpr int etaGapToIcon = 6;
    constexpr int etaAreaWidth = 56;
    constexpr int groupInnerX = 9;
    constexpr int groupInnerY = -3;
    constexpr int groupInnerW = 462;
    constexpr int groupInnerH = 196;

    // Always clear the inner group area first to avoid ghost text/boxes from previous state.
    g_epaper.fillRect(x + groupInnerX, y + groupInnerY, groupInnerW, groupInnerH, EINK_WHITE);

    if (departures == nullptr || count <= 0)
    {
        const int centerX = x + groupInnerX + (groupInnerW / 2);
        const int centerY = y + groupInnerY + (groupInnerH / 2);

        if (color == EINK_YELLOW)
        {
            drawSleepingIcon(centerX, centerY, SPRITE_BUS_16X12, EINK_BLACK);
        }
        else
        {
            drawSleepingIcon(centerX, centerY, SPRITE_TRAIN_16X12, EINK_BLACK);
        }

        return;
    }

    for (int i = 0; i < count; ++i)
    {
        const int boxX = x + rectX;
        const int boxY = y + (i * rowStep) + rectYOffset;
        const int textX = boxX + rectW + textLeftPadding;
        const int iconCenterX = x + rightPanelWidth - iconPaddingRight - iconRadius;
        const int iconCenterY = boxY + (rectH / 2);
        const int iconLeftX = iconCenterX - iconRadius;
        const int etaRightX = iconLeftX - etaGapToIcon;
        const int etaLeftX = etaRightX - etaAreaWidth;
        const int textRightLimitX = etaLeftX - iconGapFromText;
        const int textMaxWidth = (textRightLimitX > textX) ? (textRightLimitX - textX) : 0;
        const int topLineY = boxY + 1;
        const int bottomLineY = boxY + (rectH / 2) + 1;

        g_epaper.fillRoundRect(boxX, boxY, rectW, rectH, rectRadius, color);
        g_epaper.fillRect(textX, boxY, (x + rightPanelWidth - textRightPadding) - textX, rectH, EINK_WHITE);
        
        g_epaper.setTextSize(2);
        g_epaper.setRotation(3);

        // Color pairs
        switch (color)
        {            
            case EINK_BLUE:
                g_epaper.setTextColor(EINK_WHITE, EINK_BLUE, true);
                break;
            case EINK_YELLOW:
                g_epaper.setTextColor(EINK_BLACK, EINK_YELLOW, true);
                break;
            default:
                g_epaper.setTextColor(EINK_BLACK, EINK_WHITE, true);
                break;
        }

        // `line` can be shorter than the fixed char array; trim trailing spaces.
        char lineText[9] = {0};
        memcpy(lineText, departures[i].line, sizeof(departures[i].line));
        lineText[8] = '\0';

        int len = 0;
        while (len < 8 && lineText[len] != '\0')
        {
            ++len;
        }
        while (len > 0 && lineText[len - 1] == ' ')
        {
            lineText[len - 1] = '\0';
            --len;
        }

        if (len > 0)
        {
            g_epaper.setTextDatum(MC_DATUM);
            g_epaper.drawString(lineText, boxX + (rectW / 2), boxY + (rectH / 2));
            g_epaper.setTextDatum(TL_DATUM);
        }

        g_epaper.setTextSize(1);
        g_epaper.setTextColor(EINK_BLACK, EINK_WHITE, true);
        g_epaper.setTextDatum(TL_DATUM);

        char destinationText[96] = {0};
        char routeText[128] = {0};

        ellipsizeToWidth(departures[i].destination, destinationText, sizeof(destinationText), textMaxWidth);
        ellipsizeToWidth(departures[i].routeIdText, routeText, sizeof(routeText), textMaxWidth);

        if (destinationText[0] != '\0')
        {
            drawBoldText(destinationText, textX, topLineY);
        }
        if (routeText[0] != '\0')
        {
            g_epaper.drawString(routeText, textX, bottomLineY);
        }

        const int minutes = (departures[i].minutes < 0) ? 0 : departures[i].minutes;
        char etaText[20] = {0};
        snprintf(etaText, sizeof(etaText), "%d Perc", minutes);

        g_epaper.setTextSize(2);
        g_epaper.setTextColor(EINK_BLACK, EINK_WHITE, true);
        drawBoldTextRight(etaText, etaRightX, boxY + (rectH / 2));

        g_epaper.setTextSize(1);

        drawClockIcon(iconCenterX, iconCenterY, EINK_BLACK);
    }
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
    g_epaper.setTextSize(1);
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
    drawSprite(BUS_SECTION_X + 14,
               BUS_SECTION_Y + 9,
               SPRITE_BUS_16X12,
               SPRITE_ICON_WIDTH,
               SPRITE_ICON_HEIGHT,
               1,
               EINK_BLACK);
    g_epaper.drawString("BUSZMEGALLO", BUS_SECTION_X + 40, BUS_SECTION_Y + 5);

    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_WHITE, EINK_BLUE, true);
    drawSprite(TRAIN_SECTION_X + 14,
               TRAIN_SECTION_Y + 9,
               SPRITE_TRAIN_16X12,
               SPRITE_ICON_WIDTH,
               SPRITE_ICON_HEIGHT,
               1,
               EINK_WHITE);
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
    g_epaper.drawString("Pilisvorosvar", TRAIN_SECTION_X + 380, TRAIN_SECTION_Y + 12);

    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_BLACK, EINK_YELLOW, true);
    g_epaper.drawString("Pilisszentivan - PEVDI", BUS_SECTION_X + 330, BUS_SECTION_Y + 12);
}

void displayTask(void* /*pvParameters*/)
{
    // One time setup
    if (takeDisplayMutex(pdMS_TO_TICKS(500)))
    {
        displayEmptyBackground();
        g_epaper.update();
        xSemaphoreGive(g_displayMutex);
    }

    // Wait for "new changed MQTT data" notifications and refresh only then.
    for(;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        int trainCount = 0;
        int busCount   = 0;

        if (xSemaphoreTake(g_departuresMutex, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            trainCount = g_trainCount;
            if (trainCount > MAX_DEPARTURES) trainCount = MAX_DEPARTURES;

            busCount = g_busCount;
            if (busCount > MAX_DEPARTURES) busCount = MAX_DEPARTURES;

            for (int i = 0; i < trainCount; ++i) s_trainCopy[i] = g_trainDepartures[i];
            for (int i = trainCount; i < MAX_DEPARTURES; ++i) s_trainCopy[i] = {};

            for (int i = 0; i < busCount; ++i)   s_busCopy[i]   = g_busDepartures[i];
            for (int i = busCount; i < MAX_DEPARTURES; ++i) s_busCopy[i] = {};

            xSemaphoreGive(g_departuresMutex);
        }

        if (takeDisplayMutex(pdMS_TO_TICKS(500)))
        {
            displayLineData(s_busCopy, busCount, BUS_SECTION_X, BUS_SECTION_Y + 43, EINK_YELLOW);
            displayLineData(s_trainCopy, trainCount, TRAIN_SECTION_X, TRAIN_SECTION_Y + 43, EINK_BLUE);
            g_epaper.update();
            xSemaphoreGive(g_displayMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

void displayNotifyDataChanged()
{
    if (g_displayTaskHandle != nullptr)
    {
        xTaskNotifyGive(g_displayTaskHandle);
    }
}

void displayNotifyMinuteChanged()
{
    // TODO: update the clock widget on the display with the current time.
    // timeManagerGetTime() will provide HH:MM and YYYY.MM.DD when implemented.
}

void displayTaskStart()
{
        // Pin to Core 1; networking stack is thread-safe on ESP32
    xTaskCreatePinnedToCore(
        displayTask,
        "DisplayTask",
        6144,
        nullptr,
        2,          // priority
        &g_displayTaskHandle,
        1           // core 1
    );
}