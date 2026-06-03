<script lang="ts">
  const LIVE_URL = 'https://lukaszpg.github.io/TheArchivistSoE/';
  const CACHE_URL = '/soe-wiki-cache/index.html';

  let source = $state<'live' | 'cache'>('live');
  let frameKey = $state(0);
  let frameLoaded = $state(false);

  const currentUrl = $derived(source === 'live' ? LIVE_URL : CACHE_URL);

  function refresh(): void {
    frameLoaded = false;
    source = 'live';
    frameKey += 1;
  }

  function useCache(): void {
    frameLoaded = false;
    source = 'cache';
    frameKey += 1;
  }
</script>

<section class="tab-content wiki-tab">
  <div class="wiki-toolbar">
    <div class="wiki-status">
      <span class="wiki-title">SoE Wiki</span>
      <span class="wiki-source">{source === 'live' ? 'Live' : 'Bundled cache'}</span>
    </div>
    <div class="wiki-actions">
      <button type="button" class="wiki-button" onclick={useCache}>Use cached copy</button>
      <button type="button" class="wiki-button primary" onclick={refresh}>Refresh page</button>
    </div>
  </div>

  <div class="wiki-frame-wrap">
    {#if !frameLoaded}
      <div class="wiki-loading">Loading wiki...</div>
    {/if}
    {#key frameKey}
      <iframe
        class="wiki-frame"
        title="SoE Wiki"
        src={currentUrl}
        onload={() => { frameLoaded = true; }}
      ></iframe>
    {/key}
  </div>
</section>

<style>
  .wiki-tab {
    display: flex;
    flex-direction: column;
    gap: 10px;
    height: 100%;
    min-height: 0;
    padding: 0;
  }

  .wiki-toolbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    padding: 10px 12px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
  }

  .wiki-status {
    display: flex;
    align-items: baseline;
    gap: 10px;
  }

  .wiki-title {
    color: var(--text-primary);
    font-size: 14px;
    font-weight: 700;
  }

  .wiki-source {
    color: var(--text-muted);
    font-size: 12px;
  }

  .wiki-actions {
    display: flex;
    gap: 8px;
  }

  .wiki-button {
    border: 1px solid var(--border-primary);
    border-radius: 6px;
    background: var(--bg-tertiary);
    color: var(--text-primary);
    cursor: pointer;
    font: inherit;
    font-size: 12px;
    font-weight: 600;
    padding: 6px 10px;
  }

  .wiki-button.primary {
    border-color: var(--accent-primary);
    color: var(--accent-primary);
  }

  .wiki-frame-wrap {
    position: relative;
    flex: 1;
    min-height: 0;
    overflow: hidden;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: #050608;
  }

  .wiki-loading {
    position: absolute;
    inset: 0;
    display: grid;
    place-items: center;
    color: var(--text-muted);
    font-size: 13px;
  }

  .wiki-frame {
    position: relative;
    width: 100%;
    height: 100%;
    border: 0;
    background: #050608;
  }
</style>
