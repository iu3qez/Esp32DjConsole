# Value Mangling Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a per-control `float scale` factor to MIDI CC mappings, configurable via REST API and frontend.

**Architecture:** Extend `midi_mapping_t` with a `scale` field. The event engine applies `clamp(0, 127, (int)(raw * scale))` for CC output. NVS migration handles old 4-byte-per-entry blobs. Frontend shows an editable scale input per CC mapping row.

**Tech Stack:** C (ESP-IDF), Svelte 5, cJSON, NVS

---

### Task 1: Add `scale` field to `midi_mapping_t`

**Files:**
- Modify: `src/event_engine.h:17-22`

**Step 1: Add the field**

In `src/event_engine.h`, change `midi_mapping_t` to:

```c
typedef struct {
    uint8_t control_id;
    uint8_t midi_channel;
    uint8_t midi_type;      // MIDI_CC, MIDI_NOTE_ON, etc.
    uint8_t midi_param;     // CC number or note number
    float   scale;          // multiplier applied to CC value, default 0.5
} midi_mapping_t;
```

**Step 2: Commit**

```bash
git add src/event_engine.h
git commit -m "Add scale field to midi_mapping_t"
```

---

### Task 2: Update `load_defaults()` to set scale

**Files:**
- Modify: `src/event_engine.c:17-41`

**Step 1: Add scale to default mappings**

In `load_defaults()`, add `.scale = 0.5f` to the CC mappings. Button mappings get `.scale = 1.0f` (unused but clean):

```c
static void load_defaults(void) {
    s_mapping_count = 0;

    // Buttons -> Note On/Off, note number = control index
    for (uint8_t i = 0; i < 46 && s_mapping_count < MAX_MIDI_MAPPINGS; i++) {
        s_mappings[s_mapping_count++] = (midi_mapping_t){
            .control_id   = i,
            .midi_channel = 0,
            .midi_type    = MIDI_NOTE_ON,
            .midi_param   = i,
            .scale        = 1.0f,
        };
    }

    // Dials, sliders, encoders -> CC, cc number = control index
    for (uint8_t i = 46; i < 59 && s_mapping_count < MAX_MIDI_MAPPINGS; i++) {
        s_mappings[s_mapping_count++] = (midi_mapping_t){
            .control_id   = i,
            .midi_channel = 0,
            .midi_type    = MIDI_CC,
            .midi_param   = i,
            .scale        = 0.5f,
        };
    }

    ESP_LOGI(TAG, "Loaded %d default mappings", s_mapping_count);
}
```

**Step 2: Commit**

```bash
git add src/event_engine.c
git commit -m "Set default scale in load_defaults (0.5 for CC, 1.0 for notes)"
```

---

### Task 3: Apply scale in `event_engine_process()`

**Files:**
- Modify: `src/event_engine.c:69-74`

**Step 1: Replace fixed `/2` with scale multiplication**

Change the `MIDI_CC` case in `event_engine_process()`:

```c
        case MIDI_CC: {
            int scaled = (int)(event->value * m->scale);
            if (scaled < 0) scaled = 0;
            if (scaled > 127) scaled = 127;
            midi_output_send_cc(m->midi_channel, m->midi_param, (uint8_t)scaled);
            break;
        }
```

**Step 2: Build to verify compilation**

```bash
source ~/esp/esp-idf/export.sh && idf.py build
```

Expected: Build succeeds.

**Step 3: Commit**

```bash
git add src/event_engine.c
git commit -m "Apply per-control scale factor in CC output"
```

---

### Task 4: NVS migration for old blobs

**Files:**
- Modify: `src/event_engine.c:133-154` (the `event_engine_load` function)

**Step 1: Add migration logic**

The old struct was 4 bytes per entry (no `scale`). The new struct is 8 bytes (4 bytes + 4 byte float). Detect old format by checking `blob_size == count * 4` and migrate:

```c
esp_err_t event_engine_load(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("midi_map", NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;

    uint8_t count = 0;
    err = nvs_get_u8(nvs, "count", &count);
    if (err != ESP_OK || count > MAX_MIDI_MAPPINGS) {
        nvs_close(nvs);
        return ESP_ERR_NOT_FOUND;
    }

    // Probe blob size to detect old format
    size_t blob_size = 0;
    err = nvs_get_blob(nvs, "mappings", NULL, &blob_size);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }

    size_t new_size = count * sizeof(midi_mapping_t);  // 8 bytes per entry
    size_t old_entry_size = 4;  // old struct without scale
    size_t old_size = count * old_entry_size;

    if (blob_size == new_size) {
        // Current format
        err = nvs_get_blob(nvs, "mappings", s_mappings, &blob_size);
    } else if (blob_size == old_size) {
        // Old format: read into temp buffer, migrate
        ESP_LOGI(TAG, "Migrating %d mappings from old format", count);
        uint8_t old_buf[MAX_MIDI_MAPPINGS * 4];
        size_t read_size = old_size;
        err = nvs_get_blob(nvs, "mappings", old_buf, &read_size);
        if (err == ESP_OK) {
            for (int i = 0; i < count; i++) {
                s_mappings[i].control_id   = old_buf[i * 4 + 0];
                s_mappings[i].midi_channel = old_buf[i * 4 + 1];
                s_mappings[i].midi_type    = old_buf[i * 4 + 2];
                s_mappings[i].midi_param   = old_buf[i * 4 + 3];
                s_mappings[i].scale        = 0.5f;
            }
        }
    } else {
        ESP_LOGW(TAG, "Unknown blob size %d for %d entries, loading defaults", blob_size, count);
        nvs_close(nvs);
        return ESP_ERR_NOT_FOUND;
    }

    nvs_close(nvs);

    if (err == ESP_OK) {
        s_mapping_count = count;
        ESP_LOGI(TAG, "Loaded %d mappings from NVS", s_mapping_count);
    }
    return err;
}
```

