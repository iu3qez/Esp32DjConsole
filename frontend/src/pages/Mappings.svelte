<script>
  import { onMount, onDestroy } from 'svelte';
  import { getCommands, getMappings, resetMappings, downloadMappings, uploadMappings, clearMapping } from '../lib/api.js';
  import { subscribe as wsSub } from '../lib/ws.js';
  import { success, error } from '../lib/toast.js';
  import ConfirmDialog from '../lib/ConfirmDialog.svelte';

  const EXEC_LABELS = ['Button', 'Toggle', 'Knob', 'Freq', 'Wheel'];

  let commands = $state([]);
  let mappings = $state([]);
  let search = $state('');
  let catFilter = $state('');
  let showResetConfirm = $state(false);
  let learning = $state(false);
  let learnCmd = $state(null);
  let learnTimer = $state(0);
  let learnInterval = $state(null);
  let uploading = $state(false);

  // Group commands by category
  function grouped() {
    let cmds = commands;
    if (search) {
      const q = search.toLowerCase();
      cmds = cmds.filter(c => c.name.toLowerCase().includes(q));
    }
    if (catFilter) {
      cmds = cmds.filter(c => c.cat_name === catFilter);
    }
    const groups = {};
    for (const c of cmds) {
      if (!groups[c.cat_name]) groups[c.cat_name] = [];
      groups[c.cat_name].push(c);
    }
    return groups;
  }

  function categories() {
    const cats = new Set(commands.map(c => c.cat_name));
    return [...cats].sort();
  }

  // Find which control is mapped to a command
  function mappedControl(cmdId) {
    const m = mappings.find(m => m.id === cmdId);
    return m ? m.c : null;
  }

  // Count mapped controls
  function mappedCount() {
    return mappings.length;
  }

  async function loadData() {
    try {
      [commands, mappings] = await Promise.all([getCommands(), getMappings()]);
    } catch (e) { error('Failed to load: ' + e.message); }
  }

  // Learn mode
  function startLearn(cmd) {
    learnCmd = cmd;
    learning = true;
    learnTimer = 15;
    // Send learn command via WebSocket
    const ws = new WebSocket(`${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/ws`);
    ws.onopen = () => {
      ws.send(JSON.stringify({ type: 'learn', command_id: cmd.id }));
      ws.close();
    };
    // Countdown
    learnInterval = setInterval(() => {
      learnTimer--;
      if (learnTimer <= 0) {
        cancelLearn();
      }
    }, 1000);
  }

  function cancelLearn() {
    if (learnInterval) { clearInterval(learnInterval); learnInterval = null; }
    if (learning) {
      const ws = new WebSocket(`${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/ws`);
      ws.onopen = () => {
        ws.send(JSON.stringify({ type: 'learn_cancel' }));
        ws.close();
      };
    }
    learning = false;
    learnCmd = null;
  }

  // Handle WS messages for learn results
  let unsub;
  onMount(async () => {
    await loadData();
    unsub = wsSub((msg) => {
      if (msg.type === 'learned') {
        if (learnInterval) { clearInterval(learnInterval); learnInterval = null; }
        learning = false;
        learnCmd = null;
        success(`Mapped ${msg.control} to ${msg.command_name}`);
        loadData();  // Refresh mappings
      } else if (msg.type === 'learn_timeout') {
        if (learnInterval) { clearInterval(learnInterval); learnInterval = null; }
        learning = false;
        learnCmd = null;
        error('Learn timed out - no control was moved');
      }
    });
  });

  onDestroy(() => {
    if (unsub) unsub();
    if (learnInterval) clearInterval(learnInterval);
  });

  async function doReset() {
    showResetConfirm = false;
    try {
      await resetMappings();
      mappings = await getMappings();
      success('Mappings reset to defaults');
    } catch (e) { error('Reset failed: ' + e.message); }
  }

  function doDownload() {
    downloadMappings();
  }

  async function doUpload(e) {
    const file = e.target.files?.[0];
    if (!file) return;
    uploading = true;
    try {
      const text = await file.text();
      const res = await uploadMappings(text);
      success(`Uploaded ${res.loaded} mappings`);
      mappings = await getMappings();
    } catch (err) { error('Upload failed: ' + err.message); }
    uploading = false;
    e.target.value = '';
  }

  async function doClear(controlName) {
    try {
      await clearMapping(controlName);
      mappings = await getMappings();
      success(`Cleared ${controlName}`);
    } catch (e) { error('Clear failed: ' + e.message); }
  }
