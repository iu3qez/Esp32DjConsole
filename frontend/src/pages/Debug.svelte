<script>
  import { controlLog, catLog } from '../lib/stores.js';
  import { sendCat } from '../lib/api.js';
  import { success, error } from '../lib/toast.js';

  let mode = $state('usb');  // 'usb' or 'cat'
  let usbLog = $state([]);
  let catEntries = $state([]);
  let paused = $state(false);
  let catCmd = $state('');
  let sending = $state(false);

  $effect(() => {
    return controlLog.subscribe(v => { if (!paused) usbLog = v; });
  });

  $effect(() => {
    return catLog.subscribe(v => { if (!paused) catEntries = v; });
  });

  function clear() {
    if (mode === 'usb') { controlLog.set([]); usbLog = []; }
    else { catLog.set([]); catEntries = []; }
  }

  function ctrlType(t) {
    return ['BTN', 'DIAL', 'ENC'][t] ?? '?';
  }

  function fmtTime(ts) {
    const d = new Date(ts);
    return d.toLocaleTimeString('en-US', { hour12: false, fractionalSecondDigits: 1 });
  }

  function activeLog() {
    return mode === 'usb' ? usbLog : catEntries;
  }

  async function doSendCat() {
    let cmd = catCmd.trim();
    if (!cmd) return;
    if (!cmd.endsWith(';')) cmd += ';';
    sending = true;
    try {
      await sendCat(cmd);
      success(`Sent: ${cmd}`);
    } catch (e) { error('Send failed: ' + e.message); }
    sending = false;
  }

  function onCatKeydown(e) {
    if (e.key === 'Enter') doSendCat();
  }
</script>

<div class="toolbar">
  <div class="mode-toggle">
    <button class:active={mode === 'usb'} onclick={() => mode = 'usb'}>USB Events</button>
    <button class:active={mode === 'cat'} onclick={() => mode = 'cat'}>CAT Sent</button>
  </div>
  <span class="count">{activeLog().length} events</span>
  <button class:active={paused} onclick={() => paused = !paused}>{paused ? 'Resume' : 'Pause'}</button>
  <button class="clear" onclick={clear}>Clear</button>
</div>

<div class="cat-console">
  <span class="cat-label">CAT</span>
  <input type="text" bind:value={catCmd} onkeydown={onCatKeydown}
         placeholder="e.g. ZZZB; or ZZFA;" disabled={sending} />
  <button onclick={doSendCat} disabled={sending || !catCmd.trim()}>Send</button>
</div>

