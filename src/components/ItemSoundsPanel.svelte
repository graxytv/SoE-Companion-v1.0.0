<script lang="ts">
  import Button from './Button.svelte';
  import { settingsStore } from '../stores';
  import { playSound } from '../lib/sound-player';
  import {
    ITEM_SOUND_CATEGORY_LABELS,
    buildItemSoundCatalog,
    itemSoundKey,
    type ItemSoundCategory,
    type ItemSoundRule,
  } from '../lib/item-sounds';

  type LocalItemSoundFilter = ItemSoundCategory | 'all';

  const catalog = buildItemSoundCatalog();
  const categoryOptions: Array<{ key: LocalItemSoundFilter; label: string }> = [
    { key: 'all', label: 'All' },
    { key: 'unique', label: 'Uniques' },
    { key: 'set', label: 'Sets' },
    { key: 'rune', label: 'Runes' },
    { key: 'fateCard', label: 'Fate Cards' },
    { key: 'essence', label: 'Essences' },
    { key: 'material', label: 'Materials' },
    { key: 'custom', label: 'Custom' },
    { key: 'runeword', label: 'Runewords' },
    { key: 'hellforged', label: 'Hellforged' },
  ];

  let itemSoundRules = $derived(settingsStore.settings.itemSoundRules);
  let soundSlots = $derived(settingsStore.settings.sounds);
  let masterVolume = $derived(settingsStore.settings.soundVolume);

  let itemFilter = $state<LocalItemSoundFilter>('all');
  let itemSearch = $state('');
  let selectedItemKey = $state(catalog[0]?.key ?? '');
  let customItemName = $state('');
  let selectedSoundSlot = $state(1);
  let selectedVolume = $state(1);
  let status = $state('');

  const filteredCatalog = $derived(
    catalog
      .filter((item) => itemFilter === 'all' || item.category === itemFilter)
      .filter((item) => !itemSearch.trim() || item.searchText.includes(itemSearch.trim().toLowerCase())),
  );

  const selectedCatalogItem = $derived(
    catalog.find((item) => item.key === selectedItemKey) ?? filteredCatalog[0] ?? catalog[0],
  );

  $effect(() => {
    if (itemFilter === 'custom') return;
    if (filteredCatalog.length === 0) {
      selectedItemKey = '';
      return;
    }
    if (!filteredCatalog.some((item) => item.key === selectedItemKey)) {
      selectedItemKey = filteredCatalog[0].key;
    }
  });

  const configuredRules = $derived(
    Object.entries(itemSoundRules)
      .map(([key, rule]) => ({ key, rule }))
      .sort((a, b) =>
        a.rule.category === b.rule.category
          ? a.rule.itemName.localeCompare(b.rule.itemName)
          : ITEM_SOUND_CATEGORY_LABELS[a.rule.category].localeCompare(ITEM_SOUND_CATEGORY_LABELS[b.rule.category]),
      ),
  );

  function setSelectedItem(key: string) {
    selectedItemKey = key;
    const existing = itemSoundRules[key];
    if (existing) {
      selectedSoundSlot = existing.soundSlot ?? selectedSoundSlot;
      selectedVolume = existing.volume;
    }
    status = '';
  }

  function currentDraft(): { key: string; rule: ItemSoundRule } | null {
    if (itemFilter === 'custom') {
      const name = customItemName.trim();
      if (!name) return null;
      return {
        key: itemSoundKey('custom', name),
        rule: { itemName: name, category: 'custom', soundSlot: selectedSoundSlot, volume: selectedVolume },
      };
    }
    const item = selectedCatalogItem;
    if (!item) return null;
    return {
      key: item.key,
      rule: {
        itemName: item.itemName,
        category: item.category,
        soundSlot: selectedSoundSlot,
        volume: selectedVolume,
      },
    };
  }

  function saveRule() {
    const draft = currentDraft();
    if (!draft) {
      status = 'Pick an item or enter a custom item name first.';
      return;
    }
    settingsStore.setItemSoundRule(draft.key, draft.rule);
    status = `Saved ${draft.rule.itemName}.`;
  }

  function editRule(key: string, rule: ItemSoundRule) {
    itemFilter = rule.category;
    selectedItemKey = key;
    customItemName = rule.category === 'custom' ? rule.itemName : '';
    selectedSoundSlot = rule.soundSlot ?? 1;
    selectedVolume = rule.volume;
    status = '';
  }

  function removeRule(key: string) {
    settingsStore.removeItemSoundRule(key);
    status = 'Removed item sound.';
  }

  function testRule() {
    void playSound(selectedSoundSlot, masterVolume * selectedVolume);
  }
</script>

