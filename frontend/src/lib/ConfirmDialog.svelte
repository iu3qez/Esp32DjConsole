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
    background: rgba(0,0,0,0.75);
    display: flex; align-items: center; justify-content: center;
    z-index: 999;
    animation: fadeIn 0.15s ease;
  }
  @keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }
  @keyframes slideUp { from { opacity: 0; transform: translateY(12px); } to { opacity: 1; transform: translateY(0); } }
  .dialog {
    background: #16213e;
    border: 1px solid #1a3a6a;
    border-radius: 12px;
    padding: 1.5rem;
    max-width: 360px;
    width: 90vw;
    box-shadow: 0 8px 32px rgba(0,0,0,0.4);
    animation: slideUp 0.2s ease;
  }
  h3 { margin: 0 0 0.5rem; font-size: 1rem; color: #e94560; }
  p { margin: 0 0 1.25rem; color: #b0c0e0; font-size: 0.9rem; line-height: 1.5; }
  .actions { display: flex; gap: 0.5rem; justify-content: flex-end; }
  .actions button {
    padding: 0.45rem 1.1rem; border: none; border-radius: 6px;
    cursor: pointer; font-size: 0.85rem; font-weight: 500;
    transition: background 0.2s;
  }
  .cancel { background: #1a2a44; color: #b0c0e0; }
  .cancel:hover { background: #243454; }
  .confirm { background: #e94560; color: #fff; }
  .confirm:hover { background: #d13550; }
</style>
