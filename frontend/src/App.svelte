<script>
  import { onMount } from 'svelte';
  import { initStores, wsConnected } from './lib/stores.js';
  import { toasts, dismissToast } from './lib/toast.js';
  import Toast from './lib/Toast.svelte';
  import Dashboard from './pages/Dashboard.svelte';
  import Mappings from './pages/Mappings.svelte';
  import Config from './pages/Config.svelte';
  import Debug from './pages/Debug.svelte';
  import Leds from './pages/Leds.svelte';

  const tabs = [
    { id: 'dashboard', label: 'Dashboard', component: Dashboard },
    { id: 'mappings', label: 'Mappings', component: Mappings },
    { id: 'leds', label: 'LEDs', component: Leds },
    { id: 'config', label: 'Config', component: Config },
    { id: 'debug', label: 'Debug', component: Debug },
  ];

  let active = $state('dashboard');
  let connected = $state(false);
  let toastList = $state([]);

  $effect(() => { return wsConnected.subscribe(v => connected = v); });
  $effect(() => { return toasts.subscribe(v => toastList = v); });

  onMount(() => { initStores(); });
</script>

<header>
  <h1>DJ Console</h1>
  <span class="ws-badge" class:ok={connected}>{connected ? 'Live' : 'Offline'}</span>
</header>

<nav>
  {#each tabs as tab}
    <button
      class:active={active === tab.id}
      onclick={() => active = tab.id}
    >{tab.label}</button>
  {/each}
</nav>

<main>
  {#each tabs as tab}
    {#if active === tab.id}
      <tab.component />
    {/if}
  {/each}
</main>

{#each toastList as t (t.id)}
  <Toast message={t.message} type={t.type} duration={t.duration}
    onclose={() => dismissToast(t.id)} />
{/each}

<style>
  :global(body) {
    margin: 0;
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', system-ui, sans-serif;
    background: #0f0f1a;
    color: #e0e0e0;
    font-size: 15px;
    line-height: 1.5;
  }
  :global(*, *::before, *::after) { box-sizing: border-box; }

  :global(:focus-visible) {
    outline: 2px solid #e94560;
    outline-offset: 2px;
  }

  :global(.panel) {
    background: #16213e;
    border: 1px solid #1a3a6a;
    border-radius: 8px;
    padding: 1rem;
    margin-bottom: 1rem;
    box-shadow: 0 2px 8px rgba(0,0,0,0.3);
  }
  :global(.panel h2) {
    margin: 0 0 0.75rem;
    font-size: 1rem;
    color: #e94560;
    letter-spacing: 0.02em;
  }

  header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0.75rem 1.25rem;
    background: linear-gradient(180deg, #1a2744 0%, #16213e 100%);
    border-bottom: 1px solid #1a3a6a;
  }
  header h1 {
    margin: 0; font-size: 1.2rem; color: #e94560;
    letter-spacing: 0.03em;
    text-shadow: 0 0 20px rgba(233, 69, 96, 0.3);
  }

  .ws-badge {
    font-size: 0.8rem;
    padding: 3px 10px;
    border-radius: 10px;
    background: #3a2020;
    color: #e94560;
    display: flex; align-items: center; gap: 6px;
  }
  .ws-badge::before {
    content: ''; width: 7px; height: 7px; border-radius: 50%;
    background: #e94560;
  }
  .ws-badge.ok { background: #1a3a2a; color: #6ee7a0; }
  .ws-badge.ok::before { background: #52b788; box-shadow: 0 0 6px #52b788; }

  nav {
    display: flex;
    gap: 0;
    background: #131d33;
    border-bottom: 2px solid #1a3a6a;
  }
  nav button {
    flex: 1;
    padding: 0.7rem 0;
    border: none;
    background: transparent;
    color: #7a8aa8;
    cursor: pointer;
    font-size: 0.85rem;
    font-weight: 500;
    border-bottom: 2px solid transparent;
    transition: color 0.2s, border-color 0.2s, background 0.2s;
  }
  nav button:hover { color: #b0c0e0; background: rgba(255,255,255,0.03); }
  nav button.active {
    color: #e94560;
    border-bottom-color: #e94560;
    background: rgba(233, 69, 96, 0.06);
  }

  main { padding: 1rem; max-width: 800px; margin: 0 auto; }
</style>