**Step 2: Build to verify**

```bash
idf.py build
```

**Step 3: Commit**

```bash
git add src/event_engine.c
git commit -m "Add NVS migration for old mapping format without scale field"
```

---

### Task 5: Update REST API to include `scale`

**Files:**
- Modify: `src/http_server.c:242-327`

**Step 1: Add scale to GET response**

In `api_mappings_get_handler`, after the `param` line, add:

```c
cJSON_AddNumberToObject(entry, "scale", table[i].scale);
```

**Step 2: Read scale in PUT handler**

In `api_mappings_put_handler`, after reading `pm`, add scale parsing and include it in the mapping struct:

```c
        cJSON *sc  = cJSON_GetObjectItem(item, "scale");

        midi_mapping_t m = {
            .control_id   = (uint8_t)cid->valueint,
            .midi_channel = (uint8_t)ch->valueint,
            .midi_type    = (uint8_t)ty->valueint,
            .midi_param   = (uint8_t)pm->valueint,
            .scale        = (sc && cJSON_IsNumber(sc)) ? (float)sc->valuedouble : 0.5f,
        };
```

**Step 3: Build to verify**

```bash
idf.py build
```

**Step 4: Commit**

```bash
git add src/http_server.c
git commit -m "Expose scale field in mappings REST API"
```

---

### Task 6: Update frontend Mappings page

**Files:**
- Modify: `frontend/src/pages/Mappings.svelte`

**Step 1: Add scale column to mapping rows**

The current Mappings.svelte is designed for a CAT command browser with learn mode. The mappings data from the new API has `control_id`, `channel`, `type`, `param`, `scale`. The page needs to show these fields and let the user edit `scale` for CC entries.

This is a rewrite of the Active Mappings section. Replace the existing `<!-- Active Mappings -->` section (lines 214-234) with a table that shows scale:

In the mappings list, after `<span class="map-cmd">`:

```svelte
<!-- Active Mappings -->
<section class="panel">
  <h2>Active Mappings <span class="count">({mappings.length} controls)</span></h2>
  {#if mappings.length === 0}
    <p class="empty">No mappings configured.</p>
  {:else}
    <div class="mapping-list">
      <div class="map-header">
        <span class="col-id">ID</span>
        <span class="col-ch">Ch</span>
        <span class="col-type">Type</span>
        <span class="col-param">Param</span>
        <span class="col-scale">Scale</span>
      </div>
      {#each mappings as m, idx}
        <div class="map-row">
          <span class="col-id">{m.control_id}</span>
          <span class="col-ch">{m.channel}</span>
          <span class="col-type">{m.type === 0xB0 ? 'CC' : m.type === 0x90 ? 'Note' : '?'}</span>
          <span class="col-param">{m.param}</span>
          <span class="col-scale">
            {#if m.type === 0xB0}
              <input type="number" step="0.01" min="0" max="2"
                bind:value={m.scale}
                onchange={() => saveScale(idx)} />
            {:else}
              <span class="na">--</span>
            {/if}
          </span>
        </div>
      {/each}
    </div>
  {/if}
</section>
```

**Step 2: Add `saveScale` function**

In the `<script>` section, add a function that PUTs the single updated mapping:

```javascript
  async function saveScale(idx) {
    try {
      await putMappings([mappings[idx]]);
      success(`Scale updated for control ${mappings[idx].control_id}`);
    } catch (e) { error('Save failed: ' + e.message); }
  }
```

**Step 3: Add styles for the new columns**

```css
  .map-header {
    display: flex; gap: 0.5rem; padding: 0.4rem 0.5rem;
    font-size: 0.75rem; font-weight: 600; color: #7a8aa8;
    text-transform: uppercase; letter-spacing: 0.06em;
    border-bottom: 1px solid #1a3a6a44;
  }
  .col-id { width: 3rem; }
  .col-ch { width: 2.5rem; }
  .col-type { width: 4rem; }
  .col-param { width: 4rem; }
  .col-scale { flex: 1; }
  .col-scale input {
    width: 5rem; padding: 0.3rem 0.4rem; background: #0f0f1a;
    border: 1px solid #1a3a6a; color: #e0e0e0; border-radius: 4px;
    font-size: 0.85rem;
  }
  .col-scale input:focus { border-color: #e94560; outline: none; }
  .col-scale .na { color: #4a5568; }
```

**Step 4: Update `loadData` to use new API shape**

The `loadData` function currently calls `getCommands()` which doesn't exist in the new backend. Simplify to just load mappings:

```javascript
  async function loadData() {
    try {
      mappings = await getMappings();
    } catch (e) { error('Failed to load: ' + e.message); }
  }
```

Remove the `commands` state variable and the command browser section if it references non-existent APIs, or leave it (it will just show empty if the API 404s). The simpler approach: keep the full page but fix `loadData`.

**Step 5: Build frontend**

```bash
cd frontend && npm run build
```

**Step 6: Commit**

```bash
git add frontend/src/pages/Mappings.svelte
git commit -m "Add scale editing to Mappings page"
```

---

### Task 7: Flash and verify end-to-end

**Step 1: Full build**

```bash
cd /home/sf/src/Esp32DjConsole
idf.py build
```

**Step 2: Flash**

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

**Step 3: Verify**

- Move a dial on the DJ Console — check MIDI CC output values
- Open web GUI, go to Mappings tab
- Change scale on a dial (e.g. set Vol_A scale to 0.75)
- Move the dial again — verify output range changed
- Reboot the ESP32 — verify the scale persisted from NVS

**Step 4: Final commit**

```bash
git add -A
git commit -m "Value mangling: per-control CC scale factor (firmware + frontend)"
```
