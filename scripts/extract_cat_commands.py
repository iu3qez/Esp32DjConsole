#!/usr/bin/env python3
"""
Extract CAT commands from Thetis CATCommands.cs

Parses the C# source to produce a structured list of all ZZ (extended) and
standard Kenwood CAT commands, including:
  - command prefix (e.g. ZZFA, AG)
  - description from the comment above the method
  - read/write capability (from parser.nSet / parser.nGet usage)
  - Thetis console property accessed (e.g. console.VFOAFreq)
  - value range (from Math.Min/Max or explicit bounds)

Usage:
    python3 scripts/extract_cat_commands.py reference/CATCommands.cs
    python3 scripts/extract_cat_commands.py reference/CATCommands.cs --format json
    python3 scripts/extract_cat_commands.py reference/CATCommands.cs --format csv
    python3 scripts/extract_cat_commands.py reference/CATCommands.cs --diff
    python3 scripts/extract_cat_commands.py reference/CATCommands.cs --generate-c -o main/cmd_db_generated.inc
    python3 scripts/extract_cat_commands.py reference/CATCommands.cs --generate-c --overrides reference/cmd_overrides.json -o main/cmd_db_generated.inc
"""

import re
import sys
import json
import argparse
from pathlib import Path


def extract_commands(source: str) -> list[dict]:
    """Extract all CAT command methods from CATCommands.cs source."""
    commands = []

    # Match method signatures: public string XX(...) or public string ZZXX(...)
    # Capture everything from the comment block above through the method body
    method_pattern = re.compile(
        r'((?:[ \t]*//[^\n]*\n)*)'       # Group 1: comment lines above
        r'[ \t]*public\s+string\s+'       # method declaration
        r'([A-Z]{2,4})'                   # Group 2: command name (AG, ZZFA, etc.)
        r'\(([^)]*)\)',                    # Group 3: parameters
        re.MULTILINE
    )

    for match in method_pattern.finditer(source):
        comment_block = match.group(1)
        cmd_name = match.group(2)
        params = match.group(3).strip()
        method_start = match.end()

        # Extract the method body by brace-counting
        body = extract_body(source, method_start)

        # Parse the comment for description
        description = parse_comment(comment_block)

        # Determine read/write capability from the body
        has_set = 'parser.nSet' in body or 'nSet' in body
        has_get = 'parser.nGet' in body or 'nGet' in body
        # Some methods are write-only with no parameter (like ZZBU())
        if not params and not has_set and not has_get:
            rw = 'write'
        elif has_set and has_get:
            rw = 'read/write'
        elif has_set:
            rw = 'write'
        elif has_get:
            rw = 'read'
        else:
            # Methods that just delegate (return ZZXX(s)) or have custom logic
            if 'return ""' in body and ('console.' in body or 'Console.' in body):
                rw = 'write'
            else:
                rw = 'read/write'

        # Extract console properties accessed
        properties = extract_properties(body)

        # Extract value range from Math.Min/Max or explicit comparisons
        value_range = extract_range(body)

        # Check if it delegates to another command
        delegates_to = extract_delegation(body)

        # Detect if this is a toggle: (s == "0" || s == "1") pattern
        is_toggle = bool(re.search(r's\s*==\s*"0"\s*\|\|\s*s\s*==\s*"1"', body))

        # Determine command type (ZZ extended vs standard Kenwood)
        cmd_type = 'extended' if cmd_name.startswith('ZZ') else 'standard'

        commands.append({
            'cmd': cmd_name,
            'type': cmd_type,
            'description': description,
            'rw': rw,
            'properties': properties,
            'range': value_range,
            'delegates_to': delegates_to,
            'has_param': bool(params),
            'is_toggle': is_toggle,
        })

    return commands


def extract_body(source: str, start: int) -> str:
    """Extract method body by counting braces from start position."""
    depth = 0
    i = start
    body_start = None
    while i < len(source):
        if source[i] == '{':
            if depth == 0:
                body_start = i
            depth += 1
        elif source[i] == '}':
            depth -= 1
            if depth == 0:
                return source[body_start:i + 1] if body_start else ''
        i += 1
    return source[body_start:] if body_start else ''


def parse_comment(comment_block: str) -> str:
    """Extract description from comment lines above the method."""
    if not comment_block.strip():
        return ''
    lines = []
    for line in comment_block.strip().split('\n'):
        line = line.strip()
        # Remove // prefix and optional -W2PA tag
        line = re.sub(r'^//+\s*', '', line)
        line = re.sub(r'^-W2PA\s*', '', line)
        line = line.strip()
        if line:
            lines.append(line)
    return ' '.join(lines)


