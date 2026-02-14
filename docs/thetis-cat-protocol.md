# Thetis CAT Protocol Reference

How the ESP32 DJ Console communicates with Thetis SDR software via Kenwood CAT over TCP.

## Transport

- **Protocol:** Plain TCP socket (no WebSocket, no TLS)
- **Default port:** 31001 (from Thetis `TCPIPcatServer.cs`)
- **Direction:** Bidirectional. ESP32 sends commands, Thetis sends responses.
- **Reconnect:** Auto-reconnect with 3s delay on disconnect
- **Keepalive:** S-meter query (`ZZSM0;`) sent every 30s to prevent Thetis timeout

### Thetis Setup

In Thetis, enable the CAT TCP server:

1. **Setup > CAT Control** tab
2. Enable **"TCP Server"**
3. Port defaults to **31001** (must match ESP32 config)
4. The server accepts multiple simultaneous clients

## Command Format

All commands are ASCII strings terminated by semicolon (`;`).

```
COMMAND[parameters];
```

### Standard Kenwood Commands (2-letter prefix)

| Command | Description | Example |
|---------|-------------|---------|
| `FA` | VFO A frequency (11 digits, Hz) | `FA00014074000;` |
| `FB` | VFO B frequency (11 digits, Hz) | `FB00007074000;` |
| `UP` | Tune VFO up one step | `UP;` |
| `DN` | Tune VFO down one step | `DN;` |
| `MD` | Operating mode | `MD01;` (USB) |
| `AG` | Audio gain | `AG0050;` |
| `PC` | TX power | `PC050;` |
| `SM` | S-meter query | `SM0;` |
| `TX` | Transmit on/off | `TX1;` |

### ZZ Extended Commands (4-letter prefix)

Thetis extends the Kenwood protocol with `ZZ` prefixed commands. These provide access to all Thetis features.

**Query:** Send the command without parameters to read the current value:
```
ZZFA;       -> Response: ZZFA00014074000;
ZZMD;       -> Response: ZZMD01;
```

**Set:** Send the command with zero-padded parameters:
```
ZZFA00014074000;    Set VFO A to 14.074 MHz
ZZMD01;             Set mode to USB
ZZAG050;            Set AF gain to 50
```

**Error response:** `?;` means the command was not recognized or the parameter was invalid.

## Execution Types

The mapping engine uses five execution types to translate DJ control changes into CAT commands:

### CMD_CAT_BUTTON
Sends a fixed command string on button press (ignores release).

```c
// Band Up: sends "ZZBU;" on press
{ 200, "Band Up", CAT_BAND, CMD_CAT_BUTTON, "ZZBU", NULL, 0, 0, 0 }

// 20m band: sends "ZZBS020;" on press (3-digit param, value_min=20)
{ 207, "20m", CAT_BAND, CMD_CAT_BUTTON, "ZZBS", NULL, 3, 20, 20 }
```

Format: `{prefix}{zero-padded value_min};` or just `{prefix};` if value_digits=0.

### CMD_CAT_TOGGLE
Tracks on/off state internally. Each press toggles and sends the new state.

```c
// MOX toggle: sends "ZZTX0;" or "ZZTX1;" alternating
{ 400, "MOX On/Off", CAT_TX, CMD_CAT_TOGGLE, "ZZTX", NULL, 1, 0, 1 }
//                                            cmd    -     digits min max
```

Format: `{prefix}{zero-padded value_min};` (off) or `{prefix}{zero-padded value_max};` (on).

32 toggle state slots are available (enough for all toggle commands).

### CMD_CAT_SET
Scales the DJ control's 0-255 range to the command's value_min..value_max range.

```c
// AF Gain: maps dial 0-255 to "ZZAG000;".."ZZAG100;"
{ 500, "AF Gain", CAT_AUDIO, CMD_CAT_SET, "ZZAG", NULL, 3, 0, 100 }
```

Format: `{prefix}{zero-padded scaled_value};`

### CMD_CAT_FREQ
For VFO tuning encoders. Uses the same read-modify-write approach as midi2cat's `ChangeFreqVfoA`/`ChangeFreqVfoB` (see `Midi2CatCommands.cs:1573-1741`):

