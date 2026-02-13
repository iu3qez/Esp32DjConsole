#include "status_led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "led";

#define LED_GPIO    48
#define LED_COUNT   1

static led_strip_handle_t s_strip = NULL;
static TimerHandle_t s_blink_timer = NULL;
static led_color_t s_blink_color = LED_OFF;
static bool s_blink_on = false;

typedef struct {
    uint8_t r, g, b;
} rgb_t;

static rgb_t color_to_rgb(led_color_t color)
{
    // Keep brightness low (max ~30) to not be blinding
    switch (color) {
        case LED_RED:    return (rgb_t){30, 0, 0};
        case LED_GREEN:  return (rgb_t){0, 30, 0};
        case LED_BLUE:   return (rgb_t){0, 0, 30};
        case LED_YELLOW: return (rgb_t){30, 20, 0};
        case LED_PURPLE: return (rgb_t){20, 0, 30};
        case LED_CYAN:   return (rgb_t){0, 25, 25};
        case LED_WHITE:  return (rgb_t){20, 20, 20};
        default:         return (rgb_t){0, 0, 0};
    }
}

static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return;
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

static void blink_timer_cb(TimerHandle_t timer)
{
    s_blink_on = !s_blink_on;
    if (s_blink_on) {
        rgb_t c = color_to_rgb(s_blink_color);
        set_rgb(c.r, c.g, c.b);
    } else {
        set_rgb(0, 0, 0);
    }
}

esp_err_t status_led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10 MHz
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "RGB LED initialized on GPIO%d", LED_GPIO);
    return ESP_OK;
}

esp_err_t status_led_set(led_color_t color)
{
    // Stop any active blinking
    if (s_blink_timer) {
        xTimerStop(s_blink_timer, 0);
    }

    rgb_t c = color_to_rgb(color);
    set_rgb(c.r, c.g, c.b);
    return ESP_OK;
}

esp_err_t status_led_blink(led_color_t color, uint32_t interval_ms)
{
    s_blink_color = color;

    if (interval_ms == 0) {
        // Stop blinking
        if (s_blink_timer) {
            xTimerStop(s_blink_timer, 0);
        }
        return status_led_set(color);
    }

    if (!s_blink_timer) {
        s_blink_timer = xTimerCreate("led_blink", pdMS_TO_TICKS(interval_ms / 2),
                                      pdTRUE, NULL, blink_timer_cb);
    } else {
        xTimerChangePeriod(s_blink_timer, pdMS_TO_TICKS(interval_ms / 2), 0);
    }

    s_blink_on = true;
    xTimerStart(s_blink_timer, 0);
    return ESP_OK;
}
