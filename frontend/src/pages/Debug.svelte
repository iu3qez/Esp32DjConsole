<script>
  import { controlLog, catLog } from '../lib/stores.js';

  let mode = $state('usb');  // 'usb' or 'cat'
  let usbLog = $state([]);
  let catEntries = $state([]);
  let paused = $state(false);

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
</script>

<div class="toolbar">
  <div class="mode-toggle">
    <button class:active={mode === 'usb'} onclick={() => mode = 'usb'}>USB Events</button>
    <button class:active={mode === 'cat'} onclick={() => mode = 'cat'}>CAT Sent</button>
  </div>
  <span class="count">{activeLog().length} events</span>
  <button onclick={() => paused = !paused}>{paused ? 'Resume' : 'Pause'}</button>
  <button class="clear" onclick={clear}>Clear</button>
</div>

<div class="log">
  {#if mode === 'usb'}
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
    display: flex; align-items: center; gap: 0.5rem; margin-bottom: 0.5rem;
  }
  .mode-toggle {
    display: flex; border: 1px solid #0f3460; border-radius: 4px; overflow: hidden;
  }
  .mode-toggle button {
    padding: 0.3rem 0.7rem; border: none; background: transparent;
    color: #888; cursor: pointer; font-size: 0.8rem; transition: all 0.15s;
  }
  .mode-toggle button:first-child { border-right: 1px solid #0f3460; }
  .mode-toggle button.active { background: #0f3460; color: #e0e0e0; }
  .mode-toggle button:hover:not(.active) { color: #ccc; }
  .count { flex: 1; color: #888; font-size: 0.8rem; }
  .toolbar > button {
    padding: 0.3rem 0.8rem; border: none; border-radius: 4px;
    background: #0f3460; color: #e0e0e0; cursor: pointer; font-size: 0.8rem;
  }
  .toolbar > button:hover { background: #1a4a8a; }
  .toolbar > button.clear { background: #5c1a1a; }
  .toolbar > button.clear:hover { background: #8a2a2a; }

  .log {
    background: #0d1117;
    border: 1px solid #0f3460;
    border-radius: 6px;
    padding: 0.5rem;
    max-height: 70vh;
    overflow-y: auto;
    font-family: monospace;
    font-size: 0.8rem;
  }
  .entry {
    display: flex; gap: 0.75rem; padding: 0.15rem 0;
    border-bottom: 1px solid #ffffff08;
  }
  .time { color: #555; min-width: 7em; }
  .type { color: #f9c74f; min-width: 3em; }
  .cat-type { color: #43aa8b; }
  .name { color: #e94560; min-width: 10em; }
  .val { color: #52b788; }
  .cmd { color: #90be6d; min-width: 10em; }
  .cat-str { color: #577590; font-weight: bold; }
  .empty { color: #555; padding: 2rem; text-align: center; }
</style>
