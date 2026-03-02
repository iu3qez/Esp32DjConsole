---
name: add-cat-command
description: Add a new Kenwood CAT command to the Thetis mapping engine
---

# Add a CAT Command to the Mapping Engine

## Overview

The mapping engine maps DJ console controls to Thetis CAT commands. The command database
is auto-generated from `reference/CATCommands.cs` into `main/cmd_db_generated.inc`, containing
328+ commands. To add a custom command not in the Thetis source, follow this guide.

## Command Database Structure

Each command in `main/cmd_db_generated.inc` is a `thetis_cmd_t` struct:

```c
{ id, "Name", "Description (or NULL)", category, exec_type, "CAT_CMD", "CAT_CMD2 (or NULL)", value_digits, value_min, value_max }
```

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `id` | uint16_t | Unique stable ID. Use ranges: VFO=100+, Band=200+, Mode=300+, TX=400+, Audio=500+, Filter=600+, NR/NB=700+, AGC=800+, Split/RIT=900+, CW=1000+, Misc=1100+ |
| `name` | string | Short human-readable name for UI |
| `description` | string or NULL | Longer description (NULL if same as name) |
| `category` | cmd_category_t | One of: CAT_VFO, CAT_BAND, CAT_MODE, CAT_TX, CAT_AUDIO, CAT_FILTER, CAT_NR_NB, CAT_AGC, CAT_SPLIT_RIT, CAT_CW, CAT_MISC |
| `exec_type` | cmd_exec_type_t | How the command executes (see below) |
| `cat_cmd` | string | CAT command prefix (e.g. "ZZFA", "ZZBU") |
| `cat_cmd2` | string or NULL | Second command for CMD_CAT_WHEEL (dec direction) |
| `value_digits` | int | 0=no parameter, 1-11=zero-padded digit count |
| `value_min` | int | Minimum value for SET/TOGGLE |
| `value_max` | int | Maximum value for SET/TOGGLE |

### Exec Types

| Type | Use For | Behavior |
|------|---------|----------|
| `CMD_CAT_BUTTON` | Momentary actions | Sends command on press only |
| `CMD_CAT_TOGGLE` | On/off states | Tracks state, sends 0/1 alternately |
| `CMD_CAT_SET` | Knobs/sliders | Scales 0-255 input to value_min..value_max |
| `CMD_CAT_FREQ` | VFO tuning | Encoder delta * step Hz, sends 11-digit freq |
| `CMD_CAT_WHEEL` | Inc/dec pairs | Sends cat_cmd for +, cat_cmd2 for - |
| `CMD_CAT_FILTER_WIDTH` | Filter control | Sets filter width via ZZSF |

## Adding a New Exec Type

If the existing exec types don't fit:

1. Add enum value to `cmd_exec_type_t` in `main/mapping_engine.h`
2. Add `exec_XXX()` function in `main/mapping_engine.c` following the pattern of existing exec functions
3. Add case to `execute_command()` switch in `main/mapping_engine.c`

## Adding a New Command

### Option A: Regenerate from CATCommands.cs (preferred)

If the command exists in Thetis but wasn't extracted:

```bash
python3 scripts/extract_cat_commands.py reference/CATCommands.cs --generate-c -o main/cmd_db_generated.inc
```

### Option B: Manual addition

Add entry to `main/cmd_db_generated.inc` in the appropriate category section. Pick an unused ID
in the category's range. Example:

```c
// In the AUDIO section (id 500+):
{  530, "Squelch Level", "Sets squelch threshold", CAT_AUDIO, CMD_CAT_SET, "ZZSQ", NULL, 3, 0, 255 },
```

## Adding a Default Mapping

To map the new command to a DJ control by default, add to `mapping_engine_reset_defaults()` in
`main/mapping_engine.c`:

```c
add_default("Control_Name", COMMAND_ID, param);
```

- `param` = 0 for most types
- `param` = Hz per tick for CMD_CAT_FREQ (e.g. 10 for fine, 100 for coarse)
- `param` = step size for CMD_CAT_SET encoder-relative mode

## CAT Response Sync

If the new command's state should be tracked from Thetis responses (for toggle sync, VFO sync, etc.),
add handling in `mapping_engine_on_cat_response()` in `main/mapping_engine.c`. The generic toggle
and SET sync loops handle most cases automatically if the command uses CMD_CAT_TOGGLE or CMD_CAT_SET.
