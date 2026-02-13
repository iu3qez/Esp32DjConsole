#pragma once

#include "esp_err.h"

/**
 * Status LED colors for visual debugging.
 * Uses onboard WS2812 RGB LED on GPIO48.
 */
typedef enum {
    LED_OFF = 0,
    LED_RED,        // Error / disconnected
    LED_GREEN,      // Connected and running
    LED_BLUE,       // AP mode / setup
    LED_YELLOW,     // Connecting / working
    LED_PURPLE,     // USB device found
    LED_CYAN,       // TCI/CAT connected
    LED_WHITE,      // Transmitting
} led_color_t;

/**
 * Initialize the RGB LED driver.
 */
esp_err_t status_led_init(void);

/**
 * Set the LED to a solid color.
 */
esp_err_t status_led_set(led_color_t color);

/**
 * Blink the LED (non-blocking, starts a repeating timer).
 * interval_ms: blink period in milliseconds.
 * Pass 0 to stop blinking (returns to solid).
 */
esp_err_t status_led_blink(led_color_t color, uint32_t interval_ms);
