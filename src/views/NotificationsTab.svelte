<script lang="ts">
  import { settingsStore } from '../stores';
  import { Button, Toggle } from '../components';
  import { playSound } from '../lib/sound-player';
  import {
    ITEM_SOUND_CATEGORY_LABELS,
    buildItemSoundCatalog,
    itemSoundKey,
    type ItemSoundCatalogItem,
    type ItemSoundCategory,
    type ItemSoundRule,
  } from '../lib/item-sounds';

  type SubTab = 'general' | 'item-sounds';
  type ItemSoundFilter = ItemSoundCategory | 'all';

  const catalog = buildItemSoundCatalog();
  const categoryOptions: Array<{ key: ItemSoundFilter; label: string }> = [
    { key: 'all', label: 'All' },
    { key: 'unique', label: 'Uniques' },
    { key: 'set', label: 'Sets' },
    { key: 'rune', label: 'Runes' },
    { key: 'material', label: 'Materials' },
    { key: 'custom', label: 'Custom' },
    { key: 'runeword', label: 'Runewords' },
    { key: 'hellforged', label: 'Hellforged' },
  ];

  let activeTab = $state<SubTab>('general');
  let overlayEnabled = $derived(settingsStore.settings.notificationOverlayEnabled);
  let duration = $derived(settingsStore.settings.notificationDuration);
  let fontSize = $derived(settingsStore.settings.notificationFontSize);
  let opacity = $derived(settingsStore.settings.notificationOpacity);
  let itemSoundRules = $derived(settingsStore.settings.itemSoundRules);
  let soundSlots = $derived(settingsStore.settings.sounds);
  let masterVolume = $derived(settingsStore.settings.soundVolume);

  let itemFilter = $state<ItemSoundFilter>('all');
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

  function setDuration(value: number) {
    const clamped = Math.max(1000, Math.min(30000, value));
    settingsStore.set('notificationDuration', clamped);
  }

  function setFontSize(value: number) {
    const clamped = Math.max(10, Math.min(24, value));
    settingsStore.set('notificationFontSize', clamped);
  }

  function setOpacity(value: number) {
    const clamped = Math.max(0, Math.min(1, value));
    settingsStore.set('notificationOpacity', clamped);
  }

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
    activeTab = 'item-sounds';
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

<section class="tab-content">
  <div class="sub-tabs">
    <button class:active={activeTab === 'general'} onclick={() => activeTab = 'general'}>General</button>
    <button class:active={activeTab === 'item-sounds'} onclick={() => activeTab = 'item-sounds'}>Item Sounds</button>
  </div>

  {#if activeTab === 'general'}
    <div class="settings-section">
      <h2 class="section-title">Notification Settings</h2>
      <p class="section-description">
        Customize how item drop notifications appear in the overlay.
      </p>

      <div class="settings-grid">
        <div class="setting-row">
          <div class="setting-info">
            <label class="setting-label" for="notification-overlay">Notification Overlay</label>
            <span class="setting-hint">Show or hide visual item popups. Sounds, loot history, and trackers keep working.</span>
          </div>
          <div class="setting-control">
            <Toggle
              id="notification-overlay"
              checked={overlayEnabled}
              onchange={(enabled) => settingsStore.setNotificationOverlayEnabled(enabled)}
            />
          </div>
        </div>

        <div class="setting-row">
          <div class="setting-info">
            <label class="setting-label" for="duration-slider">Display Duration</label>
            <span class="setting-hint">How long notifications stay visible (1-30 seconds)</span>
          </div>
          <div class="setting-control">
            <input
              type="range"
              id="duration-slider"
              min="1000"
              max="30000"
              step="500"
              value={duration}
              oninput={(e) => setDuration(parseInt(e.currentTarget.value))}
              class="slider"
            />
            <span class="setting-value">{(duration / 1000).toFixed(1)}s</span>
          </div>
        </div>

        <div class="setting-row">
          <div class="setting-info">
            <label class="setting-label" for="font-size-slider">Size</label>
            <span class="setting-hint">Scales the whole notification (10-24 px)</span>
          </div>
          <div class="setting-control">
            <input
              type="range"
              id="font-size-slider"
              min="10"
              max="24"
              step="1"
              value={fontSize}
              oninput={(e) => setFontSize(parseInt(e.currentTarget.value))}
              class="slider"
            />
            <span class="setting-value">{fontSize}px</span>
          </div>
        </div>

        <div class="setting-row">
          <div class="setting-info">
            <label class="setting-label" for="opacity-slider">Background Opacity</label>
            <span class="setting-hint">Transparency of notification background (0-100%)</span>
          </div>
          <div class="setting-control">
            <input
              type="range"
              id="opacity-slider"
              min="0"
              max="1"
              step="0.05"
              value={opacity}
              oninput={(e) => setOpacity(parseFloat(e.currentTarget.value))}
              class="slider"
            />
            <span class="setting-value">{Math.round(opacity * 100)}%</span>
          </div>
        </div>
      </div>
    </div>
  {:else}
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
                <span>{ITEM_SOUND_CATEGORY_LABELS[entry.rule.category]} • Sound {entry.rule.soundSlot ?? '-'} • {Math.round(entry.rule.volume * 100)}%</span>
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
  {/if}
</section>

<style>
  .tab-content {
    padding: var(--space-4);
    display: flex;
    flex-direction: column;
    gap: var(--space-4);
    overflow: auto;
  }

  .sub-tabs,
  .category-tabs {
    display: flex;
    flex-wrap: wrap;
    gap: var(--space-2);
  }

  .sub-tabs button,
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

  .sub-tabs button.active,
  .category-tabs button.active {
    border-color: var(--accent-primary);
    color: var(--accent-primary);
    background: var(--bg-tertiary);
  }

  .category-tabs button.active small {
    color: color-mix(in srgb, var(--accent-primary) 70%, var(--text-muted));
  }

  .settings-section {
    display: flex;
    flex-direction: column;
    gap: var(--space-4);
    max-width: 980px;
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

  .settings-grid {
    display: flex;
    flex-direction: column;
    gap: var(--space-4);
  }

  .setting-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-4);
    padding: var(--space-3);
    background: var(--bg-secondary);
    border-radius: var(--radius-md);
  }

  .setting-info {
    display: flex;
    flex-direction: column;
    gap: var(--space-1);
  }

  .setting-label {
    font-size: var(--text-sm);
    font-weight: 500;
    color: var(--text-primary);
  }

  .setting-hint {
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .setting-control {
    display: flex;
    align-items: center;
    gap: var(--space-3);
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

  .setting-value {
    font-family: var(--font-mono);
    font-size: var(--text-sm);
    color: var(--text-primary);
    min-width: 50px;
    text-align: right;
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

  .item-sounds-section {
    max-width: 1120px;
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