</script>

<!-- Learn mode overlay -->
{#if learning && learnCmd}
  <div class="overlay">
    <div class="learn-modal">
      <h3>Assigning: {learnCmd.name}</h3>
      <p>Move a control on the DJ Console now...</p>
      <div class="timer-bar">
        <div class="timer-fill" style="width: {(learnTimer / 15) * 100}%"></div>
      </div>
      <span class="timer-text">{learnTimer}s remaining</span>
      <button class="cancel-btn" onclick={cancelLearn}>Cancel</button>
    </div>
  </div>
{/if}

{#if showResetConfirm}
  <ConfirmDialog
    title="Reset Mappings"
    message="This will replace all mappings with the factory defaults. This cannot be undone."
    onconfirm={doReset}
    oncancel={() => showResetConfirm = false}
  />
{/if}

<!-- Command Browser -->
<section class="panel">
  <h2>Command Browser</h2>
  <div class="toolbar">
    <input type="text" placeholder="Search commands..." bind:value={search} />
    <select bind:value={catFilter}>
      <option value="">All Categories</option>
      {#each categories() as cat}
        <option value={cat}>{cat}</option>
      {/each}
    </select>
  </div>

  <div class="cmd-list">
    {#each Object.entries(grouped()) as [catName, cmds]}
      <div class="cat-group">
        <h3 class="cat-header">{catName}</h3>
        {#each cmds as cmd}
          <div class="cmd-row">
            <span class="cmd-name">{cmd.name}</span>
            <span class="cmd-type">{EXEC_LABELS[cmd.exec] || '?'}</span>
            {#if mappedControl(cmd.id)}
              <span class="cmd-mapped">{mappedControl(cmd.id)}</span>
            {/if}
            <button class="assign-btn" onclick={() => startLearn(cmd)} disabled={learning}>
              Assign
            </button>
          </div>
        {/each}
      </div>
    {/each}
  </div>
</section>

<!-- Active Mappings -->
<section class="panel">
  <h2>Active Mappings <span class="count">({mappedCount()} controls mapped)</span></h2>
  {#if mappings.length === 0}
    <p class="empty">No mappings configured. Use "Assign" above to create mappings.</p>
  {:else}
    <div class="mapping-list">
      {#each mappings as m}
        <div class="map-row">
          <span class="map-ctrl">{m.c}</span>
          <span class="map-arrow">&rarr;</span>
          <span class="map-cmd">{m.name || `CMD #${m.id}`}</span>
          {#if m.p}
            <span class="map-param">({m.p})</span>
          {/if}
          <button class="clear-btn" onclick={() => doClear(m.c)}>Clear</button>
        </div>
      {/each}
    </div>
  {/if}
</section>

<!-- Actions -->
<div class="actions">
  <button onclick={doDownload}>Download</button>
  <label class="upload-btn">
    Upload
    <input type="file" accept=".json" onchange={doUpload} hidden disabled={uploading} />
  </label>
  <button class="reset" onclick={() => showResetConfirm = true}>Reset to Defaults</button>
</div>

<style>
  .panel {
    background: #16213e;
    border: 1px solid #0f3460;
    border-radius: 6px;
    padding: 0.75rem;
    margin-bottom: 1rem;
  }
  h2 { margin: 0 0 0.5rem; font-size: 1rem; color: #e94560; }
  .count { color: #888; font-size: 0.8rem; font-weight: normal; }

  .toolbar {
    display: flex; gap: 0.5rem; margin-bottom: 0.5rem;
  }
  .toolbar input, .toolbar select {
    flex: 1; padding: 0.35rem 0.5rem; background: #1a1a2e; border: 1px solid #0f3460;
    color: #e0e0e0; border-radius: 4px; font-size: 0.8rem;
  }
  .toolbar select { flex: 0 0 auto; min-width: 8rem; }

  .cmd-list { max-height: 400px; overflow-y: auto; }
  .cat-group { margin-bottom: 0.5rem; }
  .cat-header {
    font-size: 0.75rem; color: #888; text-transform: uppercase; letter-spacing: 0.05em;
    margin: 0.5rem 0 0.25rem; padding: 0.2rem 0; border-bottom: 1px solid #0f346044;
  }
  .cmd-row {
    display: flex; align-items: center; gap: 0.5rem;
    padding: 0.25rem 0.3rem; border-radius: 3px;
  }
  .cmd-row:hover { background: #1a1a2e; }
  .cmd-name { flex: 1; font-size: 0.85rem; }
  .cmd-type {
    font-size: 0.7rem; color: #888; background: #1a1a2e;
    padding: 1px 6px; border-radius: 3px; border: 1px solid #0f3460;
  }
  .cmd-mapped {
    font-size: 0.7rem; color: #2d6a4f; background: #1a2e1a;
    padding: 1px 6px; border-radius: 3px; font-family: monospace;
  }
  .assign-btn {
    padding: 2px 8px; border: 1px solid #0f3460; background: #0f3460;
    color: #ccc; border-radius: 3px; cursor: pointer; font-size: 0.75rem;
  }
  .assign-btn:hover { background: #1a4a8a; color: #fff; }
  .assign-btn:disabled { opacity: 0.4; cursor: default; }

  .mapping-list { display: flex; flex-direction: column; gap: 0.25rem; }
  .map-row {
    display: flex; align-items: center; gap: 0.4rem;
    padding: 0.3rem 0.4rem; background: #1a1a2e; border-radius: 3px;
  }
  .map-ctrl { font-family: monospace; color: #e94560; font-size: 0.85rem; min-width: 6rem; }
  .map-arrow { color: #555; }
  .map-cmd { flex: 1; font-size: 0.85rem; }
  .map-param { color: #888; font-size: 0.75rem; }
  .clear-btn {
    padding: 1px 6px; border: 1px solid #5c1a1a; background: transparent;
    color: #e94560; border-radius: 3px; cursor: pointer; font-size: 0.7rem;
  }
  .clear-btn:hover { background: #5c1a1a; }

  .empty { color: #666; font-style: italic; font-size: 0.85rem; }

  .actions {
    display: flex; gap: 0.5rem; margin-top: 0.5rem;
  }
  .actions button, .upload-btn {
    padding: 0.4rem 1rem; border: none; border-radius: 4px;
    cursor: pointer; font-size: 0.85rem;
    background: #0f3460; color: #e0e0e0;
    display: inline-flex; align-items: center;
  }
  .actions button:hover, .upload-btn:hover { background: #1a4a8a; }
  .actions button.reset { background: #5c1a1a; }
  .actions button.reset:hover { background: #8a2a2a; }

  /* Learn overlay */
  .overlay {
    position: fixed; inset: 0; background: rgba(0,0,0,0.7);
    display: flex; align-items: center; justify-content: center; z-index: 100;
  }
  .learn-modal {
    background: #16213e; border: 2px solid #e94560; border-radius: 10px;
    padding: 1.5rem 2rem; text-align: center; max-width: 320px; width: 90%;
  }
  .learn-modal h3 { margin: 0 0 0.5rem; color: #e94560; font-size: 1.1rem; }
  .learn-modal p { color: #ccc; margin: 0.5rem 0 1rem; font-size: 0.9rem; }
  .timer-bar {
    height: 6px; background: #1a1a2e; border-radius: 3px; overflow: hidden;
    margin-bottom: 0.3rem;
  }
  .timer-fill {
    height: 100%; background: #e94560; border-radius: 3px;
    transition: width 1s linear;
  }
  .timer-text { font-size: 0.75rem; color: #888; }
  .cancel-btn {
    margin-top: 1rem; padding: 0.4rem 1.5rem; border: 1px solid #555;
    background: transparent; color: #ccc; border-radius: 4px; cursor: pointer;
  }
  .cancel-btn:hover { background: #333; }
</style>