<div class="settings-section item-sounds-section">
  <div>
    <h2 class="section-title">Item Sounds</h2>
    <p class="section-description">
      Assign a sound to specific items. These sounds play whenever that item is tracked, without adding extra popups.
    </p>
  </div>

  <div class="item-sound-editor">
    <div class="category-tabs">
      {#each categoryOptions as option}
        <button class:active={itemFilter === option.key} onclick={() => itemFilter = option.key}>
          <span>{option.label}</span>
          {#if option.key === 'hellforged' || option.key === 'runeword'}
            <small>crafted only</small>
          {/if}
        </button>
      {/each}
    </div>

    <div class="editor-grid">
      {#if itemFilter === 'custom'}
        <label>
          <span>Custom Item Name</span>
          <input
            value={customItemName}
            oninput={(e) => customItemName = e.currentTarget.value}
            placeholder="Type exact item name..."
          />
        </label>
      {:else}
        <label>
          <span>Search</span>
          <input
            value={itemSearch}
            oninput={(e) => itemSearch = e.currentTarget.value}
            placeholder="Search items..."
          />
        </label>

        <label>
          <span>Item</span>
          <select bind:value={selectedItemKey} onchange={(e) => setSelectedItem(e.currentTarget.value)}>
            {#each filteredCatalog as item}
              <option value={item.key}>{item.label}</option>
            {/each}
          </select>
        </label>
      {/if}

      <label>
        <span>Sound</span>
        <select value={String(selectedSoundSlot)} onchange={(e) => selectedSoundSlot = Number(e.currentTarget.value) || 1}>
          {#each soundSlots as slot, i}
            <option value={i + 1}>{i + 1}. {slot.label}</option>
          {/each}
        </select>
      </label>

      <label class="volume-row">
        <span>Volume</span>
        <input class="slider" type="range" min="0" max="1" step="0.05" value={selectedVolume} oninput={(e) => selectedVolume = Number(e.currentTarget.value) || 0} />
        <strong>{Math.round(selectedVolume * 100)}%</strong>
      </label>
    </div>

    <div class="editor-actions">
      <Button variant="secondary" size="sm" onclick={testRule}>Test</Button>
      <Button variant="primary" size="sm" onclick={saveRule}>Save Item Sound</Button>
      {#if status}<span class="status">{status}</span>{/if}
    </div>
  </div>

  <div class="configured-list">
    <h3>Configured Item Sounds</h3>
    {#if configuredRules.length === 0}
      <p class="empty">No item sounds configured yet.</p>
    {:else}
      {#each configuredRules as entry (entry.key)}
        <div class="configured-row">
          <div>
            <strong>{entry.rule.itemName}</strong>
            <span>{ITEM_SOUND_CATEGORY_LABELS[entry.rule.category]} - Sound {entry.rule.soundSlot ?? '-'} - {Math.round(entry.rule.volume * 100)}%</span>
          </div>
          <div class="configured-actions">
            <Button variant="secondary" size="sm" onclick={() => editRule(entry.key, entry.rule)}>Edit</Button>
            <Button variant="danger" size="sm" onclick={() => removeRule(entry.key)}>Remove</Button>
          </div>
        </div>
      {/each}
    {/if}
  </div>
</div>

<style>
  .settings-section {
    display: flex;
    flex-direction: column;
    gap: var(--space-4);
    max-width: 980px;
  }

  .item-sounds-section {
    max-width: 1120px;
  }

  .section-title {
    font-size: var(--text-lg);
    font-weight: 600;
    color: var(--text-primary);
    margin: 0;
  }

  .section-description {
    font-size: var(--text-sm);
    color: var(--text-muted);
    margin: var(--space-1) 0 0;
  }

  .item-sound-editor,
  .configured-list {
    display: grid;
    gap: 12px;
    padding: 14px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
  }

  .configured-list h3 {
    margin: 0;
    color: var(--text-primary);
    font-size: 14px;
  }

  .category-tabs {
    display: flex;
    flex-wrap: wrap;
    gap: var(--space-2);
  }

  .category-tabs button {
    display: grid;
    place-items: center;
    gap: 2px;
    padding: 8px 12px;
    border: 1px solid var(--border-primary);
    border-radius: 4px;
    background: var(--bg-secondary);
    color: var(--text-secondary);
    cursor: pointer;
    font: inherit;
  }

  .category-tabs button small {
    color: var(--text-muted);
    font-size: 10px;
    line-height: 1;
  }

  .category-tabs button.active {
    border-color: var(--accent-primary);
    color: var(--accent-primary);
    background: var(--bg-tertiary);
  }

  .category-tabs button.active small {
    color: color-mix(in srgb, var(--accent-primary) 70%, var(--text-muted));
  }

  .editor-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
    gap: var(--space-3);
    align-items: end;
  }

  .editor-grid label {
    display: grid;
    gap: var(--space-1);
    color: var(--text-secondary);
    font-size: var(--text-sm);
  }

  .editor-grid input,
  .editor-grid select {
    width: 100%;
    padding: 8px 10px;
    border: 1px solid var(--border-primary);
    border-radius: 4px;
    background: var(--bg-tertiary);
    color: var(--text-primary);
    font: inherit;
  }

  .slider {
    width: 160px;
    height: 6px;
    appearance: none;
    background: var(--bg-tertiary);
    border-radius: var(--radius-full);
    cursor: pointer;
  }

  .slider::-webkit-slider-thumb {
    appearance: none;
    width: 16px;
    height: 16px;
    background: var(--accent-primary);
    border-radius: var(--radius-full);
    cursor: pointer;
    transition: transform 0.1s ease;
  }

  .slider::-webkit-slider-thumb:hover {
    transform: scale(1.1);
  }

  .slider::-moz-range-thumb {
    width: 16px;
    height: 16px;
    background: var(--accent-primary);
    border: none;
    border-radius: var(--radius-full);
    cursor: pointer;
  }

  .volume-row {
    grid-template-columns: auto 1fr 52px;
    align-items: center;
  }

  .volume-row span {
    grid-column: 1 / -1;
  }

  .volume-row .slider {
    width: 100%;
  }

  .volume-row strong {
    color: var(--text-primary);
    font-family: var(--font-mono);
    font-size: var(--text-xs);
    text-align: right;
  }

  .editor-actions,
  .configured-actions {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: var(--space-2);
  }

  .status,
  .empty,
  .configured-row span {
    color: var(--text-muted);
    font-size: var(--text-sm);
  }

  .configured-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-3);
    padding: var(--space-3) 0;
    border-top: 1px solid var(--border-primary);
  }

  .configured-row > div:first-child {
    display: grid;
    gap: 3px;
  }

  .configured-row strong {
    color: var(--accent-primary);
  }
</style>
