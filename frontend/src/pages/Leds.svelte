<script>
  import { onMount } from 'svelte';
  import { getLeds, setLed, ledsAllOff, ledsTest } from '../lib/api.js';
  import { ledStates } from '../lib/stores.js';

  const LED_LAYOUT = [
    { label: 'Deck A', leds: [
      { note: 1,  name: 'N1' },
      { note: 2,  name: 'N2' },
      { note: 3,  name: 'N3' },
      { note: 4,  name: 'N4' },
      { note: 5,  name: 'N5' },
      { note: 6,  name: 'N6' },
      { note: 7,  name: 'N7' },
      { note: 8,  name: 'N8' },
      { note: 10, name: 'Pitch-' },
      { note: 11, name: 'Pitch+' },
      { note: 14, name: 'CUE' },
      { note: 15, name: 'Play' },
      { note: 16, name: 'Listen' },
      { note: 18, name: 'Sync' },
      { note: 19, name: 'Master' },
    ]},
    { label: 'Deck B', leds: [
      { note: 20, name: 'N1' },
      { note: 21, name: 'N2' },
      { note: 22, name: 'N3' },
      { note: 23, name: 'N4' },
      { note: 24, name: 'N5' },
      { note: 25, name: 'N6' },
      { note: 26, name: 'N7' },
      { note: 27, name: 'N8' },
      { note: 30, name: 'Pitch-' },
      { note: 31, name: 'Pitch+' },
      { note: 34, name: 'CUE' },
      { note: 35, name: 'Play' },
      { note: 36, name: 'Listen' },
      { note: 38, name: 'Sync' },
      { note: 39, name: 'Master' },
    ]},
    { label: 'Global', leds: [
      { note: 40, name: 'Up' },
      { note: 41, name: 'Down' },
      { note: 45, name: 'Scratch' },
      { note: 46, name: 'Automix' },
    ]},
  ];

  let states = $state({});
  let testing = $state(false);

  $effect(() => { return ledStates.subscribe(v => states = v); });

  onMount(async () => {
    try {
      const leds = await getLeds();
      const s = {};
      for (const led of leds) {
        s[led.note] = led.state;
      }
      ledStates.set(s);
    } catch (e) {
      console.warn('Failed to fetch LED states:', e);
    }
  });

  function getState(note) {
    return states[note] || 'off';
  }

  async function toggleLed(note) {
    const current = getState(note);
    let next;
    if (current === 'off') next = 'on';
    else if (current === 'on') next = 'blink';
    else next = 'off';
    try {
      await setLed(note, next);
    } catch (e) {
      console.error('Failed to set LED:', e);
    }
  }

  async function allOff() {
    try {
      await ledsAllOff();
    } catch (e) {
      console.error('Failed to turn off LEDs:', e);
    }
  }

  async function runTest() {
    testing = true;
    try {
      await ledsTest();
    } catch (e) {
      console.error('LED test failed:', e);
    }
    setTimeout(() => testing = false, 3000);
  }
</script>

<div class="leds-page">
  <div class="toolbar">
    <button class="btn" onclick={allOff}>All Off</button>
    <button class="btn test" onclick={runTest} disabled={testing}>
      {testing ? 'Testing...' : 'Test Sweep'}
    </button>
  </div>

  {#each LED_LAYOUT as group}
    <div class="group">
      <h3>{group.label}</h3>
      <div class="led-grid">
        {#each group.leds as led}
          {@const st = getState(led.note)}
          <button
            class="led-btn"
            class:on={st === 'on'}
            class:blink={st === 'blink'}
            onclick={() => toggleLed(led.note)}
            title="Note {led.note}: {st}"
          >
            <span class="led-indicator"></span>
            <span class="led-name">{led.name}</span>
          </button>
        {/each}
      </div>
    </div>
  {/each}

  <p class="hint">Click: off &rarr; on &rarr; blink &rarr; off</p>
</div>

<style>
  .leds-page { display: flex; flex-direction: column; gap: 1rem; }

  .toolbar {
    display: flex;
    gap: 0.5rem;
  }
  .btn {
    padding: 0.45rem 1rem;
    border: none;
    border-radius: 6px;
    background: #1a3a6a;
    color: #e0e0e0;
    cursor: pointer;
    font-size: 0.85rem;
    font-weight: 500;
    transition: background 0.2s;
  }
  .btn:hover { background: #2a4a8a; }
  .btn:disabled { opacity: 0.5; cursor: not-allowed; }
  .btn.test { background: #1a4a8a; }
  .btn.test:hover { background: #2a5aaa; }

  .group {
    background: #16213e;
    border: 1px solid #1a3a6a;
    border-radius: 8px;
    padding: 0.75rem 1rem;
    box-shadow: 0 2px 8px rgba(0,0,0,0.3);
  }
  .group h3 {
    margin: 0 0 0.5rem 0;
    font-size: 0.8rem;
    color: #7a8aa8;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    border-bottom: 1px solid #1a3a6a44;
    padding-bottom: 0.4rem;
  }

  .led-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(70px, 1fr));
    gap: 6px;
  }

  .led-btn {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 4px;
    padding: 8px 4px;
    border: 1px solid #1a3a6a;
    border-radius: 8px;
    background: #0f0f1a;
    color: #7a8aa8;
    cursor: pointer;
    transition: all 0.15s;
  }
  .led-btn:hover { background: #1a2240; border-color: #2a4a7a; }

  .led-indicator {
    width: 12px;
    height: 12px;
    border-radius: 50%;
    background: #2a2a3e;
    transition: all 0.15s;
  }

  .led-btn.on .led-indicator {
    background: #ff4444;
    box-shadow: 0 0 8px #ff4444;
  }
  .led-btn.on { border-color: #5c2a2a; color: #e0e0e0; }

  .led-btn.blink .led-indicator {
    background: #ff8800;
    box-shadow: 0 0 8px #ff8800;
    animation: blink-pulse 0.8s infinite;
  }
  .led-btn.blink { border-color: #5c4422; color: #e0e0e0; }

  @keyframes blink-pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.2; }
  }

  .led-name {
    font-size: 0.75rem;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    max-width: 60px;
  }

  .hint {
    font-size: 0.8rem;
    color: #7a8aa8;
    text-align: center;
    margin-top: 0.5rem;
  }
</style>
