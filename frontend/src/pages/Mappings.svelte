<script>
  import { onMount } from 'svelte';
  import { getMappings, putMappings, resetMappings } from '../lib/api.js';
  import { success, error } from '../lib/toast.js';
  import ConfirmDialog from '../lib/ConfirmDialog.svelte';

  const ACTION_NAMES = [
    'None', 'VFO A Step', 'VFO B Step', 'VFO A Direct', 'Band Select',
    'Mode Set', 'Mode Cycle', 'Volume', 'Volume Step',
    'PTT Toggle', 'PTT Momentary', 'Tune Toggle', 'Drive', 'Drive Step',
    'Filter Width', 'Filter Shift', 'Mute Toggle', 'Split Toggle',
    'Custom TCI', 'Custom CAT',
  ];

  let mappings = $state([]);
  let saving = $state(false);
  let filter = $state('');
  let showResetConfirm = $state(false);

  onMount(async () => {
    try { mappings = await getMappings(); }
    catch (e) { error('Failed to load mappings: ' + e.message); }
  });

  function filtered() {
    if (!filter) return mappings;
    const q = filter.toLowerCase();
    return mappings.filter(m => m.control.toLowerCase().includes(q));
  }

  async function save() {
    saving = true;
    try {
      const res = await putMappings(mappings);
      success(`Saved ${res.applied} mappings`);
    } catch (e) { error('Save failed: ' + e.message); }
    saving = false;
  }

  async function doReset() {
    showResetConfirm = false;
    try {
      await resetMappings();
      mappings = await getMappings();
      success('Mappings reset to defaults');
    } catch (e) { error('Reset failed: ' + e.message); }
  }

  function updateAction(idx, val) { mappings[idx].action = parseInt(val); }
  function updateParam(idx, val) { mappings[idx].param_int = parseInt(val) || 0; }
  function updateParamStr(idx, val) { mappings[idx].param_str = val; }
</script>

<div class="toolbar">
  <input type="text" placeholder="Filter controls..." bind:value={filter} />
  <button onclick={save} disabled={saving}>{saving ? 'Saving...' : 'Save'}</button>
  <button class="reset" onclick={() => showResetConfirm = true}>Reset</button>
</div>

{#if showResetConfirm}
  <ConfirmDialog
    title="Reset Mappings"
    message="This will replace all mappings with the factory defaults. This cannot be undone."
    onconfirm={doReset}
    oncancel={() => showResetConfirm = false}
  />
{/if}

<div class="table-wrap">
  <table>
    <thead>
      <tr><th>Control</th><th>Action</th><th>Param</th><th>Str</th><th>RX</th></tr>
    </thead>
    <tbody>
      {#each filtered() as m, i}
        <tr>
          <td class="ctrl">{m.control}</td>
          <td>
            <select value={m.action} onchange={(e) => updateAction(i, e.target.value)}>
              {#each ACTION_NAMES as name, ai}
                <option value={ai}>{name}</option>
              {/each}
            </select>
          </td>
          <td><input type="number" value={m.param_int} onchange={(e) => updateParam(i, e.target.value)} /></td>
          <td><input type="text" value={m.param_str ?? ''} onchange={(e) => updateParamStr(i, e.target.value)} /></td>
          <td><input type="number" value={m.rx} min="0" max="1" style="width:3em"
            onchange={(e) => mappings[i].rx = parseInt(e.target.value) || 0} /></td>
        </tr>
      {/each}
    </tbody>
  </table>
</div>

<style>
  .toolbar {
    display: flex; gap: 0.5rem; margin-bottom: 0.75rem; align-items: center;
  }
  .toolbar input[type="text"] {
    flex: 1; padding: 0.4rem; background: #16213e; border: 1px solid #0f3460;
    color: #e0e0e0; border-radius: 4px;
  }
  .toolbar button {
    padding: 0.4rem 1rem; border: none; border-radius: 4px;
    cursor: pointer; font-size: 0.85rem;
    background: #0f3460; color: #e0e0e0;
  }
  .toolbar button:hover { background: #1a4a8a; }
  .toolbar button.reset { background: #5c1a1a; }
  .toolbar button.reset:hover { background: #8a2a2a; }

  .table-wrap { overflow-x: auto; }
  table { width: 100%; border-collapse: collapse; font-size: 0.8rem; }
  th { text-align: left; color: #888; padding: 0.3rem; border-bottom: 1px solid #0f3460; font-size: 0.75rem; text-transform: uppercase; }
  td { padding: 0.25rem 0.3rem; border-bottom: 1px solid #0f346033; }
  .ctrl { font-family: monospace; color: #e94560; white-space: nowrap; }
  td select, td input {
    background: #1a1a2e; border: 1px solid #0f3460; color: #e0e0e0;
    padding: 0.2rem; border-radius: 3px; width: 100%;
  }
  td input[type="number"] { width: 6em; }
  td input[type="text"] { width: 8em; }
</style>
