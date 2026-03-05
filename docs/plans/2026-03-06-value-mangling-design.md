# Value Mangling — Per-Control Scale Factor

## Problem

All CC controls currently use a fixed `value / 2` scaling (0-255 raw -> 0-127 MIDI). Different controls may need different scaling — e.g. a pot that only reaches 135 physically, or an encoder that needs finer resolution.

## Solution

Add a `float scale` field to each `midi_mapping_t`. The firmware applies `scale` before MIDI output:

```
cc_val = clamp(0, 127, (int)(raw_value * scale))
```

Note On/Off mappings ignore `scale` (always velocity 127 on press, 0 on release).

## Data Structure Change

```c
typedef struct {
    uint8_t control_id;
    uint8_t midi_channel;
    uint8_t midi_type;      // MIDI_CC, MIDI_NOTE_ON, etc.
    uint8_t midi_param;     // CC number or note number
    float   scale;          // multiplier, default 0.5 for CC
} midi_mapping_t;
```

Default `scale = 0.5` for CC mappings (preserves current `value / 2` behavior).

## Firmware Changes

### event_engine.c

- `load_defaults()`: set `scale = 0.5f` for all CC mappings
- `event_engine_process()`: replace `value / 2` with `(int)(value * m->scale)`, clamp 0-127
- NVS blob size changes — old blobs without `scale` field get `scale = 0.5f` on load (check blob size vs expected size)

### http_server.c

- `api_mappings_get_handler`: add `"scale"` field to JSON output
- `api_mappings_put_handler`: read `"scale"` from JSON input, default 0.5 if absent

## REST API

### GET /api/mappings

```json
[
  {"control_id": 46, "channel": 0, "type": 176, "param": 46, "scale": 0.5},
  {"control_id": 49, "channel": 0, "type": 176, "param": 49, "scale": 0.75}
]
```

### PUT /api/mappings

Same format. `scale` optional, defaults to 0.5 if omitted.

## Frontend Changes

### Mappings.svelte

- Add a numeric input (step 0.01) for `scale` on each CC mapping row
- Note mappings don't show the scale input
- Save triggers PUT to `/api/mappings`

## Default Values

| Control Type | Default Scale | Effective Range |
|---|---|---|
| Buttons (Note) | N/A | 0 or 127 |
| Dials/Sliders | 0.5 | 0-127 (from 0-255 raw) |
| Encoders | 0.5 | 0-127 (from 0-255 raw) |

## Backward Compatibility

Old NVS blobs have `sizeof(midi_mapping_t)` without the `scale` field (4 bytes per entry). New struct is 8 bytes per entry. On load, if `blob_size == count * 4`, migrate by setting `scale = 0.5f` for all entries.