1. **Query current freq** on connect: sends `ZZFA;`/`ZZFB;`, response updates local tracking
2. **Query tuning step** on connect: sends `ZZAC;`, response maps step index (0-25) to Hz via lookup table
3. **On encoder change**: computes delta, applies velocity scaling, snaps to step boundary, sends absolute 11-digit frequency

```c
// VFO A: sends "ZZFA{11 digits};" with velocity-scaled step from Thetis ZZAC
{ 100, "VFO A Tune", CAT_VFO, CMD_CAT_FREQ, "ZZFA", NULL, 11, 0, 0 }
// VFO B: same logic, sends "ZZFB{11 digits};"
{ 101, "VFO B Tune", CAT_VFO, CMD_CAT_FREQ, "ZZFB", NULL, 11, 0, 0 }
```

**Tuning step (ZZAC):** The step size comes from Thetis (queried on connect). The ZZAC index maps to Hz:

| Index | Hz | Index | Hz | Index | Hz |
|-------|-----|-------|------|-------|--------|
| 0 | 1 | 9 | 2,000 | 18 | 25,000 |
| 1 | 2 | 10 | 2,500 | 19 | 30,000 |
| 2 | 10 | 11 | 5,000 | 20 | 50,000 |
| 3 | 25 | 12 | 6,250 | 21 | 100,000 |
| 4 | 50 | 13 | 9,000 | 22 | 250,000 |
| 5 | 100 | 14 | 10,000 | 23 | 500,000 |
| 6 | 250 | 15 | 12,500 | 24 | 1,000,000 |
| 7 | 500 | 16 | 15,000 | 25 | 10,000,000 |
| 8 | 1,000 | 17 | 20,000 | | |

**Velocity scaling:** Wheel speed determines the step multiplier (1x to 10x):
- Slow wheel (delta=1): 1x base step (fine tuning)
- Fast wheel (delta>=5): 10x base step (coarse tuning)
- Linear interpolation in between

**Sync:** VFO frequencies and tuning step are queried from Thetis on every CAT connect. If Thetis changes the frequency externally, it will be re-synced on the next reconnect.

### CMD_CAT_WHEEL
For relative increment/decrement commands. Sends one of two CAT commands depending on the encoder direction.

```c
// RIT tune: sends "ZZRU;" for clockwise, "ZZRD;" for counter-clockwise
{ 906, "RIT Tune", CAT_SPLIT_RIT, CMD_CAT_WHEEL, "ZZRU", "ZZRD", 0, 0, 0 }
```

For large deltas (fast jog wheel rotation), the command is sent multiple times (up to 10x per event).

## Command Database

All commands available for mapping, organized by category. The `ID` is stable across firmware versions and is used in mapping JSON files.

### VFO (100-119)

| ID | Name | Type | CAT | Notes |
|----|------|------|-----|-------|
| 100 | VFO A Tune | FREQ | `ZZFA` | Velocity-scaled, step from ZZAC |
| 101 | VFO B Tune | FREQ | `ZZFB` | Velocity-scaled, step from ZZAC |
| 102 | VFO A -> B | BUTTON | `ZZAB` | Copy A frequency to B |
| 103 | VFO B -> A | BUTTON | `ZZBA` | Copy B frequency to A |
| 104 | VFO Swap | BUTTON | `ZZVS` | Exchange A and B |
| 105 | VFO A Up 100kHz | BUTTON | `ZZAU` | Jump A up 100 kHz |
| 106 | VFO A Down 100kHz | BUTTON | `ZZAD` | Jump A down 100 kHz |
| 107 | VFO B Up 100kHz | BUTTON | `ZZBY` | Jump B up 100 kHz |
| 108 | VFO B Down 100kHz | BUTTON | `ZZBB` | Jump B down 100 kHz |
| 109 | VFO Sync | TOGGLE | `ZZSY` | Lock B to A frequency |
| 110 | Tuning Step Up | BUTTON | `ZZSU` | Increase Thetis tuning step size |
| 111 | Tuning Step Down | BUTTON | `ZZSD` | Decrease Thetis tuning step size |
| 112 | Multi Step VFO A | WHEEL | `UP`/`DN` | Uses Kenwood UP/DN (Thetis step size) |
| 113 | Lock VFO A | TOGGLE | `ZZLA` | Prevent VFO A changes |
| 114 | Lock VFO B | TOGGLE | `ZZLB` | Prevent VFO B changes |

### Band (200-219)

