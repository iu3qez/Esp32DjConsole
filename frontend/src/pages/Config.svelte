<script>
  import { onMount } from 'svelte';
  import { getConfig, putConfig } from '../lib/api.js';
  import { success, error } from '../lib/toast.js';
  import ConfirmDialog from '../lib/ConfirmDialog.svelte';

  let cfg = $state({
    wifi_ssid: '', wifi_pass: '', wifi_pass_set: false,
    cat_host: '', cat_port: 31001,
    debug_level: 1,
  });
  let saving = $state(false);
  let newPass = $state('');
  let showConfirm = $state(false);

  onMount(async () => {
    try { cfg = { ...cfg, ...await getConfig() }; }
    catch (e) { error('Failed to load config: ' + e.message); }
  });

  function requestSave() { showConfirm = true; }

  async function doSave() {
    showConfirm = false;
    saving = true;
    const update = {
      wifi_ssid: cfg.wifi_ssid,
      cat_host: cfg.cat_host,
      cat_port: cfg.cat_port,
      debug_level: cfg.debug_level,
    };
    if (newPass) update.wifi_pass = newPass;
    try {
      const res = await putConfig(update);
      if (res.restart_required) {
        success('Saved. Device will restart to apply changes.');
      } else {
        success('Configuration saved.');
      }
      newPass = '';
    } catch (e) { error('Save failed: ' + e.message); }
    saving = false;
  }
</script>

<section class="panel">
  <h2>Network</h2>
  <div class="form">
    <label>WiFi SSID
      <input type="text" bind:value={cfg.wifi_ssid} />
    </label>
    <label>WiFi Password
      <input type="password" bind:value={newPass} placeholder={cfg.wifi_pass_set ? '(set - enter to change)' : '(not set)'} />
    </label>
  </div>
</section>

<section class="panel">
  <h2>Thetis CAT Connection</h2>
  <div class="form">
    <label>Host <input type="text" bind:value={cfg.cat_host} placeholder="192.168.1.100" /></label>
    <label>Port <input type="number" bind:value={cfg.cat_port} /></label>
  </div>
</section>

<section class="panel">
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
</section>

<button class="save" onclick={requestSave} disabled={saving}>{saving ? 'Saving...' : 'Save Configuration'}</button>

{#if showConfirm}
  <ConfirmDialog
    title="Save Configuration"
    message="Changing WiFi or CAT settings may require a device restart. Continue?"
    onconfirm={doSave}
    oncancel={() => showConfirm = false}
  />
{/if}

<style>
  h2 { font-size: 1rem; color: #e94560; margin: 0 0 0.75rem; }
  .form { display: flex; flex-direction: column; gap: 0.6rem; }
  label {
    display: flex; flex-direction: column; gap: 0.3rem;
    font-size: 0.85rem; color: #7a8aa8;
  }
  input, select {
    padding: 0.5rem 0.6rem; background: #0f0f1a; border: 1px solid #1a3a6a;
    color: #e0e0e0; border-radius: 6px; font-size: 0.9rem;
    transition: border-color 0.2s;
  }
  input:focus, select:focus { border-color: #e94560; outline: none; }
  input::placeholder { color: #4a5568; }
  .save {
    margin-top: 0.5rem; width: 100%; padding: 0.65rem;
    background: #1a4a8a; color: #e0e0e0; border: none;
    border-radius: 8px; cursor: pointer; font-size: 0.9rem;
    font-weight: 500;
    transition: background 0.2s;
  }
  .save:hover { background: #2a5aaa; }
  .save:disabled { opacity: 0.5; cursor: not-allowed; }
</style>
