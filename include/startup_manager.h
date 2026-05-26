#pragma once

#include <Arduino.h>

/**
 * @brief Boot-time mode selector.
 *
 * Reads a physical button on GPIO2 (D1 / A1 on XIAO ESP32S3) at startup.
 * If the button is held for at least DEBOUNCE_HOLD_MS milliseconds the device
 * boots into **config mode**; otherwise it starts in **normal mode**.
 *
 * Usage (call once, before any manager is initialised):
 *
 *   StartupManager::detect();
 *   if (StartupManager::isConfigMode()) { ... }
 */
class StartupManager
{
public:
    /** GPIO pin wired to the config button (XIAO ESP32S3 D1 / A1). */
    static constexpr uint8_t CONFIG_BUTTON_PIN = 2;

    /**
     * How long (ms) the button must be held to trigger config mode.
     * Also the polling window used for debounce.
     */
    static constexpr uint32_t DEBOUNCE_HOLD_MS = 50;

    /**
     * Samples collected during the debounce window.
     * All samples must read LOW (button pressed, assuming INPUT_PULLUP) to
     * confirm a deliberate press.
     */
    static constexpr uint8_t DEBOUNCE_SAMPLES = 5;

    // No construction — pure static utility class.
    StartupManager() = delete;

    /**
     * Configure the pin, sample the button with debounce and decide the boot
     * mode. Must be called once at the very start of setup(), before any other
     * manager is initialised.
     */
    static void detect();

    /** Returns true if config mode was requested at boot. */
    static bool isConfigMode();

    /** Returns true if the device is in normal (MQTT + display) mode. */
    static bool isNormalMode();

private:
    static bool s_configMode;
};
