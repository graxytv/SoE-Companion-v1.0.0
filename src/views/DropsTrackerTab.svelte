<script lang="ts">
  import { invoke } from '@tauri-apps/api/core';
  import { emit, listen } from '@tauri-apps/api/event';
  import { onMount } from 'svelte';
  import { Toggle, Button, HotkeyInput, SubTabs } from '../components';
  import IdentifiedDropsPanel from '../components/IdentifiedDropsPanel.svelte';
  import { settingsStore, lootHistoryStore, type HotkeyConfig } from '../stores';
  import {
    DROP_TRACKER_CATEGORIES,
    RUNE_NAMES,
    categoryLabel,
    type DropTrackerCategoryKey,
    type RuneName,
  } from '../lib/drop-tracker-categories';
  import { MATERIAL_TRACKER_NAMES, type MaterialTrackerName } from '../lib/material-tracker';

  const DROP_TRACKER_RESET_WARNING = 'Are you sure you want to reset the Drops Tracker? This cannot be undone.';
  const TOTAL_DROPS_RESET_WARNING = 'Are you sure you want to reset the Total Drops Tracker? This cannot be undone.';

  let { activeSubTab = $bindable('overview') }: { activeSubTab?: string } = $props();

  const subTabs = [
    { id: 'overview', label: 'Overview' },
    { id: 'tracker-settings', label: 'Tracker Settings' },
    { id: 'drops-overlay', label: 'Drops Tracker Overlay Settings' },
    { id: 'total-overlay', label: 'Total Drops Overlay Settings' },
    { id: 'mats-tracker', label: 'Mats Tracker' },
    { id: 'rune-tracker', label: 'Rune Tracker' },
    { id: 'muling-mode', label: 'Muling Mode' },
  ];

  let orderedSubTabs = $derived(applyTabOrder(subTabs, settingsStore.settings.dropsTrackerSubTabOrder));

  let pendingConfirm: null | 'drops-tracker' | 'run-counter' | 'total-drops' = $state(null);

  let dropsTrackerEnabled = $derived(settingsStore.settings.dropsTrackerEnabled);
  let totalDropsTrackerEnabled = $derived(settingsStore.settings.totalDropsTrackerEnabled);
  let dropsTrackerRunCounterEnabled = $derived(settingsStore.settings.dropsTrackerRunCounterEnabled);
  let dropsTrackerRunTimerEnabled = $derived(settingsStore.settings.dropsTrackerRunTimerEnabled);
  let dropsTrackerSessionTimerEnabled = $derived(settingsStore.settings.dropsTrackerSessionTimerEnabled);
  let dropsTrackerMulingMode = $derived(settingsStore.settings.dropsTrackerMulingMode);
  let mulingIndicatorOverlayEnabled = $derived(settingsStore.settings.mulingIndicatorOverlayEnabled);
  let trackerOverlaysSeparateWindow = $derived(settingsStore.settings.trackerOverlaysSeparateWindow);
  let runeTrackerOverlayEnabled = $derived(settingsStore.settings.runeTrackerOverlayEnabled);
  let materialTrackerOverlayEnabled = $derived(settingsStore.settings.materialTrackerOverlayEnabled);
  let materialTrackerOverlayMaterials = $derived(settingsStore.settings.materialTrackerOverlayMaterials);
  let materialTrackerCounts = $derived(settingsStore.settings.materialTrackerCounts);
  let runeTrackerOverlayRunes = $derived(settingsStore.settings.runeTrackerOverlayRunes);
  let runeTrackerCounts = $derived(settingsStore.settings.runeTrackerCounts);
  let dropsTrackerCategories = $derived(settingsStore.settings.dropsTrackerCategories);
  let totalDropsTrackerCategories = $derived(settingsStore.settings.totalDropsTrackerCategories);
  let dropsTrackerRunCount = $derived(settingsStore.settings.dropsTrackerRunCount);
  let timerNow = $state(Date.now());
  let dropsTrackerRunElapsedMs = $derived(settingsStore.getDropsTrackerRunDisplayElapsedMs(timerNow));
  let dropsTrackerSessionElapsedMs = $derived(settingsStore.getDropsTrackerSessionDisplayElapsedMs(timerNow));
  let recentTrackedItems = $derived(settingsStore.settings.dropsTrackerRecentItems);
  let totalDropsCount = $derived(
    Object.values(settingsStore.settings.totalDropsTrackerCounts).reduce((sum, n) => sum + (n ?? 0), 0),
  );

  let confirmMessage = $derived(
    pendingConfirm === 'total-drops'
      ? TOTAL_DROPS_RESET_WARNING
      : DROP_TRACKER_RESET_WARNING,
  );

  function applyTabOrder<T extends { id: string }>(items: T[], order: string[]): T[] {
    const byId = new Map(items.map((item) => [item.id, item]));
    const ordered: T[] = [];
    const seen = new Set<string>();
    for (const id of order) {
      const item = byId.get(id);
      if (!item || seen.has(id)) continue;
      ordered.push(item);
      seen.add(id);
    }
    for (const item of items) {
      if (!seen.has(item.id)) ordered.push(item);
    }
    return ordered;
  }

  onMount(() => {
    const timer = window.setInterval(() => {
      timerNow = Date.now();
    }, 1000);
    let pendingTimerPause: number | null = null;
    let unlistenGameStatus: (() => void) | null = null;

    const clearPendingTimerPause = () => {
      if (pendingTimerPause == null) return;
      window.clearTimeout(pendingTimerPause);
      pendingTimerPause = null;
    };

    listen<string>('game-status', (event) => {
      const ingame = event.payload === 'ingame';
      if (ingame) {
        clearPendingTimerPause();
        settingsStore.setDropsTrackerTimersDisplayActive(true);
        timerNow = Date.now();
        return;
      }

      clearPendingTimerPause();
      pendingTimerPause = window.setTimeout(() => {
        pendingTimerPause = null;
        settingsStore.setDropsTrackerTimersDisplayActive(false);
        timerNow = Date.now();
      }, 2500);
    }).then((u) => {
      unlistenGameStatus = u;
    });

    invoke('get_game_status').then((status: unknown) => {
      settingsStore.setDropsTrackerTimersDisplayActive(status === 'ingame');
      timerNow = Date.now();
    }).catch(() => {
      settingsStore.setDropsTrackerTimersDisplayActive(false);
    });

    return () => {
      window.clearInterval(timer);
      clearPendingTimerPause();
      if (unlistenGameStatus) unlistenGameStatus();
    };
  });

  function askResetDropsTracker() {
    pendingConfirm = 'drops-tracker';
  }

  function askResetRunCounter() {
    pendingConfirm = 'run-counter';
  }

  function askResetTotalDropsTracker() {
    pendingConfirm = 'total-drops';
  }

  async function confirmReset() {
    if (pendingConfirm === 'drops-tracker') {
      settingsStore.resetDropsTrackerCounts();
      await lootHistoryStore.clear();
    } else if (pendingConfirm === 'run-counter') {
      settingsStore.resetDropsTrackerRunCount();
    } else if (pendingConfirm === 'total-drops') {
      settingsStore.resetTotalDropsTrackerCounts();
    }
    pendingConfirm = null;
  }

  function setDropsCategory(key: DropTrackerCategoryKey, enabled: boolean) {
    settingsStore.setDropsTrackerCategory(key, enabled);
  }

  function setTotalCategory(key: DropTrackerCategoryKey, enabled: boolean) {
    settingsStore.setTotalDropsTrackerCategory(key, enabled);
  }

  function formatRunTime(ms: number): string {
    const totalSeconds = Math.floor(ms / 1000);
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = totalSeconds % 60;
    return `${minutes}:${seconds.toString().padStart(2, '0')}`;
  }

  function formatSessionTime(ms: number): string {
    const totalSeconds = Math.floor(ms / 1000);
    const hours = Math.floor(totalSeconds / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    const seconds = totalSeconds % 60;
    return `${hours}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
  }

  function resetRunTimer(): void {
    settingsStore.resetDropsTrackerRunTimer();
  }

  function resetSessionTimer(): void {
    settingsStore.resetDropsTrackerSessionTimer();
  }

  function setMulingMode(enabled: boolean): void {
    settingsStore.setDropsTrackerMulingMode(enabled);
    invoke('set_muling_banner_window_visible', { visible: enabled && settingsStore.settings.mulingIndicatorOverlayEnabled }).catch((err) => {
      console.error('[DropsTrackerTab] Failed to update muling banner window:', err);
    });
  }

  function setMulingIndicatorOverlayEnabled(enabled: boolean): void {
    settingsStore.setMulingIndicatorOverlayEnabled(enabled);
    invoke('set_muling_banner_window_visible', { visible: enabled && settingsStore.settings.dropsTrackerMulingMode }).catch((err) => {
      console.error('[DropsTrackerTab] Failed to update muling banner window:', err);
    });
  }

  function setMulingHotkey(hotkey: HotkeyConfig): void {
    settingsStore.setMulingModeHotkey(hotkey);
  }

  function resetMulingIndicatorPosition(): void {
    settingsStore.setMulingIndicatorOverlayPosition({ x: null, y: null });
    settingsStore.setMulingIndicatorOverlayWidth(58);
    settingsStore.setMulingIndicatorOverlayHeight(58);
  }

  async function refreshTrackerOverlays() {
    try {
      await emit('refresh-tracker-overlays');
      await invoke('sync_overlay_with_game').catch(() => {});
      await invoke('set_overlay_interactive', { active: false }).catch(() => {});
      window.setTimeout(() => {
        void emit('refresh-tracker-overlays');
        void invoke('sync_overlay_with_game').catch(() => {});
      }, 120);
    } catch (err) {
      console.error('[DropsTrackerTab] Failed to refresh tracker overlays:', err);
    }
  }

  function handleTrackerOverlayWindowChange(enabled: boolean) {
    settingsStore.setTrackerOverlaysSeparateWindow(enabled);
    invoke('set_tracker_overlay_window_visible', { visible: enabled }).catch((err) => {
      console.error('[DropsTrackerTab] Failed to toggle tracker overlay window:', err);
    });
  }

  function runeCount(rune: RuneName): number {
    return runeTrackerCounts[rune] ?? 0;
  }

  function materialCount(material: MaterialTrackerName): number {
    return materialTrackerCounts[material] ?? 0;
  }

  function materialTotal(): number {
    return MATERIAL_TRACKER_NAMES.reduce((sum, material) => sum + materialCount(material), 0);
  }

  function runeTotal(): number {
    return RUNE_NAMES.reduce((sum, rune) => sum + runeCount(rune), 0);
  }

  function formatTrackedTime(timestampMs: number): string {
    return new Date(timestampMs).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  }

  function categorySummary(categories: DropTrackerCategoryKey[]): string {
    return categories.map(categoryLabel).join(', ');
  }
</script>

<section class="tab-content drops-tracker-tab">
  <SubTabs
    tabs={orderedSubTabs}
    bind:activeTab={activeSubTab}
    ariaLabel="Drops Tracker sections"
    onReorder={(ids) => settingsStore.setDropsTrackerSubTabOrder(ids)}
  />

  {#if activeSubTab === 'tracker-settings'}
  <div class="settings-section tracker-section options-section">
    <div class="tracker-heading">
      <h2 class="section-title tracker-title-gold">Options</h2>
      <p class="section-description tracker-description-gold">Shared controls for tracker overlays and muling pauses.</p>
    </div>

    <div class="settings-panel-grid">
      <div class="settings-card">
        <div class="settings-card-heading">
          <h3>Overlay Window</h3>
          <span>Separate tracker display</span>
        </div>
        <div class="setting-row compact">
          <div class="setting-info">
            <span class="setting-label">Toggle Overlay Window</span>
            <span class="setting-hint">Move tracker, total drops, grail, and rune overlays into a separate scrollable window.</span>
          </div>
          <Toggle checked={trackerOverlaysSeparateWindow} onchange={handleTrackerOverlayWindowChange} />
        </div>
      </div>

      <div class="settings-card">
        <div class="settings-card-heading">
          <h3>Overlay Actions</h3>
          <span>Repair layout and repainting</span>
        </div>
        <div class="action-row">
          <Button variant="secondary" size="sm" onclick={() => { settingsStore.resetDropTrackerOverlayPositions(); void refreshTrackerOverlays(); }}>Reset Positions</Button>
          <Button variant="secondary" size="sm" onclick={refreshTrackerOverlays}>Refresh Overlays</Button>
        </div>
        <p class="settings-card-note">Reset moves Drops Tracker, Total Drops, Grail Progress, and Rune Tracker overlays back to default. Refresh repaints from saved tracker state.</p>
      </div>
    </div>
  </div>
  {:else if activeSubTab === 'drops-overlay'}
  <div class="settings-section tracker-section">
    <div class="tracker-heading">
      <h2 class="section-title tracker-title-gold">Drops Tracker Overlay</h2>
      <p class="section-description tracker-description-gold">Manually resettable counts for your current tracking window. Tracking continues while the overlay is hidden.</p>
    </div>

    <div class="settings-panel-grid">
      <div class="settings-card">
        <div class="settings-card-heading">
          <h3>Visibility</h3>
          <span>Main overlay</span>
        </div>
        <div class="setting-row compact">
          <div class="setting-info">
            <span class="setting-label">Show Drops Tracker Overlay</span>
            <span class="setting-hint">Tracking continues while hidden.</span>
          </div>
          <Toggle checked={dropsTrackerEnabled} onchange={(enabled) => settingsStore.setDropsTrackerEnabled(enabled)} />
        </div>
      </div>

      <div class="settings-card">
        <div class="settings-card-heading">
          <h3>Overlay Fields</h3>
          <span>Rows shown on the card</span>
        </div>
        <div class="switch-list">
          <label class="inline-switch"><span>Run Counter</span><Toggle checked={dropsTrackerRunCounterEnabled} onchange={(enabled) => settingsStore.setDropsTrackerRunCounterEnabled(enabled)} /></label>
          <label class="inline-switch"><span>Run Timer</span><Toggle checked={dropsTrackerRunTimerEnabled} onchange={(enabled) => settingsStore.setDropsTrackerRunTimerEnabled(enabled)} /></label>
          <label class="inline-switch"><span>Session Timer</span><Toggle checked={dropsTrackerSessionTimerEnabled} onchange={(enabled) => settingsStore.setDropsTrackerSessionTimerEnabled(enabled)} /></label>
        </div>
      </div>

      <div class="settings-card">
        <div class="settings-card-heading">
          <h3>Maintenance</h3>
          <span>Manual resets</span>
        </div>
        <div class="metric-actions">
          <div><strong>{dropsTrackerRunCount}</strong><span>Run Count</span><Button variant="danger" size="sm" onclick={askResetRunCounter}>Reset</Button></div>
          <div><strong>{formatRunTime(dropsTrackerRunElapsedMs)}</strong><span>Run Time</span><Button variant="danger" size="sm" onclick={resetRunTimer}>Reset</Button></div>
          <div><strong>{formatSessionTime(dropsTrackerSessionElapsedMs)}</strong><span>Session</span><Button variant="danger" size="sm" onclick={resetSessionTimer}>Reset</Button></div>
        </div>
      </div>

      <div class="settings-card">
        <div class="settings-card-heading">
          <h3>Tracker Counts</h3>
          <span>Current run data</span>
        </div>
        <p class="settings-card-note">Clear the Drops Tracker counts and recent tracked item history manually.</p>
        <Button variant="danger" size="sm" onclick={askResetDropsTracker}>Reset Drops Tracker</Button>
      </div>
    </div>

    <div class="category-card">
      <h3>Tracked Categories</h3>
      <div class="category-grid">
        {#each DROP_TRACKER_CATEGORIES as category}
          <label class="category-toggle">
            <span>{categoryLabel(category.key)}</span>
            <Toggle checked={dropsTrackerCategories[category.key]} onchange={(enabled) => setDropsCategory(category.key, enabled)} />
          </label>
        {/each}
      </div>
    </div>
  </div>

  {:else if activeSubTab === 'total-overlay'}
  <div class="settings-section tracker-section">
    <div class="tracker-heading">
      <h2 class="section-title tracker-title-gold">Total Drops Overlay</h2>
      <p class="section-description tracker-description-gold">Persistent lifetime counts. This tracker does not reset when the game or app restarts.</p>
    </div>

    <div class="settings-panel-grid">
      <div class="settings-card">
        <div class="settings-card-heading">
          <h3>Visibility</h3>
          <span>Lifetime overlay</span>
        </div>
        <div class="setting-row compact">
          <div class="setting-info">
            <span class="setting-label">Show Total Drops Overlay</span>
            <span class="setting-hint">Tracking continues while hidden.</span>
          </div>
          <Toggle checked={totalDropsTrackerEnabled} onchange={(enabled) => settingsStore.setTotalDropsTrackerEnabled(enabled)} />
        </div>
      </div>

      <div class="settings-card">
        <div class="settings-card-heading">
          <h3>Lifetime Counts</h3>
          <span>{totalDropsCount.toLocaleString()} tracked</span>
        </div>
        <p class="settings-card-note">Reset the persistent lifetime tracker counts. This cannot be undone.</p>
        <Button variant="danger" size="sm" onclick={askResetTotalDropsTracker}>Reset Total Drops Tracker</Button>
      </div>
    </div>

    <div class="category-card">
      <h3>Tracked Categories</h3>
      <div class="category-grid">
        {#each DROP_TRACKER_CATEGORIES as category}
          <label class="category-toggle">
            <span>{categoryLabel(category.key)}</span>
            <Toggle checked={totalDropsTrackerCategories[category.key]} onchange={(enabled) => setTotalCategory(category.key, enabled)} />
          </label>
        {/each}
      </div>
    </div>
  </div>
  {:else if activeSubTab === 'mats-tracker'}
  <div class="settings-section tracker-section">
    <div class="tracker-heading">
      <h2 class="section-title tracker-title-gold">Mats Tracker</h2>
      <p class="section-description tracker-description-gold">Per-material counts with an overlay that can show only the SoE materials you choose.</p>
    </div>

    <div class="settings-cluster">
      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Show Mats Tracker overlay</span>
          <span class="setting-hint">Turn the Mats Tracker overlay on or off. Counts continue while hidden.</span>
        </div>
        <Toggle checked={materialTrackerOverlayEnabled} onchange={(enabled) => settingsStore.setMaterialTrackerOverlayEnabled(enabled)} />
      </div>

      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Reset Mats Tracker</span>
          <span class="setting-hint">Current material drops tracked: {materialTotal()}</span>
        </div>
        <Button variant="danger" size="sm" onclick={() => settingsStore.resetMaterialTrackerCounts()}>Reset Material Counts</Button>
      </div>
    </div>

    <div class="category-card">
      <div class="category-card-heading">
        <h3>Overlay Materials</h3>
        <div class="category-card-actions">
          <Button variant="secondary" size="sm" onclick={() => settingsStore.setAllMaterialTrackerOverlayMaterials(true)}>All</Button>
          <Button variant="secondary" size="sm" onclick={() => settingsStore.setAllMaterialTrackerOverlayMaterials(false)}>None</Button>
        </div>
      </div>
      <div class="material-grid">
        {#each MATERIAL_TRACKER_NAMES as material}
          <label class="material-toggle">
            <span class="material-name">{material}</span>
            <span class="material-count">{materialCount(material)}</span>
            <Toggle checked={materialTrackerOverlayMaterials[material]} onchange={(enabled) => settingsStore.setMaterialTrackerOverlayMaterial(material, enabled)} />
          </label>
        {/each}
      </div>
    </div>
  </div>
  {:else if activeSubTab === 'rune-tracker'}
  <div class="settings-section tracker-section">
    <div class="tracker-heading">
      <h2 class="section-title tracker-title-gold">Rune Tracker</h2>
      <p class="section-description tracker-description-gold">Per-rune counts with a separate overlay that can show only the runes you care about.</p>
    </div>

    <div class="settings-cluster">
      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Show Rune Tracker overlay</span>
          <span class="setting-hint">Turn the Rune Tracker overlay on or off. Counts continue while hidden.</span>
        </div>
        <Toggle checked={runeTrackerOverlayEnabled} onchange={(enabled) => settingsStore.setRuneTrackerOverlayEnabled(enabled)} />
      </div>

      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Reset Rune Tracker</span>
          <span class="setting-hint">Current rune drops tracked: {runeTotal()}</span>
        </div>
        <Button variant="danger" size="sm" onclick={() => settingsStore.resetRuneTrackerCounts()}>Reset Rune Counts</Button>
      </div>
    </div>

    <div class="category-card">
      <div class="category-card-heading">
        <h3>Overlay Runes</h3>
        <div class="category-card-actions">
          <Button variant="secondary" size="sm" onclick={() => settingsStore.setAllRuneTrackerOverlayRunes(true)}>All</Button>
          <Button variant="secondary" size="sm" onclick={() => settingsStore.setAllRuneTrackerOverlayRunes(false)}>None</Button>
        </div>
      </div>
      <div class="rune-grid">
        {#each RUNE_NAMES as rune}
          <label class="rune-toggle">
            <span class="rune-name">{rune}</span>
            <span class="rune-count">{runeCount(rune)}</span>
            <Toggle checked={runeTrackerOverlayRunes[rune]} onchange={(enabled) => settingsStore.setRuneTrackerOverlayRune(rune, enabled)} />
          </label>
        {/each}
      </div>
    </div>
  </div>
  {:else if activeSubTab === 'muling-mode'}
  <div class="settings-section tracker-section">
    <div class="tracker-heading">
      <h2 class="section-title tracker-title-gold">Muling Mode</h2>
      <p class="section-description tracker-description-gold">Pause drop, grail, and timer tracking while moving items between characters.</p>
    </div>

    <div class="settings-cluster">
      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Muling Mode</span>
          <span class="setting-hint">Pauses Drops Tracker, Total Drops, Recently Tracked Items, Holy Grail tracking, run count, run timer, and total session time while you drop items for muling.</span>
        </div>
        <Toggle checked={dropsTrackerMulingMode} onchange={setMulingMode} />
      </div>

      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Toggle Muling Mode Hotkey</span>
          <span class="setting-hint">Toggles Muling Mode from in game.</span>
        </div>
        <HotkeyInput value={settingsStore.settings.mulingModeHotkey} onchange={setMulingHotkey} />
      </div>

      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Show Muling Indicator</span>
          <span class="setting-hint">Shows the compact hotkey/M indicator while Muling Mode is active.</span>
        </div>
        <Toggle checked={mulingIndicatorOverlayEnabled} onchange={setMulingIndicatorOverlayEnabled} />
      </div>
    </div>

    <div class="settings-cluster">
      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Reset position and size</span>
          <span class="setting-hint">Restore the indicator to the default overlay placement.</span>
        </div>
        <Button variant="secondary" size="sm" onclick={resetMulingIndicatorPosition}>Reset to default</Button>
      </div>
    </div>
  </div>
  {:else}
  <IdentifiedDropsPanel />

  <div class="settings-section tracker-section recent-items-section">
    <div class="tracker-heading">
      <h2 class="section-title tracker-title-gold">Recently Tracked Items</h2>
      <p class="section-description tracker-description-gold">Last 20 items counted by the tracker. Remove an accidental duplicate to subtract it from the tracker counts.</p>
    </div>

    {#if recentTrackedItems.length === 0}
      <div class="empty-recent-items">No tracked items yet.</div>
    {:else}
      <div class="recent-items-list">
        {#each recentTrackedItems as item (item.id)}
          <div class="recent-item-row">
            <div class="recent-item-info">
              {#if item.isNewGrail}
                <span class="recent-new-grail">**New Grail Entry**</span>
              {/if}
              <span class="recent-item-name">{item.name}</span>
              <span class="recent-item-meta">
                {formatTrackedTime(item.timestampMs)} | {categorySummary(item.categories)} | {item.source ?? 'unknown-source'}
              </span>
            </div>
            <Button variant="danger" size="sm" onclick={() => settingsStore.removeRecentDropTrackerItem(item.id)}>Remove</Button>
          </div>
        {/each}
      </div>
    {/if}
  </div>
  {/if}

  {#if pendingConfirm}
    <div class="confirm-backdrop" role="presentation">
      <div class="confirm-dialog" role="dialog" aria-modal="true" aria-label="Confirm reset">
        <p>{confirmMessage}</p>
        <div class="confirm-actions">
          <Button variant="danger" size="sm" onclick={confirmReset}>Yes</Button>
          <Button variant="secondary" size="sm" onclick={() => { pendingConfirm = null; }}>No</Button>
        </div>
      </div>
    </div>
  {/if}
</section>

<style>
  .drops-tracker-tab {
    display: flex;
    flex-direction: column;
    gap: 24px;
  }

  .tracker-heading {
    text-align: center;
    margin-bottom: 16px;
  }

  .tracker-title-gold {
    color: #d6a23a;
  }

  .section-description {
    margin: -6px 0 14px;
    color: var(--text-muted);
    font-size: 13px;
  }

  .tracker-description-gold {
    color: rgba(214, 162, 58, 0.85);
  }

  .settings-cluster {
    display: flex;
    flex-direction: column;
    gap: 12px;
    margin-bottom: 12px;
  }

  .settings-panel-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
    gap: 12px;
    margin-bottom: 14px;
  }

  .settings-card {
    display: flex;
    flex-direction: column;
    gap: 12px;
    min-width: 0;
    padding: 14px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
  }

  .settings-card-heading {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 12px;
    padding-bottom: 8px;
    border-bottom: 1px solid color-mix(in srgb, var(--border-primary) 72%, transparent);
  }

  .settings-card-heading h3 {
    margin: 0;
    color: var(--text-primary);
    font-size: 14px;
  }

  .settings-card-heading span {
    color: var(--text-muted);
    font-size: 11px;
    text-align: right;
  }

  .settings-card-note {
    margin: 0;
    color: var(--text-muted);
    font-size: 12px;
    line-height: 1.45;
  }

  .setting-row.compact {
    gap: 14px;
    padding: 0;
    border: none;
    background: transparent;
  }

  .action-row {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
  }

  .switch-list {
    display: grid;
    gap: 8px;
  }

  .inline-switch {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 14px;
    padding: 8px 0;
    border-bottom: 1px solid color-mix(in srgb, var(--border-primary) 55%, transparent);
    color: var(--text-secondary);
    font-size: 13px;
  }

  .inline-switch:last-child {
    border-bottom: none;
  }

  .metric-actions {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(128px, 1fr));
    gap: 10px;
  }

  .metric-actions > div {
    display: grid;
    gap: 6px;
    min-width: 0;
    padding: 10px;
    border: 1px solid color-mix(in srgb, var(--border-primary) 72%, transparent);
    border-radius: 6px;
    background: color-mix(in srgb, var(--bg-primary) 50%, transparent);
  }

  .metric-actions strong {
    overflow: hidden;
    color: var(--accent-primary);
    font-family: var(--font-mono);
    font-size: 13px;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .metric-actions span {
    color: var(--text-muted);
    font-size: 11px;
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

  .confirm-dialog {
    width: min(420px, calc(100vw - 48px));
    padding: 20px;
    border: 1px solid var(--border-primary);
    border-radius: 10px;
    background: var(--bg-primary);
    box-shadow: 0 16px 40px rgba(0, 0, 0, 0.35);
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

  .category-card {
    margin-top: 14px;
    padding: 14px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
  }

  .category-card h3 {
    margin: 0 0 12px;
    font-size: 14px;
    color: var(--text-primary);
  }

  .category-card-heading {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin-bottom: 12px;
  }

  .category-card-heading h3 {
    margin: 0;
  }

  .category-card-actions {
    display: flex;
    gap: 8px;
  }

  .category-grid,
  .rune-grid {
    display: grid;
    overflow: hidden;
    border-top: 1px solid color-mix(in srgb, var(--border-primary) 72%, transparent);
    border-left: 1px solid color-mix(in srgb, var(--border-primary) 72%, transparent);
    border-radius: 6px;
    background: color-mix(in srgb, var(--bg-primary) 42%, transparent);
  }

  .category-grid {
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
  }

  .category-toggle,
  .rune-toggle {
    min-width: 0;
    padding: 7px 10px;
    border-right: 1px solid color-mix(in srgb, var(--accent-primary) 64%, var(--border-primary));
    border-bottom: 1px solid color-mix(in srgb, var(--border-primary) 72%, transparent);
    color: var(--text-secondary);
    font-size: 13px;
  }

  .category-toggle {
    display: grid;
    grid-template-columns: minmax(0, max-content) auto;
    align-items: center;
    justify-content: start;
    gap: 8px;
  }

  .category-toggle span,
  .rune-name {
    min-width: 0;
    overflow: hidden;
    color: var(--text-primary);
    font-weight: 650;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .rune-grid {
    grid-template-columns: repeat(auto-fit, minmax(146px, 1fr));
  }

  .rune-toggle {
    display: grid;
    grid-template-columns: minmax(34px, max-content) 28px auto;
    align-items: center;
    justify-content: start;
    gap: 8px;
  }

  .rune-count {
    color: var(--text-muted);
    font-family: var(--font-mono);
    font-size: 12px;
    text-align: right;
  }

  .material-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
    max-height: 520px;
    overflow: auto;
    border-top: 1px solid color-mix(in srgb, var(--border-primary) 72%, transparent);
    border-left: 1px solid color-mix(in srgb, var(--border-primary) 72%, transparent);
    border-radius: 6px;
    background: color-mix(in srgb, var(--bg-primary) 42%, transparent);
  }

  .material-toggle {
    display: grid;
    grid-template-columns: minmax(0, 1fr) 42px auto;
    align-items: center;
    gap: 10px;
    min-width: 0;
    padding: 8px 10px;
    border-right: 1px solid color-mix(in srgb, var(--accent-primary) 42%, var(--border-primary));
    border-bottom: 1px solid color-mix(in srgb, var(--border-primary) 72%, transparent);
    color: var(--text-secondary);
    font-size: 13px;
  }

  .material-name {
    min-width: 0;
    overflow: visible;
    color: var(--text-primary);
    font-weight: 650;
    line-height: 1.25;
    white-space: normal;
    overflow-wrap: anywhere;
  }

  .material-count {
    color: var(--text-muted);
    font-family: var(--font-mono);
    font-size: 12px;
    text-align: right;
  }

  .recent-items-section {
    margin-bottom: 8px;
  }

  .empty-recent-items {
    padding: 14px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
    color: var(--text-muted);
    font-size: 13px;
    text-align: center;
  }

  .recent-items-list {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .recent-item-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    padding: 10px 12px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
  }

  .recent-item-info {
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .recent-item-name {
    overflow: hidden;
    color: var(--text-primary);
    font-size: 13px;
    font-weight: 600;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .recent-new-grail {
    width: fit-content;
    font-size: 12px;
    font-weight: 900;
    letter-spacing: 0.02em;
    line-height: 1.1;
    color: #ffff66;
    text-shadow: 0 0 8px rgba(255, 255, 255, 0.22);
  }

  @supports ((-webkit-background-clip: text) or (background-clip: text)) {
    .recent-new-grail {
    background: linear-gradient(90deg, #ff4d4d, #ffb84d, #ffff66, #66ff99, #66ccff, #a366ff, #ff66cc, #ff4d4d);
    background-size: 300% 100%;
    -webkit-background-clip: text;
    background-clip: text;
    color: transparent;
      -webkit-text-fill-color: transparent;
    animation: grail-rainbow 1.7s linear infinite;
    }
  }

  @keyframes grail-rainbow {
    from { background-position: 0% 50%; }
    to { background-position: 300% 50%; }
  }

  .recent-item-meta {
    color: var(--text-muted);
    font-size: 12px;
  }

</style>
