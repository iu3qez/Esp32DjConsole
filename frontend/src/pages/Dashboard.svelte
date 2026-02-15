<script>
  import { onMount } from 'svelte';
  import { status } from '../lib/stores.js';
  import { getStatus } from '../lib/api.js';

  let st = $state({});

  $effect(() => { return status.subscribe(v => st = v); });

  onMount(async () => {
    try {
      const s = await getStatus();
      status.set(s);
    } catch (_) {}
  });
</script>

<section class="cards">
  <div class="card">
    <h3>USB Console</h3>
    <div class="val" class:ok={st.usb_connected} class:err={!st.usb_connected}>
      <span class="dot"></span>
      {st.usb_connected ? 'Connected' : 'Disconnected'}
    </div>
    {#if st.usb_updates !== undefined}
      <small>{st.usb_changes ?? 0} changes / {st.usb_updates} packets</small>
    {/if}
  </div>

  <div class="card">
    <h3>CAT</h3>
    <div class="val" class:ok={st.cat_state === 'connected'} class:warn={st.cat_state === 'connecting'}>
      <span class="dot"></span>
      {st.cat_state ?? 'disconnected'}
    </div>
  </div>

  <div class="card">
    <h3>WiFi</h3>
    <div class="val" class:ok={st.wifi_connected} class:warn={st.ap_mode}>
      <span class="dot"></span>
      {#if st.ap_mode}
        AP Mode
      {:else if st.wifi_connected}
        Connected
      {:else}
        Disconnected
      {/if}
    </div>
  </div>
</section>

<section class="sys">
  <small>Heap: {st.free_heap ? (st.free_heap / 1024).toFixed(0) + ' KB free' : '---'}
  {st.min_free_heap ? ' (min ' + (st.min_free_heap / 1024).toFixed(0) + ' KB)' : ''}</small>
</section>

<style>
  .cards {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: 0.75rem;
    margin-bottom: 1rem;
  }
  .card {
    background: #16213e;
    border-radius: 8px;
    padding: 1rem;
    border: 1px solid #1a3a6a;
    box-shadow: 0 2px 8px rgba(0,0,0,0.3);
    transition: border-color 0.2s;
  }
  .card:hover { border-color: #2a4a7a; }
  .card h3 {
    margin: 0 0 0.5rem;
    font-size: 0.8rem;
    color: #7a8aa8;
    text-transform: uppercase;
    letter-spacing: 0.06em;
  }
  .val {
    font-size: 1rem;
    font-weight: 600;
    color: #9ca3af;
    text-transform: capitalize;
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .dot {
    width: 8px; height: 8px; border-radius: 50%;
    background: #555;
    flex-shrink: 0;
  }
  .val.ok { color: #6ee7a0; }
  .val.ok .dot { background: #52b788; box-shadow: 0 0 6px #52b788; }
  .val.err { color: #e94560; }
  .val.err .dot { background: #e94560; box-shadow: 0 0 6px rgba(233,69,96,0.5); }
  .val.warn { color: #f9c74f; }
  .val.warn .dot { background: #f9c74f; box-shadow: 0 0 6px rgba(249,199,79,0.4); }
  .card small { color: #7a8aa8; font-size: 0.8rem; margin-top: 0.3rem; display: block; }

  .sys { margin-top: 0.75rem; color: #7a8aa8; text-align: center; font-size: 0.85rem; }
</style>
