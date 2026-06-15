#include "display_manager.h"
#include "displaySprites.h"
#include "display_utils.h"

#include "driver.h"
#include "TFT_eSPI.h"
#include "data_source_manager.h"
#include "weather.h"
#include <qrcode.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <LittleFS.h>
#endif

static EPaper g_epaper;
static SemaphoreHandle_t g_displayMutex = nullptr;
static TaskHandle_t g_displayTaskHandle = nullptr;

constexpr uint32_t DISPLAY_NOTIFY_DATA   = (1UL << 0);
constexpr uint32_t DISPLAY_NOTIFY_STATUS = (1UL << 1);
constexpr uint32_t DISPLAY_DATA_REFRESH_INTERVAL_MS = 10UL * 60UL * 1000UL;

static void drawSleepingIcon(int centerX, int centerY, int width, int height,const uint16_t *spriteRows, uint16_t spriteColor)
{
    constexpr int iconScale = 1;
    const int iconW = width * iconScale;
    const int iconH = height * iconScale;
    const int iconX = centerX - (iconW / 2);
    const int iconY = centerY - (iconH / 2) + 8;

    g_epaper.pushImage(iconX, iconY, iconW, iconH, (uint16_t *)spriteRows);
    g_epaper.setTextColor(spriteColor, EINK_WHITE, true);
    g_epaper.setTextDatum(TL_DATUM);
    g_epaper.setTextSize(1);
    g_epaper.drawString("z", iconX + iconW - 2, iconY - 2);
    g_epaper.drawString("z", iconX + iconW + 8, iconY - 12);
    g_epaper.drawString("z", iconX + iconW + 17, iconY - 20);
}
static Departure s_trainCopy[MAX_DEPARTURES] = {};
static Departure s_busCopy[MAX_DEPARTURES]   = {};
static WeatherData s_weatherCopy = {};
static bool s_weatherValidCopy = false;

static bool s_notoFontCheckDone = false;
static bool s_notoFontAvailable = false;
static bool s_noto12Available = false;
static bool s_noto16Available = false;
static bool s_noto24Available = false;
static bool s_noto32Available = false;

static bool ensureNotoSansAvailable()
{
    if (s_notoFontCheckDone)
    {
        return s_notoFontAvailable;
    }
    s_notoFontCheckDone = true;

#if defined(SMOOTH_FONT) && defined(ARDUINO_ARCH_ESP32)
    if (!LittleFS.begin(false))
    {
        Serial.println("[DISPLAY] LittleFS init failed, Noto Sans disabled.");
        Serial.println("[DISPLAY] Tip: upload filesystem image (PlatformIO target: uploadfs).");
        return false;
    }

    Serial.printf("[DISPLAY] LittleFS mounted. total=%u used=%u\n",
                  static_cast<unsigned int>(LittleFS.totalBytes()),
                  static_cast<unsigned int>(LittleFS.usedBytes()));

    fs::File root = LittleFS.open("/");
    if (root)
    {
        fs::File file = root.openNextFile();
        while (file)
        {
            Serial.printf("[DISPLAY] LittleFS file: %s (%u bytes)\n",
                          file.name(),
                          static_cast<unsigned int>(file.size()));
            file = root.openNextFile();
        }
    }

    s_noto12Available = LittleFS.exists("/NotoSansHU12.vlw");
    s_noto16Available = LittleFS.exists("/NotoSansHU16.vlw");
    s_noto24Available = LittleFS.exists("/NotoSansHU24.vlw");
    s_noto32Available = LittleFS.exists("/NotoSansHU32.vlw");

    if (!(s_noto12Available || s_noto16Available || s_noto24Available || s_noto32Available))
    {
        Serial.println("[DISPLAY] No NotoSansHU*.vlw found, fallback font will be used.");
        return false;
    }

    s_notoFontAvailable = true;
    Serial.print("[DISPLAY] Noto Sans HU fonts available: ");
    if (s_noto12Available) Serial.print("12 ");
    if (s_noto16Available) Serial.print("16 ");
    if (s_noto24Available) Serial.print("24 ");
    if (s_noto32Available) Serial.print("32 ");
    Serial.println();
#endif

#if !defined(SMOOTH_FONT)
    Serial.println("[DISPLAY] SMOOTH_FONT is not enabled in TFT setup, custom vlw fonts are disabled.");
#endif

    return s_notoFontAvailable;
}

static const char* resolveNotoFontName(int requestedPt)
{
    switch (requestedPt)
    {
        case 12:
            if (s_noto12Available) return "NotoSansHU12";
            break;
        case 16:
            if (s_noto16Available) return "NotoSansHU16";
            break;
        case 24:
            if (s_noto24Available) return "NotoSansHU24";
            break;
        case 32:
            if (s_noto32Available) return "NotoSansHU32";
            break;
        default:
            break;
    }

    // Fallback order tuned for readability in this UI.
    if (s_noto16Available) return "NotoSansHU16";
    if (s_noto24Available) return "NotoSansHU24";
    if (s_noto12Available) return "NotoSansHU12";
    if (s_noto32Available) return "NotoSansHU32";
    return nullptr;
}