| ID | Name | Type | CAT | Notes |
|----|------|------|-----|-------|
| 200 | Band Up | BUTTON | `ZZBU` | Next band |
| 201 | Band Down | BUTTON | `ZZBD` | Previous band |
| 202 | 160m | BUTTON | `ZZBS160` | Direct band select (3-digit band number) |
| 203 | 80m | BUTTON | `ZZBS080` | |
| 204 | 60m | BUTTON | `ZZBS060` | |
| 205 | 40m | BUTTON | `ZZBS040` | |
| 206 | 30m | BUTTON | `ZZBS030` | |
| 207 | 20m | BUTTON | `ZZBS020` | |
| 208 | 17m | BUTTON | `ZZBS017` | |
| 209 | 15m | BUTTON | `ZZBS015` | |
| 210 | 12m | BUTTON | `ZZBS012` | |
| 211 | 10m | BUTTON | `ZZBS010` | |
| 212 | 6m | BUTTON | `ZZBS006` | |
| 213 | 2m | BUTTON | `ZZBS002` | |
| 214 | RX2 Band Up | BUTTON | `ZZBE` | |
| 215 | RX2 Band Down | BUTTON | `ZZBF` | |

### Mode (300-319)

| ID | Name | Type | CAT | Notes |
|----|------|------|-----|-------|
| 300 | Mode Next | BUTTON | `ZZMU` | Cycle to next mode |
| 301 | Mode Prev | BUTTON | `ZZML` | Cycle to previous mode |
| 302 | LSB | BUTTON | `ZZMD00` | Direct mode select (2-digit code) |
| 303 | USB | BUTTON | `ZZMD01` | |
| 304 | DSB | BUTTON | `ZZMD02` | |
| 305 | CW Lower | BUTTON | `ZZMD03` | |
| 306 | FM | BUTTON | `ZZMD04` | |
| 307 | AM | BUTTON | `ZZMD05` | |
| 308 | DIGL | BUTTON | `ZZMD06` | FT8/digital lower sideband |
| 309 | CW Upper | BUTTON | `ZZMD07` | |
| 310 | SPEC | BUTTON | `ZZMD08` | Spectrum display only |
| 311 | DIGU | BUTTON | `ZZMD09` | FT8/digital upper sideband |
| 312 | SAM | BUTTON | `ZZMD10` | Synchronous AM |
| 313 | DRM | BUTTON | `ZZMD11` | Digital Radio Mondiale |
| 314 | RX2 Mode Next | BUTTON | `ZZMV` | |
| 315 | RX2 Mode Prev | BUTTON | `ZZMW` | |

### TX (400-419)

| ID | Name | Type | CAT | Notes |
|----|------|------|-----|-------|
| 400 | MOX On/Off | TOGGLE | `ZZTX` | Manual transmit |
| 401 | Tune On/Off | TOGGLE | `ZZTU` | Continuous carrier for antenna tuning |
| 402 | Tuner On/Off | TOGGLE | `ZZOC` | Enable automatic antenna tuner |
| 403 | VOX On/Off | TOGGLE | `ZZVE` | Voice-operated transmit |
| 404 | Two Tone On/Off | TOGGLE | `ZZUT` | Two-tone test signal |
| 405 | PS On/Off | TOGGLE | `ZZLM` | PureSignal linearization |
| 406 | Toggle TX VFO | BUTTON | `ZZSA` | Switch which VFO transmits on |
| 407 | Tuner Bypass | TOGGLE | `ZZOD` | Bypass antenna tuner |
| 408 | External PA On/Off | TOGGLE | `ZZPE` | External power amplifier relay |

### Audio (500-529)

| ID | Name | Type | CAT | Notes |
|----|------|------|-----|-------|
| 500 | AF Gain | SET | `ZZAG` | 0-100, receiver volume |
| 501 | RX2 Volume | SET | `ZZVA` | 0-100, second receiver volume |
| 502 | Mute On/Off | TOGGLE | `ZZMA` | Mute RX1 |
| 503 | RX2 Mute On/Off | TOGGLE | `ZZMB` | Mute RX2 |
| 504 | MON On/Off | TOGGLE | `ZZMO` | TX monitor (hear yourself) |
| 505 | Drive Level | SET | `ZZPC` | 0-100, TX power |
| 506 | Mic Gain | SET | `ZZMG` | 0-100 |
| 507 | RX1 AGC Level | SET | `ZZAR` | 0-120 |
| 508 | RX2 AGC Level | SET | `ZZAS` | 0-120 |
| 509 | DX Level | SET | `ZZDX` | 0-100 |

