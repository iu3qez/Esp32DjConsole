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
    background: #1a1a2e;
    color: #e0e0e0;
    font-size: 14px;
  }
  :global(*, *::before, *::after) { box-sizing: border-box; }

  header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0.5rem 1rem;
    background: #16213e;
    border-bottom: 1px solid #0f3460;
  }
  header h1 { margin: 0; font-size: 1.2rem; color: #e94560; }

  .ws-badge {
    font-size: 0.75rem;
    padding: 2px 8px;
    border-radius: 10px;
    background: #555;
    color: #ccc;
  }
  .ws-badge.ok { background: #2d6a4f; color: #b7e4c7; }

  nav {
    display: flex;
    gap: 0;
    background: #16213e;
    border-bottom: 2px solid #0f3460;
  }
  nav button {
    flex: 1;
    padding: 0.6rem 0;
    border: none;
    background: transparent;
    color: #999;
    cursor: pointer;
    font-size: 0.85rem;
    border-bottom: 2px solid transparent;
    transition: all 0.2s;
  }
  nav button:hover { color: #ccc; }
  nav button.active { color: #e94560; border-bottom-color: #e94560; }

  main { padding: 1rem; max-width: 800px; margin: 0 auto; }
</style>