static void drawStringUtf8(const char* text, int x, int y, int requestedPt = 16)
{
    if (text == nullptr)
    {
        return;
    }

#if defined(SMOOTH_FONT) && defined(ARDUINO_ARCH_ESP32)
    if (ensureNotoSansAvailable())
    {
        const char* fontName = resolveNotoFontName(requestedPt);
        if (fontName != nullptr)
        {
            g_epaper.loadFont(fontName, LittleFS);
            g_epaper.drawString(text, x, y);
            g_epaper.unloadFont();
            return;
        }
    }
#endif

    g_epaper.drawString(text, x, y);
}

static void drawTopRightStatus()
{
    constexpr int topX = STATUS_SECTION_X + 355;
    constexpr int topY = STATUS_SECTION_Y + 2;
    constexpr int topClearWidth = 120;
    constexpr int topClearHeight = 16;

    constexpr int mqttTimeX = STATUS_SECTION_X + 75;
    constexpr int mqttTimeY = STATUS_SECTION_Y + 34;
    constexpr int mqttClearWidth = 80;
    constexpr int mqttClearHeight = 16;

    constexpr int dotRadius = 5;
    constexpr int dotX = STATUS_SECTION_X + 123;
    constexpr int dotY = STATUS_SECTION_Y + 42;

    g_epaper.fillRect(mqttTimeX, mqttTimeY, mqttClearWidth, mqttClearHeight, EINK_WHITE);

    struct tm updateTime = {};
    char mqttTimeStr[6] = "xx:xx";

    if (dataSourceManagerGetLastUpdateTime(&updateTime))
    {
        snprintf(mqttTimeStr, sizeof(mqttTimeStr), "%02d:%02d", updateTime.tm_hour, updateTime.tm_min);
    }

    g_epaper.setTextSize(1);
    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_BLACK, EINK_WHITE, true);
    g_epaper.drawString(mqttTimeStr, mqttTimeX, mqttTimeY);

    const uint16_t dotColor = dataSourceManagerIsConnected() ? EINK_GREEN : EINK_RED;
    g_epaper.fillCircle(dotX, dotY, dotRadius, dotColor);
    g_epaper.drawCircle(dotX, dotY, dotRadius, EINK_BLACK);
}

