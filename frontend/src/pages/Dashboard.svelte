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
      {st.usb_connected ? 'Connected' : 'Disconnected'}
    </div>
    {#if st.usb_updates !== undefined}
      <small>{st.usb_changes ?? 0} changes / {st.usb_updates} packets</small>
    {/if}
  </div>

  <div class="card">
    <h3>CAT</h3>
    <div class="val" class:ok={st.cat_state === 'connected'} class:warn={st.cat_state === 'connecting'}>
      {st.cat_state ?? 'disconnected'}
    </div>
  </div>

  <div class="card">
    <h3>WiFi</h3>
    <div class="val" class:ok={st.wifi_connected} class:warn={st.ap_mode}>
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
  .cards { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 0.75rem; margin-bottom: 1rem; }
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

  .sys { margin-top: 0.75rem; color: #555; text-align: center; }
</style>