### Filter (600-619)

| ID | Name | Type | CAT | Notes |
|----|------|------|-----|-------|
| 600 | Filter High | SET | `ZZFH` | 0-20000 Hz, upper passband edge |
| 601 | Filter Low | SET | `ZZFI` | 0-20000 Hz, lower passband edge |
| 602 | Filter Wider | BUTTON | `ZZFW` | Step filter wider |
| 603 | Filter Narrower | BUTTON | `ZZFN` | Step filter narrower |
| 604 | Filter High Wheel | WHEEL | `ZZHU`/`ZZHD` | Relative high cut adjust |
| 605 | Filter Low Wheel | WHEEL | `ZZLU`/`ZZLD` | Relative low cut adjust |
| 606 | RX2 Filter Wider | BUTTON | `ZZFV` | |
| 607 | RX2 Filter Narrower | BUTTON | `ZZFX` | |
| 608 | TX Filter High Whl | WHEEL | `ZZHW`/`ZZHX` | TX filter high cut |
| 609 | TX Filter Low Whl | WHEEL | `ZZLG`/`ZZLH` | TX filter low cut |

### NR/NB (700-729)

| ID | Name | Type | CAT | Notes |
|----|------|------|-----|-------|
| 700 | NB1 On/Off | TOGGLE | `ZZNA` | Noise blanker 1 |
| 701 | NB2 On/Off | TOGGLE | `ZZNB` | Noise blanker 2 |
| 702 | NR On/Off | TOGGLE | `ZZNR` | Noise reduction 1 |
| 703 | NR2 On/Off | TOGGLE | `ZZNS` | Noise reduction 2 |
| 704 | Auto Notch On/Off | TOGGLE | `ZZNT` | Automatic notch filter |
| 705 | SNB On/Off | TOGGLE | `ZZNN` | Spectral noise blanker |
| 706 | Binaural On/Off | TOGGLE | `ZZBI` | Binaural audio |
| 707 | RX2 NB1 On/Off | TOGGLE | `ZZNC` | |
| 708 | RX2 NB2 On/Off | TOGGLE | `ZZND` | |
| 709 | RX2 ANF On/Off | TOGGLE | `ZZNU` | RX2 auto notch |
| 710 | RX2 NR1 On/Off | TOGGLE | `ZZNV` | |
| 711 | RX2 NR2 On/Off | TOGGLE | `ZZNW` | |
| 712 | RX2 SNB On/Off | TOGGLE | `ZZNO` | |

### AGC (800-819)

| ID | Name | Type | CAT | Notes |
|----|------|------|-----|-------|
| 800 | AGC Mode Up | BUTTON | `ZZGU` | Cycle AGC mode (off/long/slow/med/fast) |
| 801 | AGC Mode Down | BUTTON | `ZZGD` | |
| 802 | AGC Level | SET | `ZZGT` | 0-120, AGC threshold |
| 803 | RX2 AGC Mode Up | BUTTON | `ZZGE` | |
| 804 | RX2 AGC Mode Down | BUTTON | `ZZGL` | |

### Split/RIT/XIT (900-919)

| ID | Name | Type | CAT | Notes |
|----|------|------|-----|-------|
| 900 | Split On/Off | TOGGLE | `ZZSP` | TX on VFO B, RX on VFO A |
| 901 | Quick Split | BUTTON | `ZZQS` | Copy A->B and enable split |
| 902 | RIT On/Off | TOGGLE | `ZZRT` | Receiver incremental tuning |
| 903 | XIT On/Off | TOGGLE | `ZZXT` | Transmitter incremental tuning |
| 904 | RIT Clear | BUTTON | `ZZRC` | Zero the RIT offset |
| 905 | XIT Clear | BUTTON | `ZZXC` | Zero the XIT offset |
| 906 | RIT Tune | WHEEL | `ZZRU`/`ZZRD` | Adjust RIT offset |
| 907 | XIT Tune | WHEEL | `ZZXU`/`ZZXD` | Adjust XIT offset |

### CW (1000-1019)

