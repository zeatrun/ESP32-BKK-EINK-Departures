#include "time_manager.h"
#include "display_manager.h"
#include "configuration.h"

#include <esp_sntp.h>
#include <time.h>

// Matches the bit defined in wifi_manager.cpp
#define WIFI_CONNECTED_BIT BIT0

#define NTP_SERVER         "pool.ntp.org"
#define POLL_INTERVAL_MS   1000
#define NO_SYNC_TIMEOUT_MS (2 * 60 * 1000)

static EventGroupHandle_t s_wifiEventGroup = nullptr;
static volatile bool      s_synced         = false;
static volatile bool      s_syncLost       = false;

// ── Public getters ────────────────────────────────────────────────────────────

bool timeManagerIsSynced()
{
    return s_synced;
}

bool timeManagerIsSyncLost()
{
    return s_syncLost;
}

bool timeManagerGetTime(struct tm* outTime)
{
    if (!s_synced || outTime == nullptr)
    {
        return false;
    }

    const time_t now = time(nullptr);
    return localtime_r(&now, outTime) != nullptr;
}

// ── NTP callback (called from the lwIP / SNTP context) ───────────────────────

static void onNtpSync(struct timeval* /*tv*/)
{
    s_synced   = true;
    s_syncLost = false;
    Serial.println("[Time] NTP sync OK.");
}

// ── Task ─────────────────────────────────────────────────────────────────────

static void timeTask(void* /*pvParameters*/)
{
    int        lastMinute = -1;
    TickType_t wifiLostAt = 0;
    bool       prevWifiUp = false;

    sntp_set_time_sync_notification_cb(onNtpSync);

    // configTzTime accepts a POSIX TZ string and handles DST transitions
    // automatically. The timezone is taken from g_config so it can later be
    // changed by the user through the config-mode web interface.
    configTzTime(g_config.timezone(), NTP_SERVER);

    for (;;)
    {
        const EventBits_t bits   = xEventGroupGetBits(s_wifiEventGroup);
        const bool        wifiUp = (bits & WIFI_CONNECTED_BIT) != 0;

        // ── WiFi state changes ────────────────────────────────────────────
        if (wifiUp)
        {
            if (!prevWifiUp && wifiLostAt != 0)
            {
                // WiFi just came back – force an immediate re-sync
                Serial.println("[Time] WiFi restored, forcing NTP re-sync.");
                sntp_restart();
            }

            wifiLostAt = 0;
            s_syncLost = false;
        }
        else
        {
            if (wifiLostAt == 0)
            {
                wifiLostAt = xTaskGetTickCount();
            }
            else if (!s_syncLost)
            {
                const TickType_t elapsed = xTaskGetTickCount() - wifiLostAt;
                if (elapsed >= pdMS_TO_TICKS(NO_SYNC_TIMEOUT_MS))
                {
                    s_syncLost = true;
                    Serial.println("[Time] Sync lost: no WiFi for 2 minutes.");
                }
            }
        }

        prevWifiUp = wifiUp;

        // ── Minute-change notification ────────────────────────────────────
        // The ESP counts time internally even without WiFi once synced.
        if (s_synced)
        {
            struct tm now = {};
            const time_t t = time(nullptr);

            if (localtime_r(&t, &now) != nullptr)
            {
                if (lastMinute == -1)
                {
                    // Record the current minute on first read.
                    lastMinute = now.tm_min;
                }
                else if (now.tm_min != lastMinute)
                {
                    lastMinute = now.tm_min;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void timeManagerInit(EventGroupHandle_t wifiEventGroup)
{
    s_wifiEventGroup = wifiEventGroup;
}

void timeManagerStart()
{
    xTaskCreatePinnedToCore(
        timeTask,
        "TimeTask",
        4096,
        nullptr,
        1,          // priority
        nullptr,
        0           // core 0 – with networking
    );
}
