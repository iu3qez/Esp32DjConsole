#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        2
#endif

// Device on rhport 0 (FS) — MIDI class
#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     OPT_MODE_FULL_SPEED

// Host on rhport 1 (HS) — vendor bulk for DJ Console
#define CFG_TUH_ENABLED       1
#define CFG_TUH_MAX_SPEED     OPT_MODE_HIGH_SPEED

// Enable user callbacks for tuh_edpt_xfer (vendor endpoints)
#define CFG_TUH_API_EDPT_XFER 1

//--------------------------------------------------------------------
// DEVICE CONFIGURATION — USB MIDI
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

#define CFG_TUD_MIDI          1
#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64

//--------------------------------------------------------------------
// HOST CONFIGURATION
//--------------------------------------------------------------------

#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB           0
#define CFG_TUH_DEVICE_MAX    1
#define CFG_TUH_INTERFACE_MAX 8

#ifdef __cplusplus
 }
#endif

#endif