def extract_properties(body: str) -> list[str]:
    """Extract console.PropertyName references from the method body."""
    props = set()
    for m in re.finditer(r'console\.([A-Za-z_]\w*)', body):
        prop = m.group(1)
        # Skip common non-property accesses
        if prop in ('SetupForm', 'TitleBarEncoderString', 'Invoke',
                    'InvokeRequired', 'BeginInvoke'):
            continue
        props.add(prop)
    return sorted(props)


def extract_range(body: str) -> dict | None:
    """Extract value range from Math.Min/Max or explicit comparisons."""
    mins = re.findall(r'Math\.Max\(\s*(-?\d+)', body)
    maxs = re.findall(r'Math\.Min\(\s*(-?\d+)', body)

    if mins or maxs:
        result = {}
        if mins:
            result['min'] = int(mins[0])
        if maxs:
            result['max'] = int(maxs[0])
        return result

    # Check for step >= 0 || step <= N patterns
    bound = re.findall(r'>= *(-?\d+)\s*(?:\|\||&&|or)\s*\w+ *<= *(-?\d+)', body)
    if bound:
        return {'min': int(bound[0][0]), 'max': int(bound[0][1])}

    return None


def extract_delegation(body: str) -> str | None:
    """Check if the method just delegates to another ZZ command."""
    m = re.search(r'return\s+(ZZ[A-Z]{2})\s*\(', body)
    if m:
        return m.group(1)
    return None


def load_current_db(project_dir: Path) -> set[str]:
    """Load the command prefixes currently in our mapping_engine.c s_cmd_db."""
    mapping_file = project_dir / 'main' / 'mapping_engine.c'
    if not mapping_file.exists():
        return set()
    source = mapping_file.read_text()
    # Extract cat_cmd strings from the s_cmd_db array
    prefixes = set()
    for m in re.finditer(r'"(ZZ[A-Z]{2})"', source):
        prefixes.add(m.group(1))
    return prefixes


# ===================================================================
# C code generation (--generate-c mode)
# ===================================================================

# Category bases for ID assignment
CATEGORY_BASES = {
    'CAT_VFO':       100,
    'CAT_BAND':      200,
    'CAT_MODE':      300,
    'CAT_TX':        400,
    'CAT_AUDIO':     500,
    'CAT_FILTER':    600,
    'CAT_NR_NB':     700,
    'CAT_AGC':       800,
    'CAT_SPLIT_RIT': 900,
    'CAT_CW':        1000,
    'CAT_MISC':      1100,
}

# Keywords in description/properties -> category
CATEGORY_KEYWORDS = [
    # Order matters: more specific patterns first
    (r'(?i)\bsplit\b|\bRIT\b|\bXIT\b', 'CAT_SPLIT_RIT'),
    (r'(?i)\bCW\b|\bbreak.?in\b|\bsidetone\b|\bQSK\b', 'CAT_CW'),
    (r'(?i)\bNB\b|\bNR\b|\bnoise\b|\bnotch\b|\bSNB\b|\bbinaural\b|\bANF\b', 'CAT_NR_NB'),
    (r'(?i)\bAGC\b', 'CAT_AGC'),
    (r'(?i)\bfilter\b|FilterHigh|FilterLow|CATFH|CATFI', 'CAT_FILTER'),
    (r'(?i)\bAF\b|\bgain\b|\bvolume\b|\bMUT\b|\bmute\b|\bMON\b|\bmic\b|\bDX\b', 'CAT_AUDIO'),
    (r'(?i)\bMOX\b|\bPTT\b|\bTune\b|\bVOX\b|\bTX\b|\bPureSignal\b|\bTuner\b|\bPA\b|\bDrive\b', 'CAT_TX'),
    (r'(?i)\bmode\b|Modulation|DSPMode|CATDSP', 'CAT_MODE'),
    (r'(?i)\bband\b|BandList|BandUp|BandDown', 'CAT_BAND'),
    (r'(?i)\bVFO\b|VFO[AB]Freq|CATVFO', 'CAT_VFO'),
]


def detect_category(cmd: dict) -> str:
    """Detect category from description and property keywords."""
    text = cmd['description'] + ' ' + ' '.join(cmd.get('properties', []))
    for pattern, cat in CATEGORY_KEYWORDS:
        if re.search(pattern, text):
            return cat
    return 'CAT_MISC'