<div class="log">
  {#if mode === 'usb'}
    <div class="log-header">
      <span class="col-time">Time</span>
      <span class="col-type">Type</span>
      <span class="col-name">Control</span>
      <span class="col-val">Value</span>
    </div>
    {#each usbLog as ev}
      <div class="entry">
        <span class="time">{fmtTime(ev.ts)}</span>
        <span class="type">{ctrlType(ev.ctrl)}</span>
        <span class="name">{ev.name}</span>
        <span class="val">{ev.old} &rarr; {ev.new}</span>
      </div>
    {:else}
      <div class="empty">Waiting for USB control events... Move a knob or press a button.</div>
    {/each}
  {:else}
    <div class="log-header">
      <span class="col-time">Time</span>
      <span class="col-type">Exec</span>
      <span class="col-name">Control</span>
      <span class="col-cmd">Command</span>
      <span class="col-cat">CAT String</span>
    </div>
    {#each catEntries as ev}
      <div class="entry">
        <span class="time">{fmtTime(ev.ts)}</span>
        <span class="type cat-type">{ev.exec}</span>
        <span class="name">{ev.control}</span>
        <span class="cmd">{ev.cmd}</span>
        <span class="cat-str">{ev.cat}</span>
      </div>
    {:else}
      <div class="empty">Waiting for mapped CAT commands... Move a mapped control.</div>
    {/each}
  {/if}
</div>

<style>
  .toolbar {
    display: flex; align-items: center; gap: 0.5rem; margin-bottom: 0.75rem;
  }
  .mode-toggle {
    display: flex; border: 1px solid #1a3a6a; border-radius: 6px; overflow: hidden;
  }
  .mode-toggle button {
    padding: 0.35rem 0.75rem; border: none; background: transparent;
    color: #7a8aa8; cursor: pointer; font-size: 0.85rem; font-weight: 500;
    transition: all 0.15s;
  }
  .mode-toggle button:first-child { border-right: 1px solid #1a3a6a; }
  .mode-toggle button.active { background: #1a3a6a; color: #e0e0e0; }
  .mode-toggle button:hover:not(.active) { color: #b0c0e0; background: rgba(255,255,255,0.03); }
  .count { flex: 1; color: #7a8aa8; font-size: 0.85rem; }
  .toolbar > button {
    padding: 0.4rem 0.9rem; border: none; border-radius: 6px;
    background: #1a3a6a; color: #e0e0e0; cursor: pointer; font-size: 0.85rem;
    font-weight: 500;
    transition: background 0.2s;
  }
  .toolbar > button:hover { background: #2a4a8a; }
  .toolbar > button.active { background: #1a4a8a; color: #6ee7a0; }
  .toolbar > button.clear { background: #3a1a1a; color: #e94560; }
  .toolbar > button.clear:hover { background: #5c1a1a; }

  .log {
    background: #0a0e18;
    border: 1px solid #1a3a6a;
    border-radius: 8px;
    padding: 0;
    max-height: 70vh;
    overflow-y: auto;
    font-family: 'SF Mono', 'Fira Code', 'Cascadia Code', monospace;
    font-size: 0.85rem;
  }
  .log-header {
    display: flex; gap: 0.75rem; padding: 0.5rem 0.75rem;
    background: #131d33;
    border-bottom: 1px solid #1a3a6a;
    position: sticky; top: 0; z-index: 1;
    font-size: 0.75rem;
    font-weight: 600;
    color: #7a8aa8;
    text-transform: uppercase;
    letter-spacing: 0.06em;
  }
  .col-time { min-width: 7em; }
  .col-type { min-width: 3.5em; }
  .col-name { min-width: 10em; flex: 1; }
  .col-val { min-width: 5em; }
  .col-cmd { min-width: 10em; }
  .col-cat { min-width: 6em; }

  .entry {
    display: flex; gap: 0.75rem; padding: 0.3rem 0.75rem;
    border-bottom: 1px solid rgba(255,255,255,0.03);
    transition: background 0.15s;
  }
  .entry:hover { background: rgba(233, 69, 96, 0.04); }
  .time { color: #6b7280; min-width: 7em; }
  .type { color: #f9c74f; min-width: 3.5em; font-weight: 500; }
  .cat-type { color: #43aa8b; }
  .name { color: #e94560; min-width: 10em; flex: 1; }
  .val { color: #6ee7a0; }
  .cmd { color: #90be6d; min-width: 10em; }
  .cat-str { color: #7a9ab8; font-weight: 600; }
  .empty { color: #7a8aa8; padding: 2.5rem; text-align: center; }

  .cat-console {
    display: flex; align-items: center; gap: 0.5rem; margin-bottom: 0.75rem;
    padding: 0.5rem 0.75rem;
    background: #16213e; border: 1px solid #1a3a6a; border-radius: 8px;
  }
  .cat-label {
    font-size: 0.75rem; font-weight: 700; color: #e94560;
    text-transform: uppercase; letter-spacing: 0.08em;
  }
  .cat-console input {
    flex: 1; padding: 0.4rem 0.6rem; background: #0f0f1a; border: 1px solid #1a3a6a;
    color: #e0e0e0; border-radius: 6px; font-family: 'SF Mono', 'Fira Code', monospace;
    font-size: 0.9rem;
  }
  .cat-console input:focus { border-color: #e94560; outline: none; }
  .cat-console button {
    padding: 0.4rem 1rem; border: none; border-radius: 6px;
    background: #1a4a8a; color: #e0e0e0; cursor: pointer; font-size: 0.85rem;
    font-weight: 500; transition: background 0.2s;
  }
  .cat-console button:hover:not(:disabled) { background: #2a5aaa; color: #fff; }
  .cat-console button:disabled { opacity: 0.4; cursor: default; }
</style>