| ID | Name | Type | CAT | Notes |
|----|------|------|-----|-------|
| 1000 | CW Speed | SET | `ZZCS` | 1-60 WPM |
| 1001 | CW Break-In On/Off | TOGGLE | `ZZCB` | Full break-in (QSK) |
| 1002 | CW Sidetone Freq | SET | `ZZCI` | 100-2000 Hz |
| 1003 | CW Speed Inc | WHEEL | `ZZCU`/`ZZCD` | Relative WPM adjust |
| 1004 | CW QSK On/Off | TOGGLE | `ZZCF` | |

### Misc (1100-1139)

| ID | Name | Type | CAT | Notes |
|----|------|------|-----|-------|
| 1100 | Squelch On/Off | TOGGLE | `ZZSQ` | |
| 1101 | Compander On/Off | TOGGLE | `ZZCP` | TX compressor |
| 1102 | RX2 On/Off | TOGGLE | `ZZRX` | Enable second receiver |
| 1103 | Click Tune On/Off | TOGGLE | `ZZCT` | Click-on-waterfall tuning |
| 1104 | Power On/Off | TOGGLE | `ZZPS` | Thetis soft power |
| 1105 | Squelch Level | SET | `ZZSV` | 0-160 |
| 1106 | RX EQ On/Off | TOGGLE | `ZZER` | Receive equalizer |
| 1107 | TX EQ On/Off | TOGGLE | `ZZET` | Transmit equalizer |
| 1108 | DEXP On/Off | TOGGLE | `ZZDA` | Downward expander / noise gate |
| 1109 | Diversity On/Off | TOGGLE | `ZZDB` | Diversity reception |
| 1110 | Display Pan Down | BUTTON | `ZZPD` | Pan waterfall/panadapter left |
| 1111 | Zoom Inc | WHEEL | `ZZZA`/`ZZZB` | Zoom waterfall/panadapter |
| 1112 | Display Mode Next | BUTTON | `ZZDU` | Cycle display layout |
| 1113 | VAC On/Off | TOGGLE | `ZZVC` | Virtual audio cable |
| 1114 | Quick Mode Save | BUTTON | `ZZQM` | Save current mode/freq |
| 1115 | Quick Mode Restore | BUTTON | `ZZQR` | Restore saved mode/freq |
| 1116 | RX2 Squelch On/Off | TOGGLE | `ZZSZ` | |
| 1117 | RX2 CTUN On/Off | TOGGLE | `ZZCO` | RX2 click tune |
| 1118 | APF On/Off | TOGGLE | `ZZAP` | Audio peak filter (CW) |

## Mapping JSON Format

Mappings are stored on SPIFFS at `/www/mappings.json`:

```json
[
  { "c": "Jog_A",   "id": 100, "p": 10   },
  { "c": "Pitch_A", "id": 100, "p": 100  },
  { "c": "Vol_A",   "id": 500            },
  { "c": "Play_A",  "id": 400            },
  { "c": "N1_A",    "id": 202            }
]
```

Fields:
- `c` - DJ control name (e.g., `Jog_A`, `Play_A`, `N1_A`)
- `id` - Command ID from the database above
- `p` - Optional parameter (Hz step for FREQ type, 0/absent = default)

## Data Flow

```
[DJ Console USB] --38-byte state packet--> [USB Host Driver]
                                                |
                                     compare old vs new state
                                                |
                                     mapping_engine_on_control()
                                                |
                                   lookup command by control name
                                                |
                                       execute_command()
                                                |
                              +--------+--------+--------+--------+
                              |        |        |        |        |
                           BUTTON   TOGGLE    SET     FREQ     WHEEL
                              |        |        |        |        |
                              v        v        v        v        v
                           cat_client_send("ZZXX...;")
                                                |
                                          TCP socket
                                                |
                                     [Thetis SDR Software]
```

## Reference

- **Thetis source:** `CATParser.cs` - parses all ZZ commands
- **Thetis source:** `TCPIPcatServer.cs` - TCP server implementation
- **midi2cat source:** `CatCmdDb.cs` - command enum with ~150 entries
- **midi2cat source:** `MidiMessageManager.cs` - MIDI-to-CAT dispatch

The command database in this firmware is derived from the midi2cat `CatCmdDb.cs` enum and verified against the Thetis `CATParser.cs` command list.