def detect_exec_type(cmd: dict) -> tuple[str, int, int, int]:
    """Detect exec_type, value_digits, value_min, value_max from CS analysis.

    Returns: (exec_type, value_digits, value_min, value_max)
    """
    # Toggle: s == "0" || s == "1" pattern
    if cmd.get('is_toggle'):
        return ('CMD_CAT_TOGGLE', 1, 0, 1)

    # No parameter = button (fire-and-forget)
    if not cmd['has_param']:
        return ('CMD_CAT_BUTTON', 0, 0, 0)

    # Has range with max > 1 = SET
    r = cmd.get('range')
    if r and ('max' in r or 'min' in r):
        vmin = r.get('min', 0)
        vmax = r.get('max', 1)
        if vmax > 1:
            digits = len(str(abs(vmax)))
            if vmin < 0:
                digits = max(digits, len(str(abs(vmin))))
            return ('CMD_CAT_SET', digits, vmin, vmax)
        elif vmax == 1 and vmin == 0:
            return ('CMD_CAT_TOGGLE', 1, 0, 1)

    # Delegates to another command - treat as button
    if cmd.get('delegates_to'):
        return ('CMD_CAT_BUTTON', 0, 0, 0)

    # Read/write with parameter but no detected range -> toggle (common pattern)
    if cmd['rw'] == 'read/write' and cmd['has_param']:
        return ('CMD_CAT_TOGGLE', 1, 0, 1)

    # Fallback
    return ('CMD_CAT_BUTTON', 0, 0, 0)


def load_overrides(path: Path) -> list[dict]:
    """Load override entries from JSON file."""
    if not path.exists():
        return []
    with open(path) as f:
        entries = json.load(f)
    # Filter out comment-only entries
    return [e for e in entries if 'cmd' in e]


