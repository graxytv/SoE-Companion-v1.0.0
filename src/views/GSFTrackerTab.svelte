<script lang="ts">
  import { onMount } from 'svelte';
  import { Button, Toggle } from '../components';
  import { itemsDictionaryStore, settingsStore, type GsfPlayer, type GsfWantedItem } from '../stores';
  import {
    GSF_CATEGORIES,
    GSF_SLOTS,
    buildGsfCatalog,
    gsfCategoryLabel,
    gsfSlotLabel,
    normalizeGsfItemName,
    resolveGsfItemImage,
    type GsfCatalogOption,
    type GsfItemCategory,
    type GsfItemSlot,
  } from '../lib/gsf-item-catalog';
  import { inferGsfSlot } from '../lib/gsf-tracker';
  import { playSound } from '../lib/sound-player';

  const statusOptions = [
    { key: 'needed', label: 'Needed' },
    { key: 'found', label: 'Found' },
    { key: 'skipped', label: 'Skipped' },
  ] as const;
  const classOptions = [
    { key: '', label: 'Select Class' },
    { key: 'Amazon', label: 'Amazon' },
    { key: 'Assassin', label: 'Assassin' },
    { key: 'Barbarian', label: 'Barbarian' },
    { key: 'Druid', label: 'Druid' },
    { key: 'Necromancer', label: 'Necromancer' },
    { key: 'Paladin', label: 'Paladin' },
    { key: 'Sorceress', label: 'Sorceress' },
  ] as const;
  const ALL_PLAYERS_ID = '__all_players__';

  let { activeSubTab = $bindable('overview') }: { activeSubTab?: string } = $props();

  type AggregatedWantedItem = {
    key: string;
    item: GsfWantedItem;
    count: number;
    playerNames: string[];
  };

  let selectedPlayerId = $state(ALL_PLAYERS_ID);
  let itemSearch = $state('');
  let globalSearch = $state('');
  let globalSearchSubmitted = $state('');
  let categoryFilter = $state<'all' | GsfItemCategory>('all');
  let slotFilter = $state<'all' | GsfItemSlot>('all');
  let statusFilter = $state<'all' | GsfWantedItem['status']>('all');
  let newItemName = $state('');
  let newItemCategory = $state<GsfItemCategory>('su');
  let newItemSlot = $state<GsfItemSlot>('any');
  let newItemStatus = $state<GsfWantedItem['status']>('needed');
  let transferMessage = $state('');

  let players = $derived(settingsStore.settings.gsfPlayers);
  let isAllPlayers = $derived(selectedPlayerId === ALL_PLAYERS_ID);
  let selectedPlayer = $derived(
    isAllPlayers ? null : players.find((player) => player.id === selectedPlayerId) ?? null,
  );
  let soundChoices = $derived(
    settingsStore.settings.sounds
      .map((slot, i) => ({ index: i + 1, slot }))
      .filter(({ slot }) => slot.source.kind !== 'empty'),
  );
  let gsfCatalog = $derived(buildGsfCatalog(itemsDictionaryStore.dict));
  let allCatalogOptions = $derived(
    GSF_CATEGORIES.flatMap((category) => gsfCatalog[category.key]),
  );
  let newItemSuggestions = $derived(catalogSuggestions(newItemName, 80));

  let selectedItems = $derived(
    (selectedPlayer?.wantedItems ?? []).filter((item) => {
      if (itemSearch.trim() && !item.itemName.toLowerCase().includes(itemSearch.trim().toLowerCase())) return false;
      if (categoryFilter !== 'all' && item.category !== categoryFilter) return false;
      if (slotFilter !== 'all' && item.slot !== slotFilter) return false;
      if (statusFilter !== 'all' && item.status !== statusFilter) return false;
      return true;
    }),
  );
  let allPlayerItems = $derived(buildAllPlayerItems());
  let visibleAllPlayerItems = $derived(
    allPlayerItems.filter((entry) => {
      if (itemSearch.trim() && !entry.item.itemName.toLowerCase().includes(itemSearch.trim().toLowerCase())) return false;
      if (categoryFilter !== 'all' && entry.item.category !== categoryFilter) return false;
      if (slotFilter !== 'all' && entry.item.slot !== slotFilter) return false;
      return true;
    }),
  );

  let totalNeeded = $derived(
    players.reduce(
      (sum, player) => sum + player.wantedItems.filter((item) => item.status === 'needed').length,
      0,
    ),
  );
  let globalSearchMatches = $derived(findGlobalMatches(globalSearchSubmitted));

  $effect(() => {
    if (!selectedPlayerId) {
      selectedPlayerId = ALL_PLAYERS_ID;
    } else if (selectedPlayerId !== ALL_PLAYERS_ID && !players.some((player) => player.id === selectedPlayerId)) {
      selectedPlayerId = ALL_PLAYERS_ID;
    }
  });

  onMount(() => {
    void itemsDictionaryStore.init();
  });

  function addPlayer(): void {
    selectedPlayerId = settingsStore.addGsfPlayer(`Player ${players.length + 1}`);
  }

  function addWantedItem(slot: GsfItemSlot = newItemSlot): void {
    if (!selectedPlayer) return;
    const name = newItemName.trim();
    settingsStore.addGsfWantedItem(selectedPlayer.id, {
      itemName: name,
      category: newItemCategory,
      slot,
      status: newItemStatus,
    });
    newItemName = '';
    newItemStatus = 'needed';
  }

  function testGsfSound(): void {
    const slot = settingsStore.settings.gsfSoundSlot;
    if (slot != null) {
      void playSound(slot, settingsStore.settings.soundVolume * settingsStore.settings.gsfSoundVolume);
    }
  }

  function handleGsfSoundVolumeChange(event: Event): void {
    const value = Number((event.currentTarget as HTMLInputElement).value);
    settingsStore.setGsfSoundVolume(value / 100);
  }

  function removeSelectedPlayer(): void {
    if (!selectedPlayer) return;
    settingsStore.removeGsfPlayer(selectedPlayer.id);
  }

  function suggestionsFor(_category: GsfItemCategory, query: string) {
    return catalogSuggestions(query, 60);
  }

  function catalogSuggestions(query: string, limit = 60): GsfCatalogOption[] {
    const needle = normalizeGsfItemName(query);
    const pool = allCatalogOptions;
    if (!needle) return pool.slice(0, limit);
    return pool
      .filter((option) => normalizeGsfItemName(option.name).includes(needle))
      .slice(0, limit);
  }

  function exactCatalogOption(name: string): GsfCatalogOption | null {
    const normalized = normalizeGsfItemName(name);
    if (!normalized) return null;
    return allCatalogOptions.find((option) => normalizeGsfItemName(option.name) === normalized) ?? null;
  }

  function inferredSlotForOption(option: GsfCatalogOption): GsfItemSlot {
    if (option.category === 'uniqueJewel') return 'jewel';
    const inferred = inferGsfSlot({ name: option.name });
    return inferred === 'other' ? 'any' : inferred;
  }

  function applySelectionToNewItem(): void {
    const option = exactCatalogOption(newItemName);
    if (!option) return;
    newItemName = option.name;
    newItemCategory = option.category;
    newItemSlot = inferredSlotForOption(option);
  }

  function applySelectionToExisting(item: GsfWantedItem, value: string): void {
    if (!selectedPlayer) return;
    const option = exactCatalogOption(value);
    if (!option) {
      settingsStore.updateGsfWantedItem(selectedPlayer.id, item.id, { itemName: value });
      return;
    }
    settingsStore.updateGsfWantedItem(selectedPlayer.id, item.id, {
      itemName: option.name,
      category: option.category,
      slot: inferredSlotForOption(option),
    });
  }

  function imageFor(item: GsfWantedItem): string | null {
    return resolveGsfItemImage(item.category, item.itemName);
  }

  function setFound(playerId: string, item: GsfWantedItem): void {
    settingsStore.updateGsfWantedItem(playerId, item.id, {
      status: item.status === 'found' ? 'needed' : 'found',
    });
  }

  function findGlobalMatches(query: string): Array<{ playerName: string; item: GsfWantedItem }> {
    const needle = normalizeGsfItemName(query);
    if (!needle) return [];
    const out: Array<{ playerName: string; item: GsfWantedItem }> = [];
    for (const player of players) {
      for (const item of player.wantedItems) {
        if (item.status !== 'needed') continue;
        const name = item.normalizedItemName || normalizeGsfItemName(item.itemName);
        if (name.includes(needle)) {
          out.push({ playerName: player.name || 'Unnamed Player', item });
        }
      }
    }
    return out;
  }

  function buildAllPlayerItems(): AggregatedWantedItem[] {
    const grouped = new Map<string, AggregatedWantedItem>();
    for (const player of players) {
      for (const item of player.wantedItems) {
        if (item.status !== 'needed') continue;
        const normalized = item.normalizedItemName || normalizeGsfItemName(item.itemName);
        const key = `${item.category}:${item.slot}:${normalized}`;
        const playerName = player.name || 'Unnamed Player';
        const existing = grouped.get(key);
        if (existing) {
          existing.count += 1;
          if (!existing.playerNames.includes(playerName)) existing.playerNames.push(playerName);
        } else {
          grouped.set(key, {
            key,
            item,
            count: 1,
            playerNames: [playerName],
          });
        }
      }
    }
    return [...grouped.values()].sort((a, b) => a.item.itemName.localeCompare(b.item.itemName));
  }

  function cloneForExport(player: GsfPlayer): GsfPlayer {
    return {
      ...player,
      wantedItems: player.wantedItems.map((item) => ({ ...item })),
    };
  }

  function importOnePlayer(incoming: Partial<GsfPlayer>, fallbackName: string): string {
    if (!incoming || typeof incoming !== 'object' || !Array.isArray(incoming.wantedItems)) {
      throw new Error('Missing player data');
    }
    const newId = settingsStore.addGsfPlayer(incoming.name || fallbackName);
    settingsStore.updateGsfPlayer(newId, {
      className: incoming.className ?? '',
      buildName: incoming.buildName ?? '',
      notes: incoming.notes ?? '',
    });
    for (const item of incoming.wantedItems) {
      settingsStore.addGsfWantedItem(newId, {
        itemName: item.itemName,
        category: item.category,
        slot: item.slot,
        status: item.status,
        notes: item.notes,
      });
    }
    return newId;
  }

  async function copyTextToClipboard(text: string): Promise<void> {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(text);
      return;
    }

    const textarea = document.createElement('textarea');
    textarea.value = text;
    textarea.style.position = 'fixed';
    textarea.style.opacity = '0';
    document.body.appendChild(textarea);
    textarea.select();
    document.execCommand('copy');
    textarea.remove();
  }

  async function readTextFromClipboard(): Promise<string> {
    if (!navigator.clipboard?.readText) {
      throw new Error('Clipboard read is unavailable');
    }
    return await navigator.clipboard.readText();
  }

  async function exportSelectedPlayer(): Promise<void> {
    if (!selectedPlayer) return;
    await copyTextToClipboard(JSON.stringify({ kind: 'd2mxlutils-gsf-player', version: 1, player: cloneForExport(selectedPlayer) }));
    transferMessage = 'Player exported to clipboard.';
  }

  async function exportPlayerLibrary(): Promise<void> {
    await copyTextToClipboard(JSON.stringify({
      kind: 'd2mxlutils-gsf-library',
      version: 1,
      players: players.map(cloneForExport),
    }));
    transferMessage = 'Player library exported to clipboard.';
  }

  async function importPlayerCard(): Promise<void> {
    try {
      const transferText = await readTextFromClipboard();
      const parsed = JSON.parse(transferText) as unknown;
      const maybeWrapped = parsed as { player?: unknown };
      const incoming = (maybeWrapped?.player ?? parsed) as Partial<GsfPlayer>;
      const newId = importOnePlayer(incoming, `Player ${players.length + 1}`);
      selectedPlayerId = newId;
      transferMessage = 'Player imported from clipboard.';
    } catch (error) {
      transferMessage = 'Clipboard does not contain a valid player card.';
    }
  }

  async function importPlayerLibrary(): Promise<void> {
    try {
      const transferText = await readTextFromClipboard();
      const parsed = JSON.parse(transferText) as unknown;
      const incomingPlayers = (parsed as { players?: unknown }).players;
      if (!Array.isArray(incomingPlayers) || incomingPlayers.length === 0) {
        throw new Error('Missing player library');
      }
      let firstImportedId = '';
      incomingPlayers.forEach((incoming, index) => {
        const newId = importOnePlayer(incoming as Partial<GsfPlayer>, `Imported Player ${players.length + index + 1}`);
        if (!firstImportedId) firstImportedId = newId;
      });
      if (firstImportedId) selectedPlayerId = firstImportedId;
      transferMessage = `Imported ${incomingPlayers.length} players from clipboard.`;
    } catch (error) {
      transferMessage = 'Clipboard does not contain a valid player library.';
    }
  }
