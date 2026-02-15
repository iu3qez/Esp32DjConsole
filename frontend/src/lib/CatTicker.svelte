<script>
  import { catTicker } from './stores.js';

  let items = $state([]);

  $effect(() => {
    return catTicker.subscribe(v => items = v);
  });
</script>

{#if items.length > 0}
  <div class="ticker">
    {#each items.slice(0, 5) as item, i}
      <span class="tick" class:tx={item.dir === 'TX'} class:rx={item.dir === 'RX'}
            class:latest={i === 0}>
        <span class="dir">{item.dir}</span>
        <span class="cmd">{item.cat?.replace(/;$/, '')}</span>
      </span>
    {/each}
  </div>
{/if}

<style>
  .ticker {
    display: flex;
    align-items: center;
    gap: 0.35rem;
    padding: 0.25rem 1.25rem;
    background: #0a0e18;
    border-bottom: 1px solid #1a3a6a33;
    overflow: hidden;
    font-family: 'SF Mono', 'Fira Code', 'Cascadia Code', monospace;
    font-size: 0.75rem;
    min-height: 1.6rem;
  }
  .tick {
    display: inline-flex;
    align-items: center;
    gap: 0.3rem;
    padding: 1px 6px;
    border-radius: 3px;
    white-space: nowrap;
    transition: opacity 0.3s;
  }
  .tick:not(.latest) { opacity: 0.45; }
  .tick.latest { opacity: 1; }
  .tick.tx { background: rgba(233, 69, 96, 0.1); }
  .tick.rx { background: rgba(82, 183, 136, 0.1); }
  .dir {
    font-weight: 700;
    font-size: 0.65rem;
    letter-spacing: 0.05em;
  }
  .tx .dir { color: #e94560; }
  .rx .dir { color: #52b788; }
  .cmd { color: #b0c0e0; }
  .tick.latest .cmd { color: #e0e0e0; font-weight: 500; }
</style>