def generate_c(commands: list[dict], overrides_path: Path | None = None) -> str:
    """Generate C source for s_cmd_db[] array."""
    overrides = load_overrides(overrides_path) if overrides_path else []

    # Build override lookup: cmd -> override entry
    # Some overrides use synthetic keys like "ZZBS/160" or "ZZMD/0"
    override_by_cmd = {}
    override_entries = []  # Fully-specified override entries (with synthetic keys)
    override_real_cmds = set()  # Real ZZ commands that have overrides

    for ov in overrides:
        key = ov['cmd']
        if '/' in key:
            # Synthetic key like "ZZBS/160" - this is a standalone entry
            override_entries.append(ov)
            real_cmd = key.split('/')[0]
            override_real_cmds.add(real_cmd)
        else:
            override_by_cmd[key] = ov
            override_real_cmds.add(key)

    # Track IDs used by overrides per category
    override_ids_by_cat = {}
    for ov in overrides:
        if 'id' in ov and 'category' in ov:
            cat = ov['category']
            override_ids_by_cat.setdefault(cat, set()).add(ov['id'])

    # Build the command list: auto-detected + overrides
    db_entries = []

    # Process auto-detected commands (ZZ only, skip delegates and synthetic-override cmds)
    for cmd in commands:
        if cmd['type'] != 'extended':
            continue
        if cmd.get('delegates_to'):
            continue

        zz = cmd['cmd']

        # If this command has a direct override, use override values
        if zz in override_by_cmd:
            ov = override_by_cmd[zz]
            cat_cmd = ov.get('cat_cmd', zz)
            cat_cmd2 = ov.get('cat_cmd2')
            entry = {
                'id': ov['id'],
                'name': ov['name'],
                'category': ov['category'],
                'exec_type': ov['exec_type'],
                'cat_cmd': cat_cmd,
                'cat_cmd2': cat_cmd2,
                'value_digits': ov.get('value_digits', 0),
                'value_min': ov.get('value_min', 0),
                'value_max': ov.get('value_max', 0),
                'from_override': True,
            }
            db_entries.append(entry)
            continue

        # If this real command is covered by synthetic overrides (e.g. ZZBS, ZZMD),
        # skip auto-detection — the synthetic entries handle it
        if zz in override_real_cmds:
            continue

        # Auto-detect
        exec_type, digits, vmin, vmax = detect_exec_type(cmd)
        category = detect_category(cmd)

        # Build name from description (truncate to reasonable length)
        name = cmd['description']
        if not name:
            name = zz
        # Shorten: take first ~40 chars
        if len(name) > 40:
            name = name[:37] + '...'

        entry = {
            'id': 0,  # Will be assigned below
            'name': name,
            'category': category,
            'exec_type': exec_type,
            'cat_cmd': zz,
            'cat_cmd2': None,
            'value_digits': digits,
            'value_min': vmin,
            'value_max': vmax,
            'from_override': False,
        }
        db_entries.append(entry)

    # Add standalone override entries (synthetic keys like ZZBS/160, ZZMD/0, UP/DN)
    for ov in override_entries:
        cat_cmd = ov.get('cat_cmd', ov['cmd'].split('/')[0] if '/' in ov['cmd'] else ov['cmd'])
        entry = {
            'id': ov['id'],
            'name': ov['name'],
            'category': ov['category'],
            'exec_type': ov['exec_type'],
            'cat_cmd': cat_cmd,
            'cat_cmd2': ov.get('cat_cmd2'),
            'value_digits': ov.get('value_digits', 0),
            'value_min': ov.get('value_min', 0),
            'value_max': ov.get('value_max', 0),
            'from_override': True,
        }
        db_entries.append(entry)

    # Assign IDs: category base + sequential, skipping override-pinned IDs
    id_counters = {}
    for cat, base in CATEGORY_BASES.items():
        # Find highest pinned ID in this category
        pinned = override_ids_by_cat.get(cat, set())
        # Start auto-assignment after all pinned IDs
        max_pinned = max(pinned) if pinned else base - 1
        id_counters[cat] = max(base, max_pinned + 1)

    for entry in db_entries:
        if entry['id'] != 0:
            continue  # Override-pinned
        cat = entry['category']
        # Find next available ID
        while id_counters[cat] in override_ids_by_cat.get(cat, set()):
            id_counters[cat] += 1
        entry['id'] = id_counters[cat]
        id_counters[cat] += 1

    # Sort by category order then ID
    cat_order = list(CATEGORY_BASES.keys())
    db_entries.sort(key=lambda e: (cat_order.index(e['category']), e['id']))

    # Generate C code
    lines = []
    lines.append('// Auto-generated from reference/CATCommands.cs — DO NOT EDIT')
    lines.append('// Run: python3 scripts/extract_cat_commands.py reference/CATCommands.cs --generate-c -o main/cmd_db_generated.inc')
    lines.append(f'// Total: {len(db_entries)} commands')
    lines.append('')
    lines.append('static const thetis_cmd_t s_cmd_db[] = {')

    current_cat = None
    for entry in db_entries:
        if entry['category'] != current_cat:
            current_cat = entry['category']
            cat_name = current_cat.replace('CAT_', '')
            base = CATEGORY_BASES[current_cat]
            count = sum(1 for e in db_entries if e['category'] == current_cat)
            lines.append(f'')
            lines.append(f'    // ------ {cat_name} (id {base}+, {count} entries) ------')

        # Format the C struct initializer
        cmd2 = f'"{entry["cat_cmd2"]}"' if entry['cat_cmd2'] else 'NULL'
        name_escaped = entry['name'].replace('"', '\\"')

        line = (f'    {{ {entry["id"]:4d}, '
                f'"{name_escaped}", '
                f'{entry["category"]:14s}, '
                f'{entry["exec_type"]:16s}, '
                f'"{entry["cat_cmd"]}", '
                f'{cmd2:6s}, '
                f'{entry["value_digits"]:2d}, '
                f'{entry["value_min"]:5d}, '
                f'{entry["value_max"]:5d} }},')

        lines.append(line)

    lines.append('};')
    lines.append('')

    return '\n'.join(lines)


# ===================================================================
# Original output formatters
# ===================================================================

