<script>
  import { onMount } from 'svelte';

  let { message = '', type = 'info', duration = 3000, onclose } = $props();
  let visible = $state(true);

  onMount(() => {
    const timer = setTimeout(() => { visible = false; onclose?.(); }, duration);
    return () => clearTimeout(timer);
  });
</script>

{#if visible}
  <div class="toast {type}" role="alert">
    <span>{message}</span>
    <button onclick={() => { visible = false; onclose?.(); }}>&times;</button>
  </div>
{/if}

<style>
  .toast {
    position: fixed;
    bottom: 1rem;
    left: 50%;
    transform: translateX(-50%);
    padding: 0.6rem 1.2rem;
    border-radius: 8px;
    display: flex;
    align-items: center;
    gap: 0.75rem;
    font-size: 0.85rem;
    z-index: 1000;
    animation: slide-up 0.25s ease-out;
    max-width: 90vw;
  }
  .toast.success { background: #2d6a4f; color: #b7e4c7; }
  .toast.error { background: #7a1a1a; color: #f8b4b4; }
  .toast.info { background: #1a3a5c; color: #b0d4f1; }
  .toast button {
    background: none; border: none; color: inherit;
    cursor: pointer; font-size: 1.1rem; padding: 0; line-height: 1;
  }
  @keyframes slide-up {
    from { transform: translateX(-50%) translateY(1rem); opacity: 0; }
    to { transform: translateX(-50%) translateY(0); opacity: 1; }
  }
</style>