static void drawBoldText(const char* text, int x, int y)
{
    if (text == nullptr || text[0] == '\0')
    {
        return;
    }

    drawStringUtf8(text, x, y, 12);
    drawStringUtf8(text, x + 1, y, 12);
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

static void drawWeatherMetricsRow(int cardX,
                                  int cardW,
                                  int y,
                                  uint16_t textColor,
                                  uint16_t bgColor,
                                  float windKmh,
                                  int precipPercent,
                                  int humidityPercent,
                                  bool hasValues,
                                  bool precipOnly = false)
{
    const bool compact = cardW <= 100;

    char windText[20] = {0};
    char precipText[20] = {0};
    char humidityText[20] = {0};

    if (hasValues)
    {
        if (compact)
        {
            snprintf(windText, sizeof(windText), "%.0fk", windKmh);
            snprintf(precipText, sizeof(precipText), "%d%%", precipPercent);
            snprintf(humidityText, sizeof(humidityText), "%d%%", humidityPercent);
        }
        else
        {
            snprintf(windText, sizeof(windText), "%.0f km/h", windKmh);
            snprintf(precipText, sizeof(precipText), "%d %%", precipPercent);
            snprintf(humidityText, sizeof(humidityText), "%d %%", humidityPercent);
        }
    }
    else
    {
        if (compact)
        {
            strlcpy(windText, "xxk", sizeof(windText));
            strlcpy(precipText, "xx%", sizeof(precipText));
            strlcpy(humidityText, "xx%", sizeof(humidityText));
        }
        else
        {
            strlcpy(windText, "xx km/h", sizeof(windText));
            strlcpy(precipText, "xx %", sizeof(precipText));
            strlcpy(humidityText, "xx %", sizeof(humidityText));
        }
    }

    g_epaper.setTextColor(textColor, bgColor, true);
    g_epaper.setTextSize(1);

    const int topY = y;
    const int bottomY = y + SPRITE_WEATHER_ICON_HEIGHT;

    const int precipTextWidth = g_epaper.textWidth(precipText);
    const int precipBlockWidth = SPRITE_WEATHER_ICON_WIDTH + 2 + precipTextWidth;
    const int precipStartX = cardX + ((cardW - precipBlockWidth) / 2);

    displayutil::drawMonochromeSprite(g_epaper,
                                      precipStartX,
                                      topY + 2,
                                      SPRITE_UMBRELLA_8X8,
                                      SPRITE_WEATHER_ICON_WIDTH,
                                      SPRITE_WEATHER_ICON_HEIGHT,
                                      1,
                                      textColor);
    g_epaper.drawString(precipText, precipStartX + SPRITE_WEATHER_ICON_WIDTH + 2, topY);

    if (precipOnly)
    {
        return;
    }

    const int halfW = cardW / 2;
    const int windTextWidth = g_epaper.textWidth(windText);
    const int windBlockWidth = SPRITE_WEATHER_ICON_WIDTH + 2 + windTextWidth;
    const int windStartX = cardX + ((halfW - windBlockWidth) / 2);

    const int humidityTextWidth = g_epaper.textWidth(humidityText);
    const int humidityBlockWidth = SPRITE_WEATHER_ICON_WIDTH + 2 + humidityTextWidth;
    const int humidityStartX = cardX + halfW + ((halfW - humidityBlockWidth) / 2);

    displayutil::drawMonochromeSprite(g_epaper,
                                      windStartX,
                                      bottomY + 7,
                                      SPRITE_WIND_8X8,
                                      SPRITE_WEATHER_ICON_WIDTH,
                                      SPRITE_WEATHER_ICON_HEIGHT,
                                      1,
                                      textColor);
    g_epaper.drawString(windText, windStartX + SPRITE_WEATHER_ICON_WIDTH + 2, bottomY + 5);

    displayutil::drawMonochromeSprite(g_epaper,
                                      humidityStartX,
                                      bottomY + 7,
                                      SPRITE_DROP_8X8,
                                      SPRITE_WEATHER_ICON_WIDTH,
                                      SPRITE_WEATHER_ICON_HEIGHT,
                                      1,
                                      textColor);
    g_epaper.drawString(humidityText, humidityStartX + SPRITE_WEATHER_ICON_WIDTH + 2, bottomY + 5);
}

static void drawCenteredText(const char* text, int centerX, int y, int fontPt = 16)
{
    g_epaper.setTextDatum(MC_DATUM);
    drawStringUtf8(text, centerX, y, fontPt);
    g_epaper.setTextDatum(TL_DATUM);
}

static void appendWifiQrEscaped(char* dst, size_t dstSize, const char* src)
{
    if (dst == nullptr || dstSize == 0 || src == nullptr)
    {
        return;
    }

    for (const char* p = src; *p != '\0'; ++p)
    {
        if (*p == '\\' || *p == ';' || *p == ',' || *p == ':')
        {
            strlcat(dst, "\\", dstSize);
        }

        char one[2] = {*p, '\0'};
        strlcat(dst, one, dstSize);
    }
}

static bool drawWifiQrCodeInArea(int x,
                                 int y,
                                 int areaW,
                                 int areaH,
                                 const char* ssid,
                                 const char* password)
{
    if (ssid == nullptr || ssid[0] == '\0' || areaW <= 40 || areaH <= 40)
    {
        return false;
    }

    char payload[220] = {0};
    strlcpy(payload, "WIFI:T:WPA;S:", sizeof(payload));
    appendWifiQrEscaped(payload, sizeof(payload), ssid);
    strlcat(payload, ";P:", sizeof(payload));
    appendWifiQrEscaped(payload, sizeof(payload), (password != nullptr) ? password : "");
    strlcat(payload, ";;", sizeof(payload));

    constexpr uint8_t qrVersion = 7;
    uint8_t qrData[qrcode_getBufferSize(qrVersion)] = {0};
    QRCode qr;
    qrcode_initText(&qr, qrData, qrVersion, ECC_MEDIUM, payload);

    const int modules = qr.size;
    const int quietZone = 2;
    const int maxModuleW = areaW / (modules + (quietZone * 2));
    const int maxModuleH = areaH / (modules + (quietZone * 2));
    int modulePx = (maxModuleW < maxModuleH) ? maxModuleW : maxModuleH;
    if (modulePx < 1)
    {
        modulePx = 1;
    }

    const int qrDrawSize = modulePx * (modules + (quietZone * 2));
    const int startX = x + ((areaW - qrDrawSize) / 2);
    const int startY = y + ((areaH - qrDrawSize) / 2);

    g_epaper.fillRect(startX, startY, qrDrawSize, qrDrawSize, EINK_WHITE);

    for (int my = 0; my < modules; ++my)
    {
        for (int mx = 0; mx < modules; ++mx)
        {
            if (!qrcode_getModule(&qr, mx, my))
            {
                continue;
            }

            const int px = startX + ((mx + quietZone) * modulePx);
            const int py = startY + ((my + quietZone) * modulePx);
            g_epaper.fillRect(px, py, modulePx, modulePx, EINK_BLACK);
        }
    }

    return true;
}


static const char* weatherCodeToHungarianText(int code)
{
    switch (code)
    {
        case 0: return "Napos";
        case 1: return "T. napos";
        case 2: return "V. felhős";
        case 3: return "Borult";
        case 45:
        case 48: return "Ködös";
        case 51:
        case 53:
        case 55: return "Szitálás";
        case 56:
        case 57: return "Fagyos szit.";
        case 61:
        case 63:
        case 65: return "Eső";
        case 66:
        case 67: return "Ónos eső";
        case 71:
        case 73:
        case 75:
        case 77: return "Hó";
        case 80:
        case 81:
        case 82: return "Zápor";
        case 85:
        case 86: return "Hózápor";
        case 95: return "Zivatar";
        case 96:
        case 99: return "Jeges zivatar";
        default: return "Ismeretlen";
    }
}

static uint16_t* getWeatherSpriteData(int code, bool mainCard)
{
    switch (code)
    {
        case 0:
            // Clear sky
            if (mainCard)
                return (uint16_t *)ClearSky_BlueBG_64x48;
            else
                return (uint16_t *)ClearSky_WhiteBG_64x48;
        case 1: 
            // Mainly clear
            if (mainCard)
                return (uint16_t *)MainlyClear_BlueBG_64x48;
             else
                return (uint16_t *)MainlyClear_WhiteBG_64x48;
        case 2:
            // Partly cloudy
            if (mainCard)
                return (uint16_t *)PartlyCloudy_BlueBG_64x48;
            else
                return (uint16_t *)PartlyCloudy_WhiteBG_64x48;
        case 3:
            // Overcast
            if (mainCard)
                return (uint16_t *)Overcast_BlueBG_64x48;
            else
                return (uint16_t *)Overcast_WhiteBG_64x48;
        case 45:
            // Fog light
            if (mainCard)
                return (uint16_t *)Fog_Light_BlueBG_64x48;
            else
                return (uint16_t *)Fog_Light_WhiteBG_64x48;
        case 48: 
            // Fog dense
            if (mainCard)
                return (uint16_t *)Fog_Dense_BlueBG_64x48;
            else
                return (uint16_t *)Fog_Dense_WhiteBG_64x48;
        case 51:
        case 53:
        case 55:
            // Drizzle light-heavy
            if (mainCard)
                return (uint16_t *)Drizzle_BlueBG_64x48;
            else
                return (uint16_t *)Drizzle_WhiteBG_64x48;
        case 56:
        case 57:
            // Freezing drizzle light-heavy
            if (mainCard)
                return (uint16_t *)Freezing_Drizzle_BlueBG_64x48;
            else
                return (uint16_t *)Freezing_Drizzle_WhiteBG_64x48;
        case 61:
        case 63:
            // Rain light-moderate
            if (mainCard)
                return (uint16_t *)Light_Medium_Rain_BlueBG_64x48;
            else
                return (uint16_t *)Light_Medium_Rain_WhiteBG_64x48;
        case 65:
            // Rain heavy
            if (mainCard)
                return (uint16_t *)Heavy_Rain_BlueBG_64x48;
            else
                return (uint16_t *)Heavy_Rain_WhiteBG_64x48;
        case 66:
        case 67:
            // Freezing rain light-heavy
            if (mainCard)
                return (uint16_t *)Freezing_Rain_BlueBG_64x48;
            else
                return (uint16_t *)Freezing_Rain_WhiteBG_64x48;
        case 71:
        case 73:
            // Snow light-moderate
            if (mainCard)
                return (uint16_t *)Light_Medium_Snow_BlueBG_64x48;
            else
                return (uint16_t *)Light_Medium_Snow_WhiteBG_64x48;
        case 75:
            // Snow heavy
            if (mainCard)
                return (uint16_t *)Heavy_Snow_BlueBG_64x48;
            else
                return (uint16_t *)Heavy_Snow_WhiteBG_64x48;
        case 77:
            // Snow grains
            if (mainCard)
                return (uint16_t *)Snow_Grains_BlueBG_64x48;
            else
                return (uint16_t *)Snow_Grains_WhiteBG_64x48;
        case 80:
        case 81:
        case 82: 
            // Rain shower light-moderate-heavy
            if (mainCard)
                return (uint16_t *)Heavy_Rain_BlueBG_64x48;
            else
                return (uint16_t *)Heavy_Rain_WhiteBG_64x48;
        case 85:
        case 86: 
            // Snow shower light-heavy
            if (mainCard)
                return (uint16_t *)Snow_Grains_BlueBG_64x48;
            else
                return (uint16_t *)Snow_Grains_WhiteBG_64x48;
        case 95:
            // Thunderstorm
            if (mainCard)
                return (uint16_t *)Thunderstorm_BlueBG_64x48;
            else
                return (uint16_t *)Thunderstorm_WhiteBG_64x48;
        case 96:
        case 99: 
            // Thunderstorm with slight-heavy hail
            if (mainCard)
                return (uint16_t *)Thunderstorm_Slight_Heavy_Hail_BlueBG_64x48;
            else
                return (uint16_t *)Thunderstorm_Slight_Heavy_Hail_WhiteBG_64x48;
        default: 
            // Unknown weather code
            return nullptr;
    }
}

static void drawWeatherSpriteImage(int x, int y, uint16_t* imageData)
{
    if (imageData == nullptr)
        return;

    g_epaper.pushImage(x, y, 64, 48, (uint16_t *)imageData);
}

static void drawWeatherCards(const WeatherData* weather, bool hasWeather)
{
    constexpr int bigX = WEATHER_SECTION_X + 5;
    constexpr int bigY = WAEATHER_SECTION_Y;
    constexpr int bigW = 170;
    constexpr int bigH = 215;

    constexpr int smallY = WAEATHER_SECTION_Y;
    constexpr int smallW = 90;
    constexpr int smallH = 215;
    constexpr int smallXs[3] = {
        WEATHER_SECTION_X + 185,
        WEATHER_SECTION_X + 285,
        WEATHER_SECTION_X + 385
    };

    constexpr int iconReservedBottomY = WAEATHER_SECTION_Y + 72;
    constexpr int dividerY = WAEATHER_SECTION_Y + 150;

    g_epaper.setRotation(3);
    g_epaper.setTextFont(2);

    float todayMin = 0.0F;
    float todayMax = 0.0F;
    float todayWind = 0.0F;
    int todayPrecipPercent = 0;
    int todayHumidity = 0;

    bool hasToday = false;
    if (hasWeather && weather != nullptr)
    {
        todayWind = weather->windSpeedKmh;
        todayHumidity = weather->relativeHumidity;

        if (weather->dailyCount > 0)
        {
            todayMin = weather->daily[0].tempMinC;
            todayMax = weather->daily[0].tempMaxC;
            todayPrecipPercent = weather->daily[0].precipProbMax;
            hasToday = true;
        }
    }

    char todayMainText[12] = {0};
    char todayMaxText[12] = {0};
    char todayMinText[12] = {0};
    char todayStatusText[28] = "Nincs adat";

    displayutil::formatTempWithDegree(0.0F, false, todayMainText, sizeof(todayMainText));
    displayutil::formatTempWithDegree(0.0F, false, todayMaxText, sizeof(todayMaxText));
    displayutil::formatTempWithDegree(0.0F, false, todayMinText, sizeof(todayMinText));

    if (hasWeather && weather != nullptr && hasToday)
    {
        displayutil::formatTempWithDegree(weather->temperatureC, true, todayMainText, sizeof(todayMainText));
        displayutil::formatTempWithDegree(todayMax, true, todayMaxText, sizeof(todayMaxText));
        displayutil::formatTempWithDegree(todayMin, true, todayMinText, sizeof(todayMinText));
        strlcpy(todayStatusText,
                weatherCodeToHungarianText(weather->weatherCode),
                sizeof(todayStatusText));
    }

    // Big blue card (Today / Ma)
    g_epaper.setTextSize(2);
    g_epaper.fillRect(bigX + 1, bigY + 1, bigW - 2, bigH - 2, EINK_BLUE);
    g_epaper.setTextColor(EINK_WHITE, EINK_BLUE, true);
    g_epaper.drawString("MA", bigX + 10, bigY + 8);

    // Draw weather sprite on big card (right-aligned, 64x48 bytes)
    uint16_t * weatherSpriteData = getWeatherSpriteData(weather != nullptr ? weather->weatherCode : -1, true);
    if (weatherSpriteData != nullptr)
    {
        constexpr int weatherSpriteX = bigX + bigW - 80;
        constexpr int weatherSpriteY = bigY + 10;
        drawWeatherSpriteImage(weatherSpriteX, weatherSpriteY, weatherSpriteData);
    }

    constexpr int mainTempY = iconReservedBottomY + 2;
    constexpr int auxTopY = iconReservedBottomY + 7;
    constexpr int auxBottomY = iconReservedBottomY + 22;

    g_epaper.setTextSize(4);
    g_epaper.drawString(todayMainText, bigX + 10, mainTempY);

    displayutil::drawArrowIconScaled(g_epaper, bigX + 112, auxTopY + 1, true, EINK_WHITE, 2);
    displayutil::drawArrowIconScaled(g_epaper, bigX + 112, auxBottomY + 20, false, EINK_WHITE, 2);

    g_epaper.setTextSize(2);
    g_epaper.drawString(todayMaxText, bigX + 128, auxTopY - 1);
    g_epaper.drawString(todayMinText, bigX + 128, auxBottomY + 9);

    g_epaper.setTextSize(1);
    drawCenteredText(todayStatusText, bigX + (bigW / 2), dividerY + 10);

    g_epaper.drawLine(bigX + 6, dividerY, bigX + bigW - 6, dividerY, EINK_WHITE);
    drawWeatherMetricsRow(bigX,
                          bigW,
                          dividerY + 22,
                          EINK_WHITE,
                          EINK_BLUE,
                          todayWind,
                          todayPrecipPercent,
                          todayHumidity,
                          hasWeather && hasToday,
                          false);

    // Small white cards (Tomorrow + named weekdays)
    for (int i = 0; i < 3; ++i)
    {
        const int cardX = smallXs[i];
        g_epaper.fillRoundRect(cardX, smallY, smallW, smallH, 8, EINK_WHITE);
        g_epaper.drawRoundRect(cardX, smallY, smallW, smallH, 8, EINK_BLACK);

        char label[20] = {0};
        if (i == 0)
        {
            strlcpy(label, "Holnap", sizeof(label));
        }
        else if (hasWeather && weather != nullptr && weather->dailyCount > (i + 1))
        {
            displayutil::getHungarianWeekdayLabel(weather->daily[i + 1].date, label, sizeof(label));
        }
        else
        {
            strlcpy(label, "Nap", sizeof(label));
        }

        float dayMin = 0.0F;
        float dayMax = 0.0F;
        float dayMain = 0.0F;
        int dayPrecipPercent = 0;
        bool dayAvailable = false;

        if (hasWeather && weather != nullptr && weather->dailyCount > (i + 1))
        {
            dayMin = weather->daily[i + 1].tempMinC;
            dayMax = weather->daily[i + 1].tempMaxC;
            dayMain = (dayMin + dayMax) / 2.0F;
            dayPrecipPercent = weather->daily[i + 1].precipProbMax;
            dayAvailable = true;
        }

        char mainTempText[12] = {0};
        char maxTempText[12] = {0};
        char minTempText[12] = {0};
        char dayStatusText[28] = "Nincs adat";

        displayutil::formatTempWithDegree(0.0F, false, mainTempText, sizeof(mainTempText));
        displayutil::formatTempWithDegree(0.0F, false, maxTempText, sizeof(maxTempText));
        displayutil::formatTempWithDegree(0.0F, false, minTempText, sizeof(minTempText));

        if (dayAvailable)
        {
            displayutil::formatTempWithDegree(dayMain, true, mainTempText, sizeof(mainTempText));
            displayutil::formatTempWithDegree(dayMax, true, maxTempText, sizeof(maxTempText));
            displayutil::formatTempWithDegree(dayMin, true, minTempText, sizeof(minTempText));
            strlcpy(dayStatusText,
                    weatherCodeToHungarianText(weather->daily[i + 1].weatherCode),
                    sizeof(dayStatusText));
        }

        g_epaper.setTextColor(EINK_BLACK, EINK_WHITE, true);
        g_epaper.setTextSize(1);
        drawCenteredText(label, cardX + (smallW / 2), smallY + 12);

        // Draw weather sprite on small card (centered, 64x48 bytes)
        uint16_t * dayWeatherSpriteData = nullptr;
        if (dayAvailable && hasWeather && weather != nullptr && weather->dailyCount > (i + 1))
        {
            dayWeatherSpriteData = getWeatherSpriteData(weather->daily[i + 1].weatherCode, false);
        }
        if (dayWeatherSpriteData != nullptr)
        {
            const int weatherSpriteX = cardX + (smallW / 2) - 32;  // 64 pixels wide / 2 = 32
            const int weatherSpriteY = smallY + 20;
            drawWeatherSpriteImage(weatherSpriteX, weatherSpriteY, dayWeatherSpriteData);
        }

        constexpr int smallMainTempY = iconReservedBottomY + 2;
        constexpr int smallAuxTopY = iconReservedBottomY + 8;
        constexpr int smallAuxBottomY = iconReservedBottomY + 28;

        g_epaper.setTextSize(3);
        g_epaper.drawString(mainTempText, cardX + 6, smallMainTempY);

        displayutil::drawArrowIcon(g_epaper, cardX + 58, smallAuxTopY, true, EINK_BLACK);
        displayutil::drawArrowIcon(g_epaper, cardX + 58, smallAuxBottomY + 6, false, EINK_BLACK);

        g_epaper.setTextSize(1);
        g_epaper.drawString(maxTempText, cardX + 68, smallAuxTopY);
        g_epaper.drawString(minTempText, cardX + 68, smallAuxBottomY);

        drawCenteredText(dayStatusText, cardX + (smallW / 2), dividerY + 10);

        g_epaper.drawLine(cardX + 5, dividerY, cardX + smallW - 5, dividerY, EINK_BLACK);
        drawWeatherMetricsRow(cardX,
                              smallW,
                              dividerY + 30,
                              EINK_BLACK,
                              EINK_WHITE,
                              0.0F,
                              dayPrecipPercent,
                              0,
                              dayAvailable,
                              true);
    }
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
        // Remove one full UTF-8 code point (not just one byte) from the end.
        --len;
        while (len > 0 && (static_cast<unsigned char>(out[len]) & 0xC0U) == 0x80U)
        {
            --len;
        }
        out[len] = '\0';

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
        g_epaper.setAttribute(UTF8_SWITCH, true);
        ensureNotoSansAvailable();
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

void displayShowConfigurationScreen(const char* wifiSsid,
                                    const char* wifiPassword,
                                    const char* mqttServer,
                                    uint16_t    mqttPort,
                                    const char* mqttTopicDepartures,
                                    const char* mqttTopicWeather,
                                    const char* apSsid,
                                    const char* apPassword)
{
    if (!takeDisplayMutex(pdMS_TO_TICKS(200)))
    {
        return;
    }

    g_epaper.setRotation(3);

    const int screenW = g_epaper.width();
    const int screenH = g_epaper.height();
    const int imageSectionH = screenH / 3;
    const int configTopY = imageSectionH;
    const int centerX = screenW / 2;

    g_epaper.fillScreen(EINK_WHITE);

    // Top 1/3 reserved for a future image block.
    g_epaper.fillRect(0, 0, screenW, imageSectionH, EINK_BLUE);
    g_epaper.setTextColor(EINK_WHITE, EINK_BLUE, true);
    drawCenteredText("KONFIGURÁCIÓS MÓD", centerX, 18, 24);
    const bool qrRendered = drawWifiQrCodeInArea(12,
                                                 34,
                                                 screenW - 24,
                                                 imageSectionH - 46,
                                                 apSsid,
                                                 apPassword);
    if (!qrRendered)
    {
        drawCenteredText("QR-kód nem elérhető", centerX, (imageSectionH / 2) + 8, 16);
    }
    g_epaper.drawRect(10, 12, screenW - 20, imageSectionH - 24, EINK_WHITE);

    // Bottom 2/3: centered configuration data.
    g_epaper.setTextColor(EINK_BLACK, EINK_WHITE, true);
    g_epaper.drawLine(0, configTopY, screenW, configTopY, EINK_BLACK);

    drawCenteredText("Eszköz beállításai", centerX, configTopY + 18, 24);

    const int fieldCount = 8;
    const int firstRowY = configTopY + 42;
    const int bottomPadding = 8;
    int rowStep = (screenH - firstRowY - bottomPadding) / fieldCount;
    if (rowStep < 32)
    {
        rowStep = 32;
    }

    auto drawCenteredBoldText = [&](const char* text, int y, int fontPt)
    {
        g_epaper.setTextDatum(MC_DATUM);
        drawStringUtf8(text, centerX, y, fontPt);
        drawStringUtf8(text, centerX + 1, y, fontPt);
        g_epaper.setTextDatum(TL_DATUM);
    };

    auto drawConfigRow = [&](const char* label, const char* value, int rowBaseY)
    {
        char valueSafe[180] = {0};
        strlcpy(valueSafe, (value != nullptr) ? value : "", sizeof(valueSafe));
        if (valueSafe[0] == '\0')
        {
            strlcpy(valueSafe, "(nincs megadva)", sizeof(valueSafe));
        }

        if (strlen(valueSafe) > 48)
        {
            valueSafe[45] = '.';
            valueSafe[46] = '.';
            valueSafe[47] = '.';
            valueSafe[48] = '\0';
        }

        g_epaper.setTextColor(EINK_BLACK, EINK_WHITE, true);
        drawCenteredBoldText(label, rowBaseY + 6, 16);
        drawCenteredText(valueSafe, centerX, rowBaseY + 20, 16);
    };

    char mqttPortStr[16] = {0};
    snprintf(mqttPortStr, sizeof(mqttPortStr), "%u", static_cast<unsigned int>(mqttPort));

    int rowY = firstRowY;
    drawConfigRow("WiFi SSID", wifiSsid, rowY); rowY += rowStep;
    drawConfigRow("WiFi jelszó", wifiPassword, rowY); rowY += rowStep;
    drawConfigRow("MQTT szerver", mqttServer, rowY); rowY += rowStep;
    drawConfigRow("MQTT port", mqttPortStr, rowY); rowY += rowStep;
    drawConfigRow("Indulások téma", mqttTopicDepartures, rowY); rowY += rowStep;
    drawConfigRow("Időjárás téma", mqttTopicWeather, rowY); rowY += rowStep;
    drawConfigRow("AP SSID", apSsid, rowY); rowY += rowStep;
    drawConfigRow("AP jelszó", apPassword, rowY);

    g_epaper.update();
    xSemaphoreGive(g_displayMutex);
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
            drawSleepingIcon(centerX, centerY, 128, 128, (uint16_t *)Bus_WhiteBG_128x128, EINK_BLACK);
        }
        else
        {
            drawSleepingIcon(centerX, centerY, 128, 128, (uint16_t *)Train_WhiteBG_128x128, EINK_BLACK);
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
            drawStringUtf8(routeText, textX, bottomLineY, 12);
        }

        char etaText[8] = {0};
        if (departures[i].timestamp > 0UL)
        {
            displayutil::formatDepartureTime(departures[i].timestamp, etaText, sizeof(etaText));
        }
        else
        {
            displayutil::formatDepartureEtaMinutes(departures[i].minutes, etaText, sizeof(etaText));
        }

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
    drawStringUtf8("Holnap", WEATHER_SECTION_X + 210, WAEATHER_SECTION_Y + 16, 16);

    g_epaper.setTextSize(2);
    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_WHITE, EINK_BLUE, true);
    drawStringUtf8("Ma", WEATHER_SECTION_X + 15, WAEATHER_SECTION_Y + 5, 24);

    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_BLACK, EINK_YELLOW, true);
    /*displayutil::drawMonochromeSprite(g_epaper,
                                      BUS_SECTION_X + 14,
                                      BUS_SECTION_Y + 8,
                                      SPRITE_BUS_16X12,
                                      SPRITE_ICON_WIDTH,
                                      SPRITE_ICON_HEIGHT,
                                      2,
                                      EINK_BLACK);*/

    uint16_t * busIconSpriteData = (uint16_t *)Bus_YellowBG_32x32;
    if (busIconSpriteData != nullptr)
    {
        g_epaper.pushImage(BUS_SECTION_X + 5, BUS_SECTION_Y + 6, 32, 32, (uint16_t *)busIconSpriteData);
    }

    drawStringUtf8("BUSZ INDULÁSOK", BUS_SECTION_X + 36, BUS_SECTION_Y + 7, 32);

    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_WHITE, EINK_BLUE, true);

    uint16_t * trainIconSpriteData = (uint16_t *)Train_BlueBG_Inverted_32x32;
    if (trainIconSpriteData != nullptr)
    {
        g_epaper.pushImage(TRAIN_SECTION_X + 5, TRAIN_SECTION_Y + 6, 32, 32, (uint16_t *)trainIconSpriteData);
    }

    drawStringUtf8("VONAT INDULÁSOK", TRAIN_SECTION_X + 36, TRAIN_SECTION_Y + 8, 32);

    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_BLACK, EINK_WHITE, true);
    drawStringUtf8("IDŐJÁRÁS ÉS MENETREND", STATUS_SECTION_X + 5, STATUS_SECTION_Y + 5, 32);

    g_epaper.setTextSize(1);
    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_BLACK, EINK_WHITE, true);
    drawStringUtf8("Frissítve:", STATUS_SECTION_X + 5, STATUS_SECTION_Y + 35, 16);

    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_WHITE, EINK_BLUE, true);
    drawStringUtf8("Pilisvörösvár", TRAIN_SECTION_X + 370, TRAIN_SECTION_Y + 14, 16);

    g_epaper.setRotation(3);
    g_epaper.setTextColor(EINK_BLACK, EINK_YELLOW, true);
    drawStringUtf8("Pilisszentiván - PEVDI", BUS_SECTION_X + 311, BUS_SECTION_Y + 14, 16);

    // Top-right MQTT status: last refresh time and connection indicator.
    drawTopRightStatus();
}

