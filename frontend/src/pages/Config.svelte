<script>
  import { onMount } from 'svelte';
  import { getConfig, putConfig } from '../lib/api.js';

  let cfg = $state({
    wifi_ssid: '', wifi_pass: '', wifi_pass_set: false,
    protocol: 'tci',
    tci_host: '', tci_port: 50001,
    cat_host: '', cat_port: 31001,
    debug_level: 1,
  });
  let saving = $state(false);
  let msg = $state('');
  let newPass = $state('');

  onMount(async () => {
    try { cfg = { ...cfg, ...await getConfig() }; } catch (e) { msg = 'Load failed: ' + e.message; }
  });

  async function save() {
    saving = true; msg = '';
    const update = {
      wifi_ssid: cfg.wifi_ssid,
      protocol: cfg.protocol,
      tci_host: cfg.tci_host,
      tci_port: cfg.tci_port,
      cat_host: cfg.cat_host,
      cat_port: cfg.cat_port,
      debug_level: cfg.debug_level,
    };
    if (newPass) update.wifi_pass = newPass;
    try {
      const res = await putConfig(update);
      msg = res.restart_required ? 'Saved. WiFi restarting...' : 'Saved.';
      newPass = '';
    } catch (e) { msg = 'Save failed: ' + e.message; }
    saving = false;
  }
</script>

<h2>Network</h2>
<div class="form">
  <label>WiFi SSID
    <input type="text" bind:value={cfg.wifi_ssid} />
  </label>
  <label>WiFi Password
    <input type="password" bind:value={newPass} placeholder={cfg.wifi_pass_set ? '(set - enter to change)' : '(not set)'} />
  </label>
</div>

<h2>Radio Connection</h2>
<div class="form">
  <label>Protocol
    <select bind:value={cfg.protocol}>
      <option value="tci">TCI (WebSocket)</option>
      <option value="cat">CAT (TCP)</option>
    </select>
  </label>

  <fieldset>
    <legend>TCI</legend>
    <label>Host <input type="text" bind:value={cfg.tci_host} placeholder="192.168.1.100" /></label>
    <label>Port <input type="number" bind:value={cfg.tci_port} /></label>
  </fieldset>

  <fieldset>
    <legend>CAT</legend>
    <label>Host <input type="text" bind:value={cfg.cat_host} placeholder="192.168.1.100" /></label>
    <label>Port <input type="number" bind:value={cfg.cat_port} /></label>
  </fieldset>
</div>

<h2>Debug</h2>
<div class="form">
  <label>USB Debug Level
    <select bind:value={cfg.debug_level}>
      <option value={0}>Off</option>
      <option value={1}>Control changes</option>
      <option value={2}>Changes + hex diff</option>
      <option value={3}>Full hex dump</option>
    </select>
  </label>
</div>

{#if msg}<div class="msg">{msg}</div>{/if}

<button class="save" onclick={save} disabled={saving}>{saving ? 'Saving...' : 'Save Configuration'}</button>

<style>
  h2 { font-size: 0.9rem; color: #e94560; margin: 1rem 0 0.5rem; }
  .form { display: flex; flex-direction: column; gap: 0.5rem; }
  label { display: flex; flex-direction: column; gap: 0.2rem; font-size: 0.8rem; color: #888; }
  input, select {
    padding: 0.4rem; background: #16213e; border: 1px solid #0f3460;
    color: #e0e0e0; border-radius: 4px; font-size: 0.85rem;
  }
  fieldset {
    border: 1px solid #0f3460; border-radius: 6px; padding: 0.5rem 0.75rem;
    display: flex; flex-direction: column; gap: 0.4rem;
  }
  legend { color: #888; font-size: 0.75rem; padding: 0 0.3rem; }
  .msg { margin-top: 0.75rem; padding: 0.4rem; background: #16213e; border-radius: 4px; color: #52b788; font-size: 0.8rem; }
  .save {
    margin-top: 1rem; width: 100%; padding: 0.6rem;
    background: #0f3460; color: #e0e0e0; border: none;
    border-radius: 6px; cursor: pointer; font-size: 0.9rem;
  }
  .save:hover { background: #1a4a8a; }
  .save:disabled { opacity: 0.5; cursor: not-allowed; }
</style>
