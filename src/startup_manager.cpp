#include "startup_manager.h"

// ── Static member definition ──────────────────────────────────────────────────
bool StartupManager::s_configMode = false;

// ── Public API ────────────────────────────────────────────────────────────────

void StartupManager::detect()
{
    // Configure button pin with internal pull-up.
    // Button is expected to connect pin to GND when pressed.
    pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);

    // Short settle time so the pull-up stabilises before first read.
    delay(10);

    // Debounce: sample the pin DEBOUNCE_SAMPLES times over DEBOUNCE_HOLD_MS.
    // All samples must be LOW (pressed) to confirm deliberate intent.
    const uint32_t intervalMs = DEBOUNCE_HOLD_MS / DEBOUNCE_SAMPLES;
    uint8_t lowCount = 0;

    for (uint8_t i = 0; i < DEBOUNCE_SAMPLES; ++i)
    {
        if (digitalRead(CONFIG_BUTTON_PIN) == LOW)
        {
            ++lowCount;
        }
        delay(intervalMs);
    }

    s_configMode = (lowCount == DEBOUNCE_SAMPLES);

    if (s_configMode)
    {
        Serial.println("[STARTUP] Config button held — booting in CONFIG mode.");
    }
    else
    {
        Serial.println("[STARTUP] Config button not held — booting in NORMAL mode.");
    }
}

bool StartupManager::isConfigMode()
{
    return s_configMode;
}

bool StartupManager::isNormalMode()
{
    return !s_configMode;
}
