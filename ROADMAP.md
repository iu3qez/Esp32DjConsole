# ESP32 DJ Console - Roadmap

## Current Focus

_No current goal. Next planned item will be promoted on next planning session._

## Completed
- [x] Add live CAT command ticker bar showing TX/RX commands (2026-02-15) `653ed7f`
- [x] Add VFO idle-resync to prevent band jumps after Thetis frequency changes (2026-02-15) `eb47f80`
- [x] Fix ZZSF format, encoder filter width, and mapping persistence (2026-02-15) `2059918`
- [x] Fix ZZSF filter width, add encoder-relative SET mode, add CAT console (2026-02-15) `1a30a9d`
- [x] Fix WebSocket socket leak and improve Mappings page (2026-02-15) `b75926d`
- [x] Fix default mapping IDs and overlay user mappings on top of defaults (2026-02-15) `afee184`
- [x] Add CMD_CAT_FILTER_WIDTH exec type for knob-controlled filter width via ZZSF (2026-02-15) `eeeefe1`
- [x] Fix /api/commands JSON truncation by using chunked responses (2026-02-15) `5cc2ab5`
- [x] Update README and roadmap with current project state (2026-02-15) `bb6f479`
- [x] Fix command names, exec_type detection, and add description tooltips (2026-02-15) `a2c6a9f`
- [x] OTA firmware update support (2026-02-15)
- [x] WiFi AP mode captive portal improvements (2026-02-15)
- [x] Bump CPU to 240MHz and enable performance optimizations (2026-02-15) `2b4b155`
- [x] Auto-generate CAT command DB from CATCommands.cs and add toggle state sync (2026-02-15) `ab70e8b`
- [x] CAT response-driven LED state (2026-02-15)
- [x] Add LED flash-on-press for non-toggle button commands (2026-02-15)
- [x] Clean up tmp/ reference files (2026-02-15)
- [x] Delete orphaned tci_client.c/h files (2026-02-15)
- [x] Add learn_timeout WebSocket notification to frontend (2026-02-15)
- [x] Verify CAT commands against Thetis documentation (2026-02-15)
- [x] LED control system testing and hardware validation (2026-02-15)
- [x] Add project roadmap (2026-02-15) `8eb1c61`
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

## Recent Planning Sessions
_Planning sessions will be recorded here automatically._