def format_table(commands: list[dict], show_diff: bool = False,
                 current_cmds: set[str] | None = None) -> str:
    """Format commands as a human-readable table."""
    lines = []

    if show_diff and current_cmds is not None:
        lines.append(f'Legend: [+] = in our DB, [-] = MISSING from our DB\n')

    # Group by type
    zz_cmds = [c for c in commands if c['type'] == 'extended']
    std_cmds = [c for c in commands if c['type'] == 'standard']

    for label, cmds in [('Extended ZZ Commands', zz_cmds),
                         ('Standard Kenwood Commands', std_cmds)]:
        lines.append(f'{"=" * 80}')
        lines.append(f' {label} ({len(cmds)} commands)')
        lines.append(f'{"=" * 80}')
        lines.append('')

        for c in cmds:
            marker = ''
            if show_diff and current_cmds is not None:
                if c['cmd'] in current_cmds:
                    marker = '[+] '
                else:
                    marker = '[-] '

            desc = c['description'] or '(no description)'
            rw = c['rw']
            toggle_marker = ' [TOGGLE]' if c.get('is_toggle') else ''
            lines.append(f'{marker}{c["cmd"]:6s}  {rw:11s}  {desc}{toggle_marker}')

            details = []
            if c['properties']:
                details.append(f'console: {", ".join(c["properties"])}')
            if c['range']:
                r = c['range']
                parts = []
                if 'min' in r:
                    parts.append(f'min={r["min"]}')
                if 'max' in r:
                    parts.append(f'max={r["max"]}')
                details.append(f'range: {", ".join(parts)}')
            if c['delegates_to']:
                details.append(f'delegates to: {c["delegates_to"]}')
            if details:
                lines.append(f'        {"  |  ".join(details)}')
            lines.append('')

    return '\n'.join(lines)


def format_csv(commands: list[dict]) -> str:
    """Format as CSV."""
    lines = ['cmd,type,rw,description,properties,range_min,range_max,delegates_to,is_toggle']
    for c in commands:
        props = ';'.join(c['properties'])
        rmin = c['range']['min'] if c['range'] and 'min' in c['range'] else ''
        rmax = c['range']['max'] if c['range'] and 'max' in c['range'] else ''
        desc = c['description'].replace('"', '""')
        deleg = c['delegates_to'] or ''
        toggle = '1' if c.get('is_toggle') else '0'
        lines.append(f'{c["cmd"]},{c["type"]},{c["rw"]},"{desc}","{props}",{rmin},{rmax},{deleg},{toggle}')
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(
        description='Extract CAT commands from Thetis CATCommands.cs')
    parser.add_argument('input', help='Path to CATCommands.cs')
    parser.add_argument('--format', choices=['table', 'json', 'csv'],
                        default='table', help='Output format (default: table)')
    parser.add_argument('--include-kenwood', action='store_true',
                        help='Also include standard Kenwood commands (they just delegate to ZZ)')
    parser.add_argument('--diff', action='store_true',
                        help='Mark commands present/missing in our mapping DB')
    parser.add_argument('--generate-c', action='store_true',
                        help='Generate C source for s_cmd_db[] array')
    parser.add_argument('--overrides', help='Path to cmd_overrides.json for hand-tuned entries')
    parser.add_argument('--output', '-o', help='Write to file instead of stdout')
    args = parser.parse_args()

    source_path = Path(args.input)
    if not source_path.exists():
        print(f'Error: {source_path} not found', file=sys.stderr)
        sys.exit(1)

    source = source_path.read_text(encoding='utf-8-sig')  # Handle BOM
    commands = extract_commands(source)

    if not args.include_kenwood and not args.generate_c:
        commands = [c for c in commands if c['type'] == 'extended']

    # Generate C mode
    if args.generate_c:
        overrides_path = Path(args.overrides) if args.overrides else None
        output = generate_c(commands, overrides_path)

        if args.output:
            Path(args.output).write_text(output)
            # Count entries in generated output
            count = output.count('{ ')
            print(f'Generated {args.output} ({count} commands)')
        else:
            print(output)
        return

    # Load current DB for diff mode
    current_cmds = None
    if args.diff:
        project_dir = Path(__file__).resolve().parent.parent
        current_cmds = load_current_db(project_dir)

    # Format output
    if args.format == 'json':
        output = json.dumps(commands, indent=2)
    elif args.format == 'csv':
        output = format_csv(commands)
    else:
        output = format_table(commands, show_diff=args.diff,
                              current_cmds=current_cmds)

    # Summary
    zz_count = sum(1 for c in commands if c['type'] == 'extended')
    std_count = sum(1 for c in commands if c['type'] == 'standard')
    summary = f'\nTotal: {len(commands)} commands ({zz_count} ZZ extended, {std_count} standard Kenwood)'

    if args.diff and current_cmds is not None:
        in_db = sum(1 for c in commands if c['cmd'] in current_cmds)
        missing = sum(1 for c in commands
                      if c['type'] == 'extended' and c['cmd'] not in current_cmds)
        summary += f'\nIn our DB: {in_db} | Missing ZZ commands: {missing}'

    if args.format == 'table':
        output += summary

    if args.output:
        Path(args.output).write_text(output)
        print(f'Written to {args.output}')
        print(summary)
    else:
        print(output)


if __name__ == '__main__':
    main()
