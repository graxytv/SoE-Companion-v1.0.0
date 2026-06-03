<script lang="ts">
  import { lootHistoryStore } from '../stores';

  interface Props {
    onClose?: () => void;
  }

  let { onClose }: Props = $props();

  let entries = $derived(lootHistoryStore.entries);

  const qualityColors: Record<string, string> = {
    Unique: 'var(--quality-unique)',
    Set: 'var(--quality-set)',
    Rare: 'var(--quality-rare)',
    Magic: 'var(--quality-magic)',
    Crafted: 'var(--quality-crafted)',
    Normal: 'var(--text-muted)',
    Superior: 'var(--text-secondary)',
    Inferior: 'var(--text-muted)',
  };

  function color(quality: string): string {
    return qualityColors[quality] ?? 'var(--text-muted)';
  }
</script>

<div class="loot-history-panel">
  <div class="panel-header">
    <span class="panel-title">Loot History</span>
    {#if onClose}
      <button class="close-btn" onclick={onClose}>✕</button>
    {/if}
  </div>
  {#if entries.length === 0}
    <div class="empty">No drops recorded this session.</div>
  {:else}
    <div class="entry-list">
      {#each [...entries].reverse() as entry (entry.unit_id)}
        <div class="entry">
          <span class="item-name" style:color={color(entry.quality)}>{entry.name}</span>
          <span class="item-quality">{entry.quality}</span>
        </div>
      {/each}
    </div>
  {/if}
</div>

<style>
  .loot-history-panel {
    display: flex;
    flex-direction: column;
    font-family: var(--font-mono);
    font-size: 12px;
    background: rgba(0,0,0,0.85);
    border-radius: 6px;
    overflow: hidden;
    min-width: 240px;
    max-height: 400px;
  }

  .panel-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 6px 10px;
    border-bottom: 1px solid rgba(255,255,255,0.1);
  }

  .panel-title {
    color: var(--text-secondary);
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.05em;
  }

  .close-btn {
    background: transparent;
    border: none;
    color: var(--text-muted);
    cursor: pointer;
    font-size: 12px;
    padding: 0 2px;
    line-height: 1;
  }

  .close-btn:hover { color: var(--text-primary); }

  .entry-list {
    overflow-y: auto;
    max-height: 360px;
  }

  .empty {
    color: var(--text-muted);
    font-style: italic;
    padding: 8px 10px;
  }

  .entry {
    display: flex;
    justify-content: space-between;
    gap: 8px;
    padding: 3px 10px;
    border-bottom: 1px solid rgba(255,255,255,0.04);
  }

  .item-name {
    font-weight: 600;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .item-quality {
    color: var(--text-muted);
    font-size: 11px;
    flex-shrink: 0;
  }
</style>
