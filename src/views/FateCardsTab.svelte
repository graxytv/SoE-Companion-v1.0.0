<script lang="ts">
  import { onMount } from 'svelte';
  import { invoke } from '@tauri-apps/api/core';
  import { listen } from '@tauri-apps/api/event';
  import { Button, SubTabs, Toggle } from '../components';
  import { itemsDictionaryStore, settingsStore, type FateCardBackupStatus } from '../stores';
  import { buildHolyGrailItems } from '../lib/holy-grail';
  import { detailsForGrailItem, type GrailItemDetails } from '../lib/grail-item-details';
  import {
    SOE_13_FATE_CARD_TIERS,
    fateCardTierKey,
    fateCardTierLabel,
  } from '../lib/soe-13-items';

  interface RuneStashSyncResult {
    counts: Partial<Record<string, number>>;
    fate_card_counts?: Partial<Record<string, number>>;
    fate_card_sync_available?: boolean;
    scanned_files: string[];
    message: string;
  }

  const subTabs = [
    { id: 'card-list', label: 'Card List' },
    { id: 'overlay', label: 'Overlay Settings' },
    { id: 'backup', label: 'Backup' },
  ];
  const FATE_CARD_STASH_SYNC_INTERVAL_MS = 30 * 1000;

  let activeSubTab = $state('card-list');
  let fateCardSearch = $state('');
  let fateCardMode = $state<'all' | 'owned' | 'full' | 'incomplete'>('all');
  let syncBusy = $state(false);
  let syncMessage = $state('Shared-stash sync reads Fate Card item stacks from the selected pd2_shared.stash file.');
  let lastSyncAt = $state<string | null>(null);
  let detectedStashPaths = $state<string[]>([]);
  let stashPathDraft = $state('');
  let selectedDetails = $state<GrailItemDetails | null>(null);
  let backupBusy = $state(false);
  let backupMessage = $state('');
  let pendingRestore = $state(false);
  let backupStatus = $state<FateCardBackupStatus | null>(null);

  let grailItems = $derived(buildHolyGrailItems(itemsDictionaryStore.dict));
  let fateCardItems = $derived(
    grailItems.filter((item) => item.category === 'fateCards'),
  );
  let filteredFateCards = $derived(
    fateCardItems.filter((item) => {
      const count = fateCardCount(item.name);
      const stackSize = fateCardStackSize(item);
      const full = count >= stackSize;
      if (fateCardMode === 'owned' && count <= 0) return false;
      if (fateCardMode === 'full' && !full) return false;
      if (fateCardMode === 'incomplete' && full) return false;
      const q = fateCardSearch.trim().toLowerCase();
      if (!q) return true;
      return (
        item.name.toLowerCase().includes(q) ||
        String(item.fateCardReward ?? '').toLowerCase().includes(q) ||
        fateCardTierLabel(fateCardTier(item)).toLowerCase().includes(q)
      );
    }).toSorted((a, b) => {
      const tierDiff = fateCardTier(a) - fateCardTier(b);
      return tierDiff || a.name.localeCompare(b.name);
    }),
  );
  let fullFateCardStacks = $derived(
    fateCardItems.reduce((sum, item) => sum + fateCardFullStackCount(item), 0),
  );
  let ownedFateCardTypes = $derived(
    fateCardItems.filter((item) => fateCardCount(item.name) > 0).length,
  );
  let totalFateCardsOwned = $derived(
    fateCardItems.reduce((sum, item) => sum + fateCardCount(item.name), 0),
  );

  function formatDate(value: string | null | undefined): string {
    if (!value) return '-';
    const numeric = Number(value);
    const date = Number.isFinite(numeric) ? new Date(numeric) : new Date(value);
    return Number.isNaN(date.getTime())
      ? value
      : date.toLocaleString([], {
          year: 'numeric',
          month: 'short',
          day: 'numeric',
          hour: '2-digit',
          minute: '2-digit',
        });
  }

  function fateCardCount(name: string): number {
    return settingsStore.settings.fateCardCounts[name] ?? 0;
  }

  function fateCardDropCount(name: string): number {
    return settingsStore.settings.fateCardDropCounts[name] ?? 0;
  }

  function fateCardStackSize(item: { fateCardAmountRequired?: number | null }): number {
    return Math.max(1, Math.floor(item.fateCardAmountRequired ?? 1));
  }

  function fateCardTier(item: { qualityLevel?: number | null }): number {
    return Math.max(0, Math.floor(item.qualityLevel ?? 0));
  }

  function fateCardFullStackCount(item: { name: string; fateCardAmountRequired?: number | null }): number {
    return Math.floor(fateCardCount(item.name) / fateCardStackSize(item));
  }

  function tierDropTotal(tier: number): number {
    return fateCardItems
      .filter((item) => fateCardTier(item) === tier)
      .reduce((sum, item) => sum + fateCardDropCount(item.name), 0);
  }

  function fateCardSyncStatus(result: RuneStashSyncResult): string {
    if (result.fate_card_sync_available === false) {
      return 'No pd2_shared.stash file was found. Select your shared stash file to sync Fate Cards.';
    }
    const total = Object.values(result.fate_card_counts ?? {}).reduce<number>(
      (sum, count) => sum + (Number(count) || 0),
      0,
    );
    if (total <= 0) return 'No Fate Cards found in shared stash item slots.';
    return `${total} Fate Card${total === 1 ? '' : 's'} synced from shared stash item slots.`;
  }

  async function detectSharedStashPaths(): Promise<void> {
    try {
      const paths = await invoke<string[]>('detect_shared_stash_paths');
      detectedStashPaths = paths;
      const savedPath = settingsStore.settings.runewordPlannerStashPath;
      if (!savedPath && paths[0]) {
        settingsStore.setRunewordPlannerStashPath(paths[0]);
        stashPathDraft = paths[0];
      } else {
        stashPathDraft = savedPath ?? paths[0] ?? '';
      }
    } catch (error) {
      syncMessage = `Could not auto-detect shared stash files: ${error}`;
    }
  }

  async function syncFateCardsFromSharedStash(): Promise<void> {
    if (syncBusy) return;
    syncBusy = true;
    try {
      const result = await invoke<RuneStashSyncResult>('sync_shared_stash_runes', {
        stashPath: settingsStore.settings.runewordPlannerStashPath,
      });
      if (result.fate_card_sync_available !== false) {
        settingsStore.setFateCardCounts(result.fate_card_counts ?? {});
      }
      lastSyncAt = new Date().toISOString();
      syncMessage = fateCardSyncStatus(result);
    } catch (error) {
      syncMessage = `Shared-stash sync failed: ${error}`;
    } finally {
      syncBusy = false;
    }
  }

  function selectDetectedStashPath(event: Event): void {
    const value = (event.currentTarget as HTMLSelectElement).value;
    stashPathDraft = value;
    settingsStore.setRunewordPlannerStashPath(value || null);
    if (value) void syncFateCardsFromSharedStash();
  }

  function saveStashPath(): void {
    const path = stashPathDraft.trim();
    settingsStore.setRunewordPlannerStashPath(path || null);
    syncMessage = path
      ? 'Shared stash path saved. Use the header Sync button to refresh Fate Card counts.'
      : 'Shared stash path cleared. Press Detect Stash to auto-detect again.';
  }

  function openItemDetails(item: Parameters<typeof detailsForGrailItem>[0]): void {
    selectedDetails = detailsForGrailItem(item);
  }

  async function refreshBackupStatus(): Promise<void> {
    try {
      backupStatus = await settingsStore.getFateCardBackupStatus();
    } catch (error) {
      backupMessage = `Could not read Fate Card backup status: ${error}`;
    }
  }

  async function backupNow(): Promise<void> {
    backupBusy = true;
    backupMessage = '';
    try {
      const status = await settingsStore.backupFateCards();
      backupStatus = status;
      backupMessage = `Backup saved with ${status.cardCount} card type${status.cardCount !== 1 ? 's' : ''}.`;
    } catch (error) {
      backupMessage = `Backup failed: ${error}`;
    } finally {
      backupBusy = false;
    }
  }

  async function confirmRestore(): Promise<void> {
    backupBusy = true;
    backupMessage = '';
    try {
      const total = await settingsStore.restoreFateCardsBackup();
      backupMessage = `Backup restored and merged. ${total} total Fate Card${total !== 1 ? 's' : ''} tracked.`;
      await refreshBackupStatus();
    } catch (error) {
      backupMessage = `Restore failed: ${error}`;
    } finally {
      pendingRestore = false;
      backupBusy = false;
    }
  }

  async function openBackupFolder(): Promise<void> {
    try {
      await settingsStore.openFateCardBackupFolder();
    } catch (error) {
      backupMessage = `Could not open backup folder: ${error}`;
    }
  }

  onMount(() => {
    const unlisteners: Array<() => void> = [];
    stashPathDraft = settingsStore.settings.runewordPlannerStashPath ?? '';
    void detectSharedStashPaths().then(() => syncFateCardsFromSharedStash());
    void refreshBackupStatus();
    listen<RuneStashSyncResult>('master-shared-stash-synced', (event) => {
      if (event.payload.fate_card_sync_available !== false) {
        lastSyncAt = new Date().toISOString();
        syncMessage = fateCardSyncStatus(event.payload);
      }
    }).then((unlisten) => unlisteners.push(unlisten));
    const timer = window.setInterval(() => {
      void syncFateCardsFromSharedStash();
    }, FATE_CARD_STASH_SYNC_INTERVAL_MS);
    return () => {
      window.clearInterval(timer);
      unlisteners.forEach((unlisten) => unlisten());
    };
  });
