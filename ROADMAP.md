# ESP32 DJ Console - Roadmap

## Current Focus
**LED control system testing and hardware validation**
- LED sweep test works, debugging single LED toggle via web UI
- Started: 2026-02-15

## Completed
- [x] Add LED send logging and fix blink/on state transitions (2026-02-15) `016e6a5`
- [x] USB host driver for Hercules DJ Console MP3 e2 (2026-02-13)
- [x] Kenwood CAT TCP client for Thetis (2026-02-13)
- [x] Mapping engine with 80+ Thetis command database (2026-02-14)
- [x] MIDI-learn mode via WebSocket (2026-02-14)
- [x] Svelte web GUI (Dashboard, Mappings, Config, Debug pages) (2026-02-14)
- [x] SPIFFS-based mapping persistence with download/upload (2026-02-14)
- [x] VFO frequency tracking with velocity-scaled tuning (2026-02-14)
- [x] LED control system - USB bulk OUT, driver, REST API, frontend (2026-02-15)
- [x] TCI protocol removal in favor of CAT-only (2026-02-14)

## In Progress
- [ ] Debug single LED toggle (sweep works, individual set doesn't)
- [ ] Test mapping engine toggle -> LED feedback on hardware

## Planned
- [ ] Verify CAT commands against Thetis documentation (especially ZZBS band select)
- [ ] Add learn_timeout WebSocket notification to frontend
- [ ] Delete orphaned tci_client.c/h files
- [ ] Clean up tmp/ reference files
- [ ] Add LED flash-on-press for non-toggle button commands
- [ ] CAT response-driven LED state (e.g., TX state from Thetis updates LED)
- [ ] WiFi AP mode captive portal improvements
- [ ] OTA firmware update support

## Recent Planning Sessions
_Planning sessions will be recorded here automatically._
