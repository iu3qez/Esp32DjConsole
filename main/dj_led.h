#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * LED driver for Hercules DJ Console MP3 e2.
 *
 * Protocol: USB bulk OUT, 3-byte MIDI packets:
 *   {0x90, note, velocity}  velocity=0x7F=on, 0x00=off
 *   Notes 48+ = blinking version of (note - 48)
 *   {0xB0, 0x7F, 0x7F} = reset LED controller
 */

// LED note assignments - Deck A
#define LED_N1_A            1
#define LED_N2_A            2
#define LED_N3_A            3
#define LED_N4_A            4
#define LED_N5_A            5
#define LED_N6_A            6
#define LED_N7_A            7
#define LED_N8_A            8
#define LED_PITCHDOWN_A    10
#define LED_PITCHUP_A      11
#define LED_CUE_A          14
#define LED_PLAY_A         15
#define LED_LISTEN_A       16
#define LED_SYNC_A         18
#define LED_MASTERTEMPO_A  19

// LED note assignments - Deck B
#define LED_N1_B           20
#define LED_N2_B           21
#define LED_N3_B           22
#define LED_N4_B           23
#define LED_N5_B           24
#define LED_N6_B           25
#define LED_N7_B           26
#define LED_N8_B           27
#define LED_PITCHDOWN_B    30
#define LED_PITCHUP_B      31
#define LED_CUE_B          34
#define LED_PLAY_B         35
#define LED_LISTEN_B       36
#define LED_SYNC_B         38
#define LED_MASTERTEMPO_B  39

// LED note assignments - Global
#define LED_UP             40
#define LED_DOWN           41
#define LED_SCRATCH        45
#define LED_AUTOMIX        46

#define LED_NOTE_MAX       46
#define LED_BLINK_OFFSET   48  // note + 48 = blinking version

// LED state values
#define LED_STATE_OFF       0
#define LED_STATE_ON        1
#define LED_STATE_BLINK     2

/**
 * Initialize LED driver. Resets controller and turns all LEDs off.
 * Call after USB device is connected.
 */
void dj_led_init(void);

/**
 * Set an LED on or off.
 * @param note  LED note number (use LED_* defines)
 * @param on    true=on, false=off
 */
void dj_led_set(uint8_t note, bool on);

/**
 * Set an LED to blink or stop blinking.
 * @param note   LED note number
 * @param blink  true=blink, false=off
 */
void dj_led_blink(uint8_t note, bool blink);

/**
 * Turn all LEDs off.
 */
void dj_led_all_off(void);

/**
 * Get current state of an LED.
 * @param note  LED note number
 * @return LED_STATE_OFF, LED_STATE_ON, or LED_STATE_BLINK
 */
uint8_t dj_led_get(uint8_t note);

/**
 * Get all LED states as an array.
 * @return pointer to internal state array of size LED_NOTE_MAX+1
 */
const uint8_t *dj_led_get_all(void);

/**
 * Run a test sequence: sweep all LEDs on then off.
 * Blocking - takes ~2 seconds.
 */
void dj_led_test(void);
