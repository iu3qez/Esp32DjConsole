#include "dj_led.h"
#include "usb_dj_host.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "dj_led";

static uint8_t s_led_state[LED_NOTE_MAX + 1];

// All valid LED notes (skip gaps in the note map)
static const uint8_t s_valid_notes[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 14, 15, 16, 18, 19,  // Deck A
    20, 21, 22, 23, 24, 25, 26, 27, 30, 31, 34, 35, 36, 38, 39,  // Deck B
    40, 41, 45, 46,  // Global
};
#define VALID_NOTE_COUNT (sizeof(s_valid_notes) / sizeof(s_valid_notes[0]))

static void send_note(uint8_t note, uint8_t velocity)
{
    uint8_t pkt[3] = {0x90, note, velocity};
    esp_err_t err = usb_dj_host_send(pkt, sizeof(pkt));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LED send note=%d vel=0x%02X: %s",
                 note, velocity, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "LED note=%d vel=0x%02X OK", note, velocity);
    }
}

void dj_led_init(void)
{
    memset(s_led_state, LED_STATE_OFF, sizeof(s_led_state));

    // Reset LED controller
    uint8_t reset_cmd[] = {0xB0, 0x7F, 0x7F};
    usb_dj_host_send(reset_cmd, sizeof(reset_cmd));
    vTaskDelay(pdMS_TO_TICKS(20));

    // All off
    dj_led_all_off();
    ESP_LOGI(TAG, "LED driver initialized (%d LEDs)", VALID_NOTE_COUNT);
}

void dj_led_set(uint8_t note, bool on)
{
    if (note > LED_NOTE_MAX) return;

    uint8_t old_state = s_led_state[note];
    uint8_t new_state = on ? LED_STATE_ON : LED_STATE_OFF;
    if (old_state == new_state) return;

    s_led_state[note] = new_state;

    // If transitioning from blink, turn off blink note first
    if (old_state == LED_STATE_BLINK) {
        send_note(note + LED_BLINK_OFFSET, 0x00);
    }

    send_note(note, on ? 0x7F : 0x00);
}

void dj_led_blink(uint8_t note, bool blink)
{
    if (note > LED_NOTE_MAX) return;

    uint8_t old_state = s_led_state[note];
    uint8_t new_state = blink ? LED_STATE_BLINK : LED_STATE_OFF;
    if (old_state == new_state) return;

    s_led_state[note] = new_state;

    if (blink) {
        // Turn off solid first, then send blink note
        send_note(note, 0x00);
        send_note(note + LED_BLINK_OFFSET, 0x7F);
    } else {
        send_note(note + LED_BLINK_OFFSET, 0x00);
        send_note(note, 0x00);
    }
}

void dj_led_all_off(void)
{
    for (int i = 0; i < (int)VALID_NOTE_COUNT; i++) {
        uint8_t note = s_valid_notes[i];
        send_note(note, 0x00);
        send_note(note + LED_BLINK_OFFSET, 0x00);
        s_led_state[note] = LED_STATE_OFF;
    }
}

uint8_t dj_led_get(uint8_t note)
{
    if (note > LED_NOTE_MAX) return LED_STATE_OFF;
    return s_led_state[note];
}

const uint8_t *dj_led_get_all(void)
{
    return s_led_state;
}

void dj_led_test(void)
{
    ESP_LOGI(TAG, "Running LED test sequence...");

    // Sweep on
    for (int i = 0; i < (int)VALID_NOTE_COUNT; i++) {
        send_note(s_valid_notes[i], 0x7F);
        s_led_state[s_valid_notes[i]] = LED_STATE_ON;
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    // Sweep off
    for (int i = 0; i < (int)VALID_NOTE_COUNT; i++) {
        send_note(s_valid_notes[i], 0x00);
        s_led_state[s_valid_notes[i]] = LED_STATE_OFF;
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    ESP_LOGI(TAG, "LED test complete");
}