</script>

<section class="tab-content gsf-tab">
  <div class="sub-tabs-list" role="tablist" aria-label="GSF Tracker sections">
    <button type="button" class="sub-tab" class:active={activeSubTab === 'overview'} onclick={() => { activeSubTab = 'overview'; }}>Overview</button>
    <button type="button" class="sub-tab" class:active={activeSubTab === 'settings'} onclick={() => { activeSubTab = 'settings'; }}>GSF Tracking Settings</button>
  </div>

  {#if activeSubTab === 'overview'}
  <div class="settings-section gsf-hero">
    <div>
      <h2 class="section-title">GSF Tracker</h2>
      <p class="section-description">
        Track items your group needs. The selected player only changes this editor; drops are matched against every player.
      </p>
    </div>
    <div class="gsf-summary">
      <span>{players.length} Players</span>
      <strong>{totalNeeded} Needed</strong>
    </div>
  </div>

  <div class="settings-section">
    <div class="settings-cluster">
      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Player Card Sharing</span>
          <span class="setting-hint">Export one player or the full player library to clipboard, then import it from clipboard.</span>
        </div>
        <div class="gsf-setting-actions">
          <Button variant="secondary" size="sm" disabled={!selectedPlayer} onclick={exportSelectedPlayer}>Export Player</Button>
          <Button variant="secondary" size="sm" onclick={importPlayerCard}>Import Player</Button>
          <Button variant="secondary" size="sm" disabled={players.length === 0} onclick={exportPlayerLibrary}>Export All Players</Button>
          <Button variant="secondary" size="sm" onclick={importPlayerLibrary}>Import All Players</Button>
          {#if transferMessage}<span>{transferMessage}</span>{/if}
        </div>
      </div>
    </div>
  </div>

  <div class="settings-section gsf-editor-section">
    <div class="gsf-toolbar">
      <div class="gsf-player-picker">
        <label class="label" for="gsf-player-select">Player</label>
        <div class="gsf-player-row">
          <select id="gsf-player-select" class="filter-select gsf-player-select" bind:value={selectedPlayerId}>
            <option value={ALL_PLAYERS_ID}>All Players</option>
            {#each players as player (player.id)}
              <option value={player.id}>{player.name || 'Unnamed Player'}</option>
            {/each}
          </select>
        </div>
      </div>
      <div class="gsf-toolbar-actions">
        <Button variant="secondary" size="sm" onclick={addPlayer}>Add Player</Button>
        <Button variant="danger" size="sm" disabled={!selectedPlayer} onclick={removeSelectedPlayer}>Remove Player</Button>
      </div>
    </div>

    <div class="gsf-global-search">
      <div class="gsf-global-search-row">
        <input
          class="input"
          placeholder="Search all players for an item"
          bind:value={globalSearch}
          list="gsf-global-options"
          onkeydown={(event) => { if (event.key === 'Enter') globalSearchSubmitted = globalSearch; }}
        />
        <datalist id="gsf-global-options">
          {#each catalogSuggestions(globalSearch, 60) as option (`global-${option.category}-${option.name}`)}
            <option value={option.name}>{gsfCategoryLabel(option.category)}</option>
          {/each}
        </datalist>
        <Button variant="secondary" size="sm" onclick={() => { globalSearchSubmitted = globalSearch; }}>Search</Button>
      </div>
      {#if globalSearchSubmitted}
        <div class="gsf-global-results">
          {#if globalSearchMatches.length > 0}
            {#each globalSearchMatches as match (`${match.playerName}-${match.item.id}`)}
              <span><strong>{match.playerName}</strong> needs {match.item.itemName} ({gsfCategoryLabel(match.item.category)} / {gsfSlotLabel(match.item.slot)})</span>
            {/each}
          {:else}
            <span>No needed items matched that search.</span>
          {/if}
        </div>
      {/if}
    </div>

    {#if isAllPlayers}
      <div class="gsf-items-panel">
        <div class="gsf-filters">
          <input class="input" placeholder="Filter tracked item name" bind:value={itemSearch} />
          <select class="filter-select" bind:value={categoryFilter}>
            <option value="all">All Categories</option>
            {#each GSF_CATEGORIES as category (category.key)}
              <option value={category.key}>{category.label}</option>
            {/each}
          </select>
          <select class="filter-select" bind:value={slotFilter}>
            <option value="all">All Slots</option>
            {#each GSF_SLOTS as slot (slot.key)}
              <option value={slot.key}>{slot.label}</option>
            {/each}
          </select>
        </div>

        <div class="gsf-item-list">
          {#each visibleAllPlayerItems as entry (entry.key)}
            {@const image = imageFor(entry.item)}
            <div class="gsf-item-row gsf-all-item-row">
              <div class="gsf-item-image">
                {#if image}
                  <img src={image} alt={entry.item.itemName || 'Item'} onerror={(event) => { (event.currentTarget as HTMLImageElement).style.display = 'none'; }} />
                {/if}
                {#if entry.count > 1}
                  <span class="gsf-needed-count">x{entry.count}</span>
                {/if}
              </div>

              <div class="gsf-item-fields">
                <div class="gsf-all-item-title">
                  <strong>{entry.item.itemName}</strong>
                  <span>{gsfCategoryLabel(entry.item.category)} / {gsfSlotLabel(entry.item.slot)}</span>
                </div>
                <div class="gsf-player-list">
                  {entry.playerNames.join(', ')}
                </div>
              </div>
            </div>
          {:else}
            <div class="gsf-empty">
              No active needed items match the current filters.
            </div>
          {/each}
        </div>
      </div>
    {:else if selectedPlayer}
      <div class="gsf-player-fields">
        <label>
          <span class="label">Name</span>
          <input class="input" value={selectedPlayer.name} oninput={(event) => settingsStore.updateGsfPlayer(selectedPlayer.id, { name: (event.currentTarget as HTMLInputElement).value })} />
        </label>
        <label>
          <span class="label">Class</span>
          <select class="filter-select" value={selectedPlayer.className} onchange={(event) => settingsStore.updateGsfPlayer(selectedPlayer.id, { className: (event.currentTarget as HTMLSelectElement).value })}>
            {#each classOptions as option (option.key)}
              <option value={option.key}>{option.label}</option>
            {/each}
          </select>
        </label>
        <label>
          <span class="label">Build</span>
          <input class="input" placeholder="Example: Stormzon, Bow Druid, Totemancer" value={selectedPlayer.buildName} oninput={(event) => settingsStore.updateGsfPlayer(selectedPlayer.id, { buildName: (event.currentTarget as HTMLInputElement).value })} />
        </label>
        <label>
          <span class="label">Player Notes</span>
          <input class="input" value={selectedPlayer.notes} oninput={(event) => settingsStore.updateGsfPlayer(selectedPlayer.id, { notes: (event.currentTarget as HTMLInputElement).value })} />
        </label>
      </div>

      <div class="gsf-items-panel">
          <div class="gsf-add-row">
            <input
              class="input gsf-add-name"
              placeholder="Type an item name to add"
              bind:value={newItemName}
              list="gsf-add-options"
              oninput={applySelectionToNewItem}
              onchange={applySelectionToNewItem}
            />
            <datalist id="gsf-add-options">
              {#each newItemSuggestions as option (`${option.category}-${option.name}`)}
                <option value={option.name}>{gsfCategoryLabel(option.category)}</option>
              {/each}
            </datalist>
            <select class="filter-select" bind:value={newItemCategory}>
              {#each GSF_CATEGORIES as category (category.key)}
                <option value={category.key}>{category.label}</option>
              {/each}
            </select>
            <select class="filter-select" bind:value={newItemSlot}>
              {#each GSF_SLOTS as slot (slot.key)}
                <option value={slot.key}>{slot.label}</option>
              {/each}
            </select>
            <select class="filter-select" bind:value={newItemStatus}>
              {#each statusOptions as status (status.key)}
                <option value={status.key}>{status.label}</option>
              {/each}
            </select>
            <Button variant="primary" size="sm" disabled={!selectedPlayer || !newItemName.trim()} onclick={() => addWantedItem()}>Add Item</Button>
          </div>

          <div class="gsf-filters">
            <input class="input" placeholder="Filter tracked item name" bind:value={itemSearch} />
            <select class="filter-select" bind:value={categoryFilter}>
              <option value="all">All Categories</option>
              {#each GSF_CATEGORIES as category (category.key)}
                <option value={category.key}>{category.label}</option>
              {/each}
            </select>
            <select class="filter-select" bind:value={slotFilter}>
              <option value="all">All Slots</option>
              {#each GSF_SLOTS as slot (slot.key)}
                <option value={slot.key}>{slot.label}</option>
              {/each}
            </select>
            <select class="filter-select" bind:value={statusFilter}>
              <option value="all">All Statuses</option>
              {#each statusOptions as status (status.key)}
                <option value={status.key}>{status.label}</option>
              {/each}
            </select>
          </div>

          <div class="gsf-item-list">
            {#each selectedItems as item (item.id)}
              {@const image = imageFor(item)}
              <div class="gsf-item-row">
                <div class="gsf-item-image">
                  {#if image}
                    <img src={image} alt={item.itemName || 'Item'} onerror={(event) => { (event.currentTarget as HTMLImageElement).style.display = 'none'; }} />
                  {/if}
                </div>

                <div class="gsf-item-fields">
                  <div class="gsf-item-grid">
                    <label>
                      <span>Category</span>
                      <select class="filter-select" value={item.category} onchange={(event) => settingsStore.updateGsfWantedItem(selectedPlayer.id, item.id, { category: (event.currentTarget as HTMLSelectElement).value as GsfItemCategory })}>
                        {#each GSF_CATEGORIES as category (category.key)}
                          <option value={category.key}>{category.label}</option>
                        {/each}
                      </select>
                    </label>
                    <label>
                      <span>Slot</span>
                      <select class="filter-select" value={item.slot} onchange={(event) => settingsStore.updateGsfWantedItem(selectedPlayer.id, item.id, { slot: (event.currentTarget as HTMLSelectElement).value as GsfItemSlot })}>
                        {#each GSF_SLOTS as slot (slot.key)}
                          <option value={slot.key}>{slot.label}</option>
                        {/each}
                      </select>
                    </label>
                    <label>
                      <span>Status</span>
                      <select class="filter-select" value={item.status} onchange={(event) => settingsStore.updateGsfWantedItem(selectedPlayer.id, item.id, { status: (event.currentTarget as HTMLSelectElement).value as GsfWantedItem['status'] })}>
                        {#each statusOptions as status (status.key)}
                          <option value={status.key}>{status.label}</option>
                        {/each}
                      </select>
                    </label>
                  </div>

                  <label>
                    <span>Item Name</span>
                    <input
                      class="input"
                      list={`gsf-options-${item.id}`}
                      value={item.itemName}
                      placeholder="Search all item data"
                      oninput={(event) => applySelectionToExisting(item, (event.currentTarget as HTMLInputElement).value)}
                      onchange={(event) => applySelectionToExisting(item, (event.currentTarget as HTMLInputElement).value)}
                    />
                    <datalist id={`gsf-options-${item.id}`}>
                      {#each suggestionsFor(item.category, item.itemName) as option (`${option.category}-${option.name}`)}
                        <option value={option.name}>{gsfCategoryLabel(option.category)}</option>
                      {/each}
                    </datalist>
                  </label>

                  <label>
                    <span>Notes</span>
                    <input class="input" value={item.notes} oninput={(event) => settingsStore.updateGsfWantedItem(selectedPlayer.id, item.id, { notes: (event.currentTarget as HTMLInputElement).value })} />
                  </label>
                </div>

                <div class="gsf-item-actions">
                  <span class="gsf-status status-{item.status}">{item.status}</span>
                  <Button variant="secondary" size="sm" onclick={() => setFound(selectedPlayer.id, item)}>{item.status === 'found' ? 'Need' : 'Found'}</Button>
                  <Button variant="danger" size="sm" onclick={() => settingsStore.removeGsfWantedItem(selectedPlayer.id, item.id)}>Remove</Button>
                </div>
              </div>
            {:else}
              <div class="gsf-empty">
                No wanted items match the current filters.
              </div>
            {/each}
          </div>
      </div>
    {:else}
      <div class="gsf-empty">
        Add a player to start tracking group finds.
      </div>
    {/if}
  </div>
  {:else}
  <div class="settings-section">
    <div class="settings-cluster">
      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Enable GSF Tracking</span>
          <span class="setting-hint">Matches item drops against every player's needed items.</span>
        </div>
        <Toggle checked={settingsStore.settings.gsfEnabled} onchange={(enabled) => settingsStore.setGsfEnabled(enabled)} />
      </div>
      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Show GSF Loot Notifications</span>
          <span class="setting-hint">Adds Needed by text to matched drop notifications.</span>
        </div>
        <Toggle checked={settingsStore.settings.gsfNotificationEnabled} onchange={(enabled) => settingsStore.setGsfNotificationEnabled(enabled)} />
      </div>
      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">GSF Match Sound</span>
          <span class="setting-hint">Optional sound played when a wanted item drops.</span>
        </div>
        <div class="gsf-sound-controls">
          <select class="filter-select" value={settingsStore.settings.gsfSoundSlot ?? ''} onchange={(event) => settingsStore.setGsfSoundSlot((event.currentTarget as HTMLSelectElement).value === '' ? null : Number((event.currentTarget as HTMLSelectElement).value))}>
            <option value="">None</option>
            {#each soundChoices as { index, slot } (index)}
              <option value={index}>{slot.label}</option>
            {/each}
          </select>
          <Button
            variant="secondary"
            size="sm"
            disabled={settingsStore.settings.gsfSoundSlot == null}
            onclick={testGsfSound}
          >
            Test
          </Button>
        </div>
      </div>
      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">GSF Match Sound Volume</span>
          <span class="setting-hint">Controls only the GSF wanted-item match sound.</span>
        </div>
        <div class="gsf-volume-control">
          <input
            type="range"
            min="0"
            max="100"
            value={Math.round(settingsStore.settings.gsfSoundVolume * 100)}
            oninput={handleGsfSoundVolumeChange}
            aria-label="GSF match sound volume"
          />
          <span>{Math.round(settingsStore.settings.gsfSoundVolume * 100)}%</span>
        </div>
      </div>
    </div>
  </div>
  {/if}
</section>

<style>
  .gsf-hero {
    display: flex;
    justify-content: space-between;
    gap: var(--space-4);
    align-items: center;
  }

  .gsf-summary {
    min-width: 130px;
    border: 1px solid rgba(240, 205, 140, 0.45);
    border-radius: var(--radius-md);
    padding: var(--space-3);
    display: grid;
    gap: var(--space-1);
    text-align: right;
    color: var(--text-secondary);
  }

  .gsf-summary strong {
    color: var(--accent-primary);
    font-size: var(--text-xl);
  }

  .gsf-toolbar,
  .gsf-player-row,
  .gsf-toolbar-actions,
  .gsf-filters,
  .gsf-player-fields {
    display: flex;
    gap: var(--space-2);
    align-items: end;
    flex-wrap: wrap;
  }

  .gsf-toolbar {
    justify-content: space-between;
    margin-bottom: var(--space-3);
  }

  .gsf-player-picker {
    min-width: 360px;
    flex: 1;
  }

  .gsf-player-select {
    min-width: 220px;
  }

  .gsf-player-fields {
    margin-bottom: var(--space-3);
  }

  .gsf-player-fields label {
    flex: 1;
    min-width: 220px;
  }

  .gsf-item-image img {
    max-width: 100%;
    max-height: 100%;
    object-fit: contain;
    image-rendering: pixelated;
  }

  .gsf-setting-actions {
    display: flex;
    align-items: center;
    justify-content: flex-end;
    gap: var(--space-2);
    flex-wrap: wrap;
  }

  .gsf-setting-actions span {
    color: var(--text-muted);
    font-size: var(--text-xs);
    white-space: nowrap;
  }

  .gsf-sound-controls,
  .gsf-volume-control {
    display: flex;
    align-items: center;
    justify-content: flex-end;
    gap: var(--space-2);
    flex-wrap: wrap;
  }

  .gsf-volume-control input {
    width: 170px;
  }

  .gsf-volume-control span {
    min-width: 42px;
    color: var(--text-secondary);
    font-family: var(--font-mono);
    font-size: var(--text-sm);
    text-align: right;
  }

  .gsf-global-search {
    display: grid;
    gap: var(--space-2);
    margin-bottom: var(--space-4);
    padding: var(--space-3);
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-md);
    background: rgba(0, 0, 0, 0.14);
  }

  .gsf-global-search-row {
    display: flex;
    align-items: center;
    gap: var(--space-2);
    flex-wrap: wrap;
  }

  .gsf-global-search-row .input {
    flex: 1;
    min-width: 260px;
  }

  .gsf-global-results {
    display: grid;
    gap: var(--space-1);
    color: var(--text-muted);
    font-size: var(--text-xs);
  }

  .gsf-global-results strong {
    color: var(--accent-primary);
  }

  .gsf-items-panel {
    min-width: 0;
    width: 100%;
  }

  .gsf-add-row,
  .gsf-filters {
    display: flex;
    gap: var(--space-2);
    align-items: center;
    flex-wrap: wrap;
    margin-bottom: var(--space-3);
  }

  .gsf-add-name {
    flex: 1;
    min-width: 280px;
  }

  .gsf-filters .input {
    max-width: 220px;
  }

  .filter-select {
    padding: var(--space-2) var(--space-3);
    background: var(--input-bg);
    border: 1px solid var(--input-border);
    border-radius: var(--radius-md);
    color: var(--text-primary);
    font-size: var(--text-sm);
  }

  .gsf-item-list {
    display: grid;
    gap: var(--space-2);
  }

  .gsf-item-row {
    display: grid;
    grid-template-columns: 58px minmax(0, 1fr) auto;
    gap: var(--space-3);
    align-items: center;
    padding: var(--space-3);
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-md);
    background: rgba(0, 0, 0, 0.18);
  }

  .gsf-item-image {
    position: relative;
    width: 54px;
    height: 54px;
    display: grid;
    place-items: center;
    border: 1px solid #4e4942;
    background: rgba(0, 0, 0, 0.28);
  }

  .gsf-needed-count {
    position: absolute;
    right: 2px;
    bottom: 1px;
    padding: 0 3px;
    border-radius: 2px;
    background: rgba(0, 0, 0, 0.82);
    color: #ffd21f;
    font-family: var(--font-mono);
    font-size: var(--text-xs);
    font-weight: 800;
    line-height: 1.2;
    text-shadow: 0 1px 0 #000;
  }

  .gsf-all-item-row {
    grid-template-columns: 58px minmax(0, 1fr);
  }

  .gsf-all-item-title {
    display: flex;
    gap: var(--space-2);
    align-items: baseline;
    flex-wrap: wrap;
  }

  .gsf-all-item-title strong {
    color: var(--text-primary);
  }

  .gsf-all-item-title span {
    color: var(--text-muted);
    font-size: var(--text-xs);
  }

  .gsf-player-list {
    color: var(--accent-primary);
    font-size: var(--text-sm);
    line-height: 1.35;
  }

  .gsf-item-fields {
    display: grid;
    gap: var(--space-2);
  }

  .gsf-item-fields label {
    display: grid;
    gap: 2px;
    color: var(--text-muted);
    font-size: var(--text-xs);
  }

  .gsf-item-grid {
    display: grid;
    grid-template-columns: repeat(3, minmax(110px, 1fr));
    gap: var(--space-2);
  }

  .gsf-item-actions {
    display: grid;
    gap: var(--space-2);
    justify-items: end;
  }

  .gsf-status {
    font-family: var(--font-mono);
    font-size: var(--text-xs);
    text-transform: uppercase;
  }

  .status-needed { color: var(--accent-primary); }
  .status-found { color: var(--status-success-text); }
  .status-skipped { color: var(--text-muted); }

  .gsf-empty {
    padding: var(--space-5);
    color: var(--text-muted);
    text-align: center;
    border: 1px dashed var(--border-secondary);
    border-radius: var(--radius-md);
  }
</style>