void displayTask(void* /*pvParameters*/)
{
    TickType_t lastDataRefreshTick = 0;
    bool hasSeenDepartureData = false;
    bool hasSeenWeatherData = false;

    // One time setup
    if (takeDisplayMutex(pdMS_TO_TICKS(500)))
    {
        displayEmptyBackground();
        g_epaper.update();
        xSemaphoreGive(g_displayMutex);
    }

    // Wait for display notifications and refresh only when requested.
    for(;;)
    {
        uint32_t notifyValue = 0;
        xTaskNotifyWait(0, UINT32_MAX, &notifyValue, portMAX_DELAY);

        const bool refreshData = (notifyValue & DISPLAY_NOTIFY_DATA) != 0;
        bool shouldRenderData = false;
        bool currentDepartureReady = false;
        bool currentWeatherReady = false;

        int trainCount = 0;
        int busCount   = 0;

        if (refreshData && xSemaphoreTake(g_departuresMutex, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            trainCount = g_trainCount;
            if (trainCount > MAX_DEPARTURES) trainCount = MAX_DEPARTURES;

            busCount = g_busCount;
            if (busCount > MAX_DEPARTURES) busCount = MAX_DEPARTURES;

            currentDepartureReady = g_departuresValid;

            for (int i = 0; i < trainCount; ++i) s_trainCopy[i] = g_trainDepartures[i];
            for (int i = trainCount; i < MAX_DEPARTURES; ++i) s_trainCopy[i] = {};

            for (int i = 0; i < busCount; ++i)   s_busCopy[i]   = g_busDepartures[i];
            for (int i = busCount; i < MAX_DEPARTURES; ++i) s_busCopy[i] = {};

            xSemaphoreGive(g_departuresMutex);
        }

        if (refreshData && xSemaphoreTake(g_weatherMutex, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            s_weatherValidCopy = g_weatherValid;
            currentWeatherReady = g_weatherValid;
            if (s_weatherValidCopy)
            {
                s_weatherCopy = g_weatherData;
            }
            else
            {
                s_weatherCopy = {};
            }
            xSemaphoreGive(g_weatherMutex);
        }

        if (refreshData)
        {
            const TickType_t nowTick = xTaskGetTickCount();

            const bool departureJustArrived = currentDepartureReady && !hasSeenDepartureData;
            const bool weatherJustArrived = currentWeatherReady && !hasSeenWeatherData;
            const bool initialArrival = departureJustArrived || weatherJustArrived;

            if (departureJustArrived)
            {
                hasSeenDepartureData = true;
            }
            if (weatherJustArrived)
            {
                hasSeenWeatherData = true;
            }

            const TickType_t refreshIntervalTicks = pdMS_TO_TICKS(DISPLAY_DATA_REFRESH_INTERVAL_MS);
            const bool intervalElapsed =
                (lastDataRefreshTick == 0)
                || ((nowTick - lastDataRefreshTick) >= refreshIntervalTicks);

            shouldRenderData = initialArrival || intervalElapsed;

            if (shouldRenderData)
            {
                lastDataRefreshTick = nowTick;
            }
        }

        const bool shouldUpdate = shouldRenderData;

        if (shouldUpdate && takeDisplayMutex(pdMS_TO_TICKS(500)))
        {
            if (shouldRenderData)
            {
                drawWeatherCards(&s_weatherCopy, s_weatherValidCopy);
                displayLineData(s_busCopy, busCount, BUS_SECTION_X, BUS_SECTION_Y + 43, EINK_YELLOW);
                displayLineData(s_trainCopy, trainCount, TRAIN_SECTION_X, TRAIN_SECTION_Y + 43, EINK_BLUE);
                drawTopRightStatus();
            }

            g_epaper.update();
            xSemaphoreGive(g_displayMutex);
        }
    }
}

void displayNotifyDataChanged()
{
    if (g_displayTaskHandle != nullptr)
    {
        xTaskNotify(g_displayTaskHandle, DISPLAY_NOTIFY_DATA, eSetBits);
    }
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