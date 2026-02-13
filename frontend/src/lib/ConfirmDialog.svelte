<script>
  let { title = 'Confirm', message = '', onconfirm, oncancel } = $props();
</script>

<div class="overlay" onclick={oncancel} role="presentation">
  <!-- svelte-ignore a11y_interactive_supports_focus a11y_click_events_have_key_events -->
  <div class="dialog" onclick={(e) => e.stopPropagation()} role="dialog" aria-labelledby="dlg-title">
    <h3 id="dlg-title">{title}</h3>
    <p>{message}</p>
    <div class="actions">
      <button class="cancel" onclick={oncancel}>Cancel</button>
      <button class="confirm" onclick={onconfirm}>Confirm</button>
    </div>
  </div>
</div>

<style>
  .overlay {
    position: fixed; inset: 0;
    background: rgba(0,0,0,0.6);
    display: flex; align-items: center; justify-content: center;
    z-index: 999;
  }
  .dialog {
    background: #16213e;
    border: 1px solid #0f3460;
    border-radius: 10px;
    padding: 1.25rem;
    max-width: 360px;
    width: 90vw;
  }
  h3 { margin: 0 0 0.5rem; font-size: 1rem; color: #e94560; }
  p { margin: 0 0 1rem; color: #ccc; font-size: 0.85rem; }
  .actions { display: flex; gap: 0.5rem; justify-content: flex-end; }
  .actions button {
    padding: 0.4rem 1rem; border: none; border-radius: 6px;
    cursor: pointer; font-size: 0.85rem;
  }
  .cancel { background: #333; color: #ccc; }
  .cancel:hover { background: #444; }
  .confirm { background: #e94560; color: #fff; }
  .confirm:hover { background: #c73550; }
</style>