</script>

<section class="tab-content fate-cards-tab">
  <div class="fate-hero">
    <div>
      <h2 class="section-title fate-title">Fate Cards</h2>
      <p class="section-description fate-description">
        Tracks card counts from shared stash item stacks, full-stack grail completion, and the dedicated Fate Cards overlay.
      </p>
    </div>
    <div class="progress-card">
      <span class="progress-percent">{fullFateCardStacks}</span>
      <span class="progress-count">Total Full Stacks</span>
      <span class="progress-latest">{totalFateCardsOwned} total cards tracked</span>
    </div>
  </div>

  <SubTabs tabs={subTabs} bind:activeTab={activeSubTab} ariaLabel="Fate Cards sections" />

  {#if activeSubTab === 'card-list'}
    <div class="settings-section">
      <div class="toolbar">
        <div>
          <h2 class="section-title">Card List</h2>
          <p class="section-description">
            Full stacks automatically complete the Fate Card Grail category.
          </p>
        </div>
        <div class="action-row">
          <Button variant="secondary" size="sm" onclick={detectSharedStashPaths}>Detect Stash</Button>
        </div>
      </div>

      <div class="stash-path-card">
        <label>
          <span>Shared Stash File</span>
          <select class="filter-select" value={settingsStore.settings.runewordPlannerStashPath ?? ''} onchange={selectDetectedStashPath}>
            <option value="">Auto-detect pd2_shared.stash</option>
            {#if settingsStore.settings.runewordPlannerStashPath && !detectedStashPaths.includes(settingsStore.settings.runewordPlannerStashPath)}
              <option value={settingsStore.settings.runewordPlannerStashPath}>Saved custom path</option>
            {/if}
            {#each detectedStashPaths as path}
              <option value={path}>{path}</option>
            {/each}
          </select>
        </label>
        <label class="stash-path-input">
          <span>Manual Path</span>
          <input
            class="filter-input"
            bind:value={stashPathDraft}
            placeholder="C:\Program Files (x86)\Diablo II\Save\pd2_shared.stash"
          />
        </label>
        <Button variant="secondary" size="sm" onclick={saveStashPath}>Save Path</Button>
      </div>

      <div class="summary-grid">
        <div><strong>{totalFateCardsOwned}</strong><span>Total Cards</span></div>
        <div><strong>{ownedFateCardTypes}</strong><span>Card Types Owned</span></div>
        <div><strong>{fullFateCardStacks}</strong><span>Full Stacks</span></div>
        <div><strong>{lastSyncAt ? formatDate(lastSyncAt) : 'Never'}</strong><span>Last Sync</span></div>
      </div>

      <p class="status-message">{syncMessage}</p>

      <div class="filters">
        <input class="filter-input" bind:value={fateCardSearch} placeholder="Search cards or rewards..." />
        <select class="filter-select" bind:value={fateCardMode}>
          <option value="all">All Cards</option>
          <option value="owned">Owned</option>
          <option value="full">Full Stacks</option>
          <option value="incomplete">Incomplete</option>
        </select>
      </div>

      <div class="fate-card-table">
        <div class="fate-card-row fate-card-header-row">
          <span>Card</span>
          <span>Card Tier</span>
          <span>Have</span>
          <span>Full Stack Size</span>
          <span>Total Full Stacks</span>
          <span>Full Stack Reward</span>
        </div>
        {#each filteredFateCards as item (item.key)}
          {@const count = fateCardCount(item.name)}
          {@const stackSize = fateCardStackSize(item)}
          <div class="fate-card-row" class:full-stack={count >= stackSize}>
            <button type="button" class="item-name-button fate-card-name-button category-fateCards" onclick={() => openItemDetails(item)}>
              <strong>{item.name}</strong>
              <small>{item.fateCardDropLocation ?? '-'}</small>
            </button>
            <span>{fateCardTierLabel(fateCardTier(item))}</span>
            <span>{count}</span>
            <span>{stackSize}</span>
            <span>{fateCardFullStackCount(item)}</span>
            <span class="fate-card-reward">{item.fateCardReward ?? '-'}</span>
          </div>
        {/each}
      </div>
    </div>
  {/if}

  {#if activeSubTab === 'overlay'}
    <div class="settings-section">
      <h2 class="section-title">Overlay Settings</h2>
      <p class="section-description">Choose which live Fate Card drop counters appear on the overlay. Card List counts still come from shared-stash sync.</p>

      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Show Fate Cards overlay</span>
          <span class="setting-hint">Move and resize it from the global overlay layout editor.</span>
        </div>
        <Toggle checked={settingsStore.settings.fateCardTrackerOverlayEnabled} onchange={(enabled) => settingsStore.setFateCardTrackerOverlayEnabled(enabled)} />
      </div>

      <div class="category-card">
        <div class="category-heading">
          <h3>Track Card Tiers</h3>
          <div class="inline-actions">
            <Button variant="ghost" size="sm" onclick={() => settingsStore.setAllFateCardTrackerOverlayTiers(true)}>All</Button>
            <Button variant="ghost" size="sm" onclick={() => settingsStore.setAllFateCardTrackerOverlayTiers(false)}>None</Button>
          </div>
        </div>
        <div class="fate-card-tier-grid">
          {#each SOE_13_FATE_CARD_TIERS as tier}
            {@const tierKey = fateCardTierKey(tier)}
            <label class="fate-card-tier-toggle">
              <span>{fateCardTierLabel(tier)}</span>
              <strong>{tierDropTotal(tier)}</strong>
              <Toggle checked={settingsStore.settings.fateCardTrackerOverlayTiers[tierKey] ?? true} onchange={(enabled) => settingsStore.setFateCardTrackerOverlayTier(tier, enabled)} />
            </label>
          {/each}
        </div>
      </div>

      <div class="category-card">
        <div class="category-heading">
          <h3>Track Specific Cards</h3>
          <div class="inline-actions">
            <Button variant="ghost" size="sm" onclick={() => settingsStore.setAllFateCardTrackerOverlayCards(true)}>All</Button>
            <Button variant="ghost" size="sm" onclick={() => settingsStore.setAllFateCardTrackerOverlayCards(false)}>None</Button>
          </div>
        </div>
        <div class="filters overlay-filter">
          <input class="filter-input" bind:value={fateCardSearch} placeholder="Search cards..." />
          <select class="filter-select" bind:value={fateCardMode}>
            <option value="all">All Cards</option>
            <option value="owned">Owned</option>
            <option value="full">Full Stacks</option>
            <option value="incomplete">Incomplete</option>
          </select>
        </div>
        <div class="fate-card-toggle-list">
          {#each filteredFateCards as item (item.key + '-overlay')}
            <label class="fate-card-toggle-row">
              <span class="fate-card-toggle-name">{item.name}</span>
              <small>{fateCardTierLabel(fateCardTier(item))}</small>
              <strong>{fateCardDropCount(item.name)}</strong>
              <Toggle checked={settingsStore.settings.fateCardTrackerOverlayCards[item.name] ?? false} onchange={(enabled) => settingsStore.setFateCardTrackerOverlayCard(item.name, enabled)} />
            </label>
          {/each}
        </div>
      </div>
    </div>
  {/if}

  {#if activeSubTab === 'backup'}
    <div class="settings-section">
      <h2 class="section-title">Backup</h2>
      <p class="section-description">Back up and restore Fate Card counts. Restores merge by keeping the highest count per card.</p>
      <div class="backup-card">
        <div class="backup-info">
          <span><strong>Backup file:</strong> {backupStatus?.backupExists ? 'Found' : 'Not created yet'}</span>
          <span><strong>Backed up card types:</strong> {backupStatus?.cardCount ?? 0}</span>
          <span><strong>Last backup:</strong> {formatDate(backupStatus?.exportedAt)}</span>
          <span class="backup-path">{backupStatus?.backupPath ?? ''}</span>
        </div>
        <div class="backup-actions">
          <Button variant="secondary" size="sm" disabled={backupBusy} onclick={backupNow}>Backup Now</Button>
          <Button variant="secondary" size="sm" disabled={backupBusy || !backupStatus?.backupExists} onclick={() => { pendingRestore = true; }}>Restore/Merge</Button>
          <Button variant="ghost" size="sm" onclick={openBackupFolder}>Open Folder</Button>
        </div>
      </div>
      {#if backupMessage}
        <p class="status-message">{backupMessage}</p>
      {/if}
    </div>
  {/if}

  {#if pendingRestore}
    <div class="confirm-backdrop" role="presentation">
      <div class="confirm-dialog" role="dialog" aria-modal="true">
        <p>Restore and merge the Fate Card backup into your current counts? Current higher counts will be kept.</p>
        <div class="confirm-actions">
          <Button variant="primary" size="sm" disabled={backupBusy} onclick={confirmRestore}>Yes</Button>
          <Button variant="secondary" size="sm" disabled={backupBusy} onclick={() => { pendingRestore = false; }}>No</Button>
        </div>
      </div>
    </div>
  {/if}

  {#if selectedDetails}
    <div class="confirm-backdrop" role="presentation" onclick={() => { selectedDetails = null; }}>
      <div class="item-detail-dialog" role="dialog" aria-modal="true" onclick={(event) => event.stopPropagation()}>
        <div class="item-detail-header">
          <div>
            <p>{selectedDetails.subtitle}</p>
            <h3>{selectedDetails.title}</h3>
          </div>
          <Button variant="secondary" size="sm" onclick={() => { selectedDetails = null; }}>Close</Button>
        </div>

        {#if selectedDetails.meta.length > 0}
          <div class="item-detail-meta">
            {#each selectedDetails.meta as row}
              <div>
                <span>{row.label}</span>
                <strong>{row.value}</strong>
              </div>
            {/each}
          </div>
        {/if}

        {#if selectedDetails.properties.length > 0}
          <div class="item-detail-properties">
            {#each selectedDetails.properties as property}
              <span>{property}</span>
            {/each}
          </div>
        {:else}
          <div class="item-detail-empty">No stat lines available.</div>
        {/if}

        {#if selectedDetails.note}
          <p class="item-detail-note">{selectedDetails.note}</p>
        {/if}
      </div>
    </div>
  {/if}
</section>

<style>
  .fate-cards-tab {
    display: flex;
    flex-direction: column;
    gap: var(--space-4);
  }

  .fate-hero {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-4);
    padding: var(--space-5);
    border: 1px solid var(--border-primary);
    background: var(--bg-secondary);
    box-shadow: var(--shadow-sm);
  }

  .fate-title {
    color: #d7a8ff;
  }

  .fate-description {
    margin: 0;
    color: color-mix(in srgb, #d7a8ff 82%, var(--text-secondary));
  }

  .section-description {
    margin: -6px 0 14px;
    color: var(--text-muted);
    font-size: 13px;
  }

  .progress-card {
    min-width: 190px;
    display: grid;
    gap: 8px;
    padding: 18px;
    border: 1px solid color-mix(in srgb, #d7a8ff 58%, transparent);
    border-radius: 8px;
    background: color-mix(in srgb, var(--bg-primary) 82%, transparent);
    text-align: right;
  }

  .progress-percent {
    color: #d7a8ff;
    font-family: var(--font-display);
    font-size: 32px;
  }

  .progress-count,
  .progress-latest {
    color: var(--text-primary);
    font-size: 12px;
  }

  .toolbar {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    gap: 16px;
  }

  .action-row,
  .inline-actions,
  .backup-actions {
    display: flex;
    flex-wrap: wrap;
    justify-content: flex-end;
    gap: 8px;
  }

  .stash-path-card {
    display: grid;
    grid-template-columns: minmax(220px, 0.7fr) minmax(300px, 1fr) auto;
    align-items: end;
    gap: 10px;
    margin-bottom: 10px;
    padding: 12px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
  }

  .stash-path-card label {
    display: flex;
    min-width: 0;
    flex-direction: column;
    gap: 6px;
    color: var(--text-secondary);
    font-size: 12px;
  }

  .filter-input,
  .filter-select {
    width: 100%;
    padding: 8px 10px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
    color: var(--text-primary);
    font-size: 13px;
  }

  .stash-path-card .filter-input,
  .stash-path-card .filter-select {
    background: var(--bg-primary);
  }

  .summary-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(130px, 1fr));
    gap: 8px;
    padding: 12px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
  }

  .summary-grid div {
    display: flex;
    flex-direction: column;
    gap: 3px;
  }

  .summary-grid strong {
    color: #d7a8ff;
    font-family: var(--font-mono);
    font-size: 18px;
  }

  .summary-grid span,
  .status-message {
    color: var(--text-muted);
    font-size: 12px;
  }

  .filters {
    display: grid;
    grid-template-columns: minmax(220px, 1fr) 180px;
    gap: 10px;
    margin: 14px 0;
  }

  .overlay-filter {
    margin-top: 0;
  }

  .fate-card-table {
    overflow: auto;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
  }

  .fate-card-row {
    display: grid;
    grid-template-columns: minmax(190px, 1.05fr) 86px 70px 118px 142px minmax(180px, 1.1fr);
    align-items: center;
    gap: 10px;
    min-width: 880px;
    padding: 9px 12px;
    border-bottom: 1px solid var(--border-primary);
    background: var(--bg-secondary);
    color: var(--text-secondary);
    font-size: 13px;
  }

  .fate-card-row:last-child {
    border-bottom: none;
  }

  .fate-card-header-row {
    background: var(--bg-tertiary, var(--bg-secondary));
    color: var(--text-primary);
    font-size: 12px;
    font-weight: 800;
    text-transform: uppercase;
  }

  .fate-card-row.full-stack {
    background: color-mix(in srgb, #d7a8ff 8%, var(--bg-secondary));
  }

  .item-name-button {
    appearance: none;
    align-items: flex-start;
    border: 0;
    background: transparent;
    cursor: pointer;
    padding: 0;
    text-align: left;
    font: inherit;
  }

  .item-name-button:hover strong,
  .item-name-button:focus-visible strong {
    text-decoration: underline;
    text-underline-offset: 3px;
  }

  .fate-card-name-button {
    display: flex;
    min-width: 0;
    flex-direction: column;
    gap: 2px;
    color: #d7a8ff;
  }

  .fate-card-name-button strong,
  .fate-card-name-button small,
  .fate-card-reward {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .fate-card-name-button small {
    color: var(--text-muted);
    font-size: 11px;
  }

  .category-card,
  .backup-card {
    margin-top: 14px;
    padding: 14px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
  }

  .category-heading {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin-bottom: 12px;
  }

  .category-heading h3 {
    margin: 0;
    color: var(--text-primary);
    font-size: 14px;
  }

  .fate-card-tier-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(112px, 1fr));
    gap: 8px;
  }

  .fate-card-tier-toggle {
    display: grid;
    grid-template-columns: auto 1fr auto;
    align-items: center;
    gap: 8px;
    padding: 8px 10px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-primary);
    color: var(--text-secondary);
    font-size: 12px;
  }

  .fate-card-tier-toggle span {
    color: #d7a8ff;
    font-weight: 800;
  }

  .fate-card-tier-toggle strong {
    justify-self: end;
    color: var(--text-primary);
    font-family: var(--font-mono);
  }

  .fate-card-toggle-list {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
    gap: 6px;
    max-height: 430px;
    overflow: auto;
    padding-right: 4px;
  }

  .fate-card-toggle-row {
    display: grid;
    grid-template-columns: minmax(0, 1fr) 38px 42px auto;
    align-items: center;
    gap: 8px;
    min-width: 0;
    padding: 7px 9px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-primary);
    color: var(--text-secondary);
    font-size: 12px;
  }

  .fate-card-toggle-name {
    min-width: 0;
    overflow: hidden;
    color: #d7a8ff;
    font-weight: 700;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .fate-card-toggle-row small,
  .fate-card-toggle-row strong {
    text-align: right;
  }

  .fate-card-toggle-row small {
    color: var(--accent-primary);
    font-weight: 800;
  }

  .fate-card-toggle-row strong {
    color: var(--text-primary);
    font-family: var(--font-mono);
  }

  .setting-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
    padding: 12px 0;
    border-bottom: 1px solid var(--border-primary);
  }

  .setting-info {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .setting-label {
    color: var(--text-primary);
    font-weight: 700;
  }

  .setting-hint {
    color: var(--text-muted);
    font-size: 12px;
  }

  .backup-card {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    gap: 16px;
  }

  .backup-info {
    display: flex;
    flex-direction: column;
    gap: 5px;
    min-width: 0;
    color: var(--text-secondary);
    font-size: 13px;
  }

  .backup-path {
    max-width: 620px;
    overflow: hidden;
    color: var(--text-muted);
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .confirm-backdrop {
    position: fixed;
    inset: 0;
    z-index: 50;
    display: flex;
    align-items: center;
    justify-content: center;
    background: rgba(0, 0, 0, 0.45);
  }

  .confirm-dialog,
  .item-detail-dialog {
    width: min(640px, calc(100vw - 48px));
    max-height: min(720px, calc(100vh - 48px));
    overflow: auto;
    padding: 18px;
    border: 1px solid var(--border-primary);
    border-radius: 10px;
    background: var(--bg-primary);
    box-shadow: 0 18px 48px rgba(0, 0, 0, 0.45);
  }

  .confirm-dialog {
    width: min(420px, calc(100vw - 48px));
  }

  .confirm-dialog p {
    margin: 0 0 18px;
    color: var(--text-primary);
    line-height: 1.45;
  }

  .confirm-actions {
    display: flex;
    justify-content: flex-end;
    gap: 10px;
  }

  .item-detail-header {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    gap: 16px;
    margin-bottom: 14px;
    padding-bottom: 12px;
    border-bottom: 1px solid var(--border-primary);
  }

  .item-detail-header p {
    margin: 0 0 4px;
    color: var(--accent-primary);
    font-size: 12px;
    font-weight: 700;
    text-transform: uppercase;
  }

  .item-detail-header h3 {
    margin: 0;
    color: var(--text-primary);
    font-size: 22px;
  }

  .item-detail-meta {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 8px;
    margin-bottom: 14px;
  }

  .item-detail-meta div {
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding: 8px 10px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
  }

  .item-detail-meta span {
    color: var(--text-muted);
    font-size: 11px;
    text-transform: uppercase;
  }

  .item-detail-meta strong {
    color: var(--text-primary);
    font-size: 13px;
    line-height: 1.35;
  }

  .item-detail-properties {
    display: grid;
    gap: 5px;
    padding: 12px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
    color: #65b8ff;
    font-size: 13px;
    line-height: 1.35;
  }

  .item-detail-empty {
    padding: 12px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
    color: var(--text-muted);
    font-size: 13px;
  }

  .item-detail-note {
    margin: 12px 0 0;
    color: var(--text-secondary);
    font-size: 12px;
    line-height: 1.45;
  }
</style>
