<script>
  import { onMount } from 'svelte';
  import { status, radio } from '../lib/stores.js';
  import { getStatus } from '../lib/api.js';

  let st = $state({});
  let rd = $state({});

  $effect(() => { return status.subscribe(v => st = v); });
  $effect(() => { return radio.subscribe(v => rd = v); });

  onMount(async () => {
    try {
      const s = await getStatus();
      status.set(s);
      if (s.radio) radio.set(s.radio);
    } catch (_) {}
  });

  function fmtFreq(hz) {
    if (!hz) return '---';
    const mhz = (hz / 1e6).toFixed(6);
    return mhz + ' MHz';
  }
</script>

<section class="cards">
  <div class="card">
    <h3>USB Console</h3>
    <div class="val" class:ok={st.usb_connected} class:err={!st.usb_connected}>
      {st.usb_connected ? 'Connected' : 'Disconnected'}
    </div>
    {#if st.usb_updates !== undefined}
      <small>{st.usb_changes ?? 0} changes / {st.usb_updates} packets</small>
    {/if}
  </div>

  <div class="card">
    <h3>TCI</h3>
    <div class="val" class:ok={st.tci_state === 'ready'} class:warn={st.tci_state === 'connecting'}>
      {st.tci_state ?? 'unknown'}
    </div>
  </div>

  <div class="card">
    <h3>CAT</h3>
    <div class="val" class:ok={st.cat_state === 'connected'} class:warn={st.cat_state === 'connecting'}>
      {st.cat_state ?? 'unknown'}
    </div>
  </div>

  <div class="card">
    <h3>Protocol</h3>
    <div class="val">{(st.protocol ?? 'tci').toUpperCase()}</div>
  </div>
</section>

<section class="radio">
  <h2>Radio State</h2>
  <div class="grid">
    <div><span class="lbl">VFO A</span><span>{fmtFreq(rd.vfo_a)}</span></div>
    <div><span class="lbl">VFO B</span><span>{fmtFreq(rd.vfo_b)}</span></div>
    <div><span class="lbl">Mode</span><span>{rd.mode || '---'}</span></div>
    <div><span class="lbl">Drive</span><span>{rd.drive ?? '---'}%</span></div>
    <div><span class="lbl">TX</span><span class:tx={rd.tx}>{rd.tx ? 'TX' : 'RX'}</span></div>
    <div><span class="lbl">Filter</span><span>{rd.filter_low ?? '?'} - {rd.filter_high ?? '?'} Hz</span></div>
  </div>
</section>

<section class="sys">
  <small>Heap: {st.free_heap ? (st.free_heap / 1024).toFixed(0) + ' KB free' : '---'}
  {st.min_free_heap ? ' (min ' + (st.min_free_heap / 1024).toFixed(0) + ' KB)' : ''}</small>
</section>

<style>
  .cards { display: grid; grid-template-columns: 1fr 1fr; gap: 0.75rem; margin-bottom: 1rem; }
  .card {
    background: #16213e;
    border-radius: 8px;
    padding: 0.75rem;
    border: 1px solid #0f3460;
  }
  .card h3 { margin: 0 0 0.3rem; font-size: 0.8rem; color: #888; text-transform: uppercase; }
  .val { font-size: 1rem; font-weight: 600; color: #ccc; text-transform: capitalize; }
  .val.ok { color: #52b788; }
  .val.err { color: #e94560; }
  .val.warn { color: #f9c74f; }
  .card small { color: #666; font-size: 0.7rem; }

  .radio { background: #16213e; border-radius: 8px; padding: 0.75rem; border: 1px solid #0f3460; }
  .radio h2 { margin: 0 0 0.5rem; font-size: 0.9rem; color: #e94560; }
  .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 0.4rem; }
  .grid div { display: flex; justify-content: space-between; padding: 0.2rem 0; }
  .grid .lbl { color: #888; font-size: 0.8rem; }
  .grid span { color: #e0e0e0; font-family: monospace; }
  .tx { color: #e94560; font-weight: bold; }

  .sys { margin-top: 0.75rem; color: #555; text-align: center; }
</style>
