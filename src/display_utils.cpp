#include "display_utils.h"

#include <time.h>

namespace displayutil
{
void drawMonochromeSprite(EPaper& display,
                          int x,
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
                display.drawPixel(x + col, y + row, color);
            }
            else
            {
                display.fillRect(x + (col * scale), y + (row * scale), scale, scale, color);
            }
        }
    }
}

void drawArrowIcon(EPaper& display, int x, int y, bool up, uint16_t color)
{
    if (up)
    {
        display.drawLine(x + 3, y + 7, x + 3, y + 1, color);
        display.drawLine(x + 3, y + 1, x + 1, y + 3, color);
        display.drawLine(x + 3, y + 1, x + 5, y + 3, color);
    }
    else
    {
        display.drawLine(x + 3, y + 1, x + 3, y + 7, color);
        display.drawLine(x + 3, y + 7, x + 1, y + 5, color);
        display.drawLine(x + 3, y + 7, x + 5, y + 5, color);
    }
}

void drawArrowIconScaled(EPaper& display, int x, int y, bool up, uint16_t color, int scale)
{
    if (scale <= 1)
    {
        drawArrowIcon(display, x, y, up, color);
        return;
    }

    auto sx = [x, scale](int v) { return x + (v * scale); };
    auto sy = [y, scale](int v) { return y + (v * scale); };

    if (up)
    {
        display.drawLine(sx(3), sy(7), sx(3), sy(1), color);
        display.drawLine(sx(3), sy(1), sx(1), sy(3), color);
        display.drawLine(sx(3), sy(1), sx(5), sy(3), color);
    }
    else
    {
        display.drawLine(sx(3), sy(1), sx(3), sy(7), color);
        display.drawLine(sx(3), sy(7), sx(1), sy(5), color);
        display.drawLine(sx(3), sy(7), sx(5), sy(5), color);
    }
}

void formatDepartureTime(unsigned long unixTimestamp, char* out, size_t outSize)
{
    if (out == nullptr || outSize < 6)
    {
        return;
    }

    strlcpy(out, "xx:xx", outSize);

    if (unixTimestamp == 0)
    {
        return;
    }

    const time_t departureTs = static_cast<time_t>(unixTimestamp);
    struct tm departureTime = {};
    if (localtime_r(&departureTs, &departureTime) == nullptr)
    {
        return;
    }

    snprintf(out, outSize, "%02d:%02d", departureTime.tm_hour, departureTime.tm_min);
}

void formatTempWithDegree(float tempValue, bool valid, char* out, size_t outSize)
{
    if (out == nullptr || outSize == 0)
    {
        return;
    }

    if (!valid)
    {
        strlcpy(out, "xx\u00b0", outSize);
        return;
    }

    snprintf(out, outSize, "%.0f\u00b0", tempValue);
}

void getHungarianWeekdayLabel(const char* isoDate, char* out, size_t outSize)
{
    if (out == nullptr || outSize == 0)
    {
        return;
    }

    strlcpy(out, "Nap", outSize);

    if (isoDate == nullptr)
    {
        return;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (sscanf(isoDate, "%d-%d-%d", &year, &month, &day) != 3)
    {
        return;
    }

    struct tm dateTm = {};
    dateTm.tm_year = year - 1900;
    dateTm.tm_mon = month - 1;
    dateTm.tm_mday = day;
    dateTm.tm_isdst = -1;

    if (mktime(&dateTm) == static_cast<time_t>(-1))
    {
        return;
    }

    switch (dateTm.tm_wday)
    {
        case 0: strlcpy(out, "Vas\u00e1rnap", outSize); break;
        case 1: strlcpy(out, "H\u00e9tf\u0151", outSize); break;
        case 2: strlcpy(out, "Kedd", outSize); break;
        case 3: strlcpy(out, "Szerda", outSize); break;
        case 4: strlcpy(out, "Cs\u00fct\u00f6rt\u00f6k", outSize); break;
        case 5: strlcpy(out, "P\u00e9ntek", outSize); break;
        case 6: strlcpy(out, "Szombat", outSize); break;
        default: break;
    }
}
}