<script>
  import { controlLog } from '../lib/stores.js';

  let log = $state([]);
  let paused = $state(false);

  $effect(() => {
    return controlLog.subscribe(v => { if (!paused) log = v; });
  });

  function clear() { controlLog.set([]); log = []; }

  function ctrlType(t) {
    return ['BTN', 'DIAL', 'ENC'][t] ?? '?';
  }

  function fmtTime(ts) {
    const d = new Date(ts);
    return d.toLocaleTimeString('en-US', { hour12: false, fractionalSecondDigits: 1 });
  }
</script>

<div class="toolbar">
  <span class="count">{log.length} events</span>
  <button onclick={() => paused = !paused}>{paused ? 'Resume' : 'Pause'}</button>
  <button class="clear" onclick={clear}>Clear</button>
</div>

<div class="log">
  {#each log as ev}
    <div class="entry">
      <span class="time">{fmtTime(ev.ts)}</span>
      <span class="type">{ctrlType(ev.ctrl)}</span>
      <span class="name">{ev.name}</span>
      <span class="val">{ev.old} &rarr; {ev.new}</span>
    </div>
  {:else}
    <div class="empty">Waiting for control events... Move a knob or press a button on the DJ console.</div>
  {/each}
</div>

<style>
  .toolbar {
    display: flex; align-items: center; gap: 0.5rem; margin-bottom: 0.5rem;
  }
  .count { flex: 1; color: #888; font-size: 0.8rem; }
  .toolbar button {
    padding: 0.3rem 0.8rem; border: none; border-radius: 4px;
    background: #0f3460; color: #e0e0e0; cursor: pointer; font-size: 0.8rem;
  }
  .toolbar button:hover { background: #1a4a8a; }
  .toolbar button.clear { background: #5c1a1a; }
  .toolbar button.clear:hover { background: #8a2a2a; }

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
  .name { color: #e94560; min-width: 10em; }
  .val { color: #52b788; }
  .empty { color: #555; padding: 2rem; text-align: center; }
</style>
