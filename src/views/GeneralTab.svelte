<script lang="ts">
  import { invoke } from '@tauri-apps/api/core';
  import { emit, listen } from '@tauri-apps/api/event';
  import { onMount } from 'svelte';
  import { Button, HotkeyInput, ThemeToggle, Toggle } from '../components';
  import { settingsStore, itemsDictionaryStore, type HotkeyConfig } from '../stores';
  import { buildHolyGrailItems } from '../lib/holy-grail';

  interface Props {
    gameStatus?: 'unknown' | 'ingame' | 'menu';
  }

  interface AccountStatsSyncResult {
    totalKills: number;
    bossKills: Record<string, number>;
    matchedText: string;
  }

  interface AccountStatsResetResult {
    stashPath: string;
    backupPath: string;
    previousTotalKills: number;
    checksum: string;
  }

  let { gameStatus = 'unknown' }: Props = $props();

  let saveExitAutomationStatus = $state('');
  let newGameAutomationOpen = $state(false);
  let syncingAccountStats = $state(false);
  let resettingAccountStats = $state(false);
  let confirmAccountStatsReset = $state(false);
  let accountStatsMessage = $state('');
  let overlayLayoutEditing = $state(false);
  let overlayLayoutMessage = $state('');

  type HotkeyId = 'toggleWindow' | 'editOverlay';
  interface HotkeyRow {
    id: HotkeyId;
    label: string;
    hint: string;
    setter: (h: HotkeyConfig) => void;
  }

  const HOTKEY_ROWS: readonly HotkeyRow[] = [
    {
      id: 'toggleWindow',
      label: 'Toggle window',
      hint: 'Show/hide main window over game',
      setter: (h) => settingsStore.setToggleWindowHotkey(h),
    },
    {
      id: 'editOverlay',
      label: 'Edit overlay layout',
      hint: 'Shows draggable native layout windows over Diablo II.',
      setter: (h) => settingsStore.setEditOverlayHotkey(h),
    },
  ];

  const HOTKEY_GETTERS: Record<HotkeyId, () => HotkeyConfig> = {
    toggleWindow: () => settingsStore.settings.toggleWindowHotkey,
    editOverlay:  () => settingsStore.settings.editOverlayHotkey,
  };
  let hotkeyValues = $derived(
    Object.fromEntries(
      (Object.keys(HOTKEY_GETTERS) as HotkeyId[]).map((id) => [id, HOTKEY_GETTERS[id]()]),
    ) as Record<HotkeyId, HotkeyConfig>,
  );

  const UNBOUND: HotkeyConfig = { keyCode: 0, modifiers: 0, display: 'None' };

  function sameChord(a: HotkeyConfig, b: HotkeyConfig): boolean {
    return a.keyCode === b.keyCode && a.modifiers === b.modifiers;
  }

  function isBound(h: HotkeyConfig): boolean {
    return h.keyCode !== 0 || h.modifiers !== 0;
  }

  function handleHotkeyChange(id: HotkeyId, hotkey: HotkeyConfig) {
    if (isBound(hotkey)) {
      for (const row of HOTKEY_ROWS) {
        if (row.id === id) continue;
        if (sameChord(hotkeyValues[row.id], hotkey)) {
          row.setter(UNBOUND);
        }
      }
    }
    HOTKEY_ROWS.find((r) => r.id === id)!.setter(hotkey);
  }

  function numberInput(event: Event): number {
    const input = event.currentTarget as HTMLInputElement;
    return Number(input.value);
  }

  function handleGameResetHotkeyChange(hotkey: HotkeyConfig) {
    if (isBound(hotkey)) {
      for (const row of HOTKEY_ROWS) {
        if (sameChord(hotkeyValues[row.id], hotkey)) {
          row.setter(UNBOUND);
        }
      }
    }
    settingsStore.setGameResetHotkey(hotkey);
  }

  function setAlwaysShowOverlays(enabled: boolean): void {
    settingsStore.set('alwaysShowOverlays', enabled);
  }

  async function toggleOverlayLayoutEditor(): Promise<void> {
    const active = !overlayLayoutEditing;
    overlayLayoutEditing = active;
    if (active) overlayLayoutMessage = '';
    await emit('overlay-edit-mode', { active });
  }

  async function saveResetAutomationConfig(): Promise<void> {
    saveExitAutomationStatus = 'Saving...';
    try {
      const path = await invoke<string>('write_save_exit_automation_config', {
        difficulty: settingsStore.settings.saveExitAutomationDifficulty,
        clickX: settingsStore.settings.saveExitAutomationClickX,
        clickY: settingsStore.settings.saveExitAutomationClickY,
        coordinateModePercent: settingsStore.settings.saveExitAutomationCoordinateModePercent,
        delayMs: settingsStore.settings.saveExitAutomationDelayMs,
        mainMenuWaitMs: settingsStore.settings.saveExitAutomationMainMenuWaitMs,
        hotkeyKeyCode: settingsStore.settings.gameResetHotkey.keyCode,
        hotkeyModifiers: settingsStore.settings.gameResetHotkey.modifiers,
        projectD2Dir: settingsStore.settings.projectD2Path,
      });
      saveExitAutomationStatus = `Saved to ${path}`;
    } catch (err) {
      saveExitAutomationStatus = `Failed to save: ${err}`;
    }
  }

  function evaluateAchievementUnlocks(): void {
    settingsStore.evaluateAchievementUnlocks({
      holyGrailFound: settingsStore.settings.holyGrailFound,
      holyGrailItems: buildHolyGrailItems(itemsDictionaryStore.dict),
      runeTrackerCounts: settingsStore.settings.runeTrackerCounts,
    });
  }

  async function syncAccountStats(): Promise<void> {
    syncingAccountStats = true;
    accountStatsMessage = 'Reading shared stash account stats...';
    try {
      const stats = settingsStore.settings.achievementStats;
      const result = await invoke<AccountStatsSyncResult>('sync_accountstats_kills', {
        currentKills: stats.totalKills,
        stashPath: settingsStore.settings.runewordPlannerStashPath,
      });
      settingsStore.updateAchievementStats({
        totalKills: result.totalKills,
        bossKills: {
          ...stats.bossKills,
          ...result.bossKills,
        },
      });
      evaluateAchievementUnlocks();
      const bossCount = Object.keys(result.bossKills ?? {}).length;
      accountStatsMessage = `Synced ${result.totalKills.toLocaleString()} Monster Kills and ${bossCount} boss stat${bossCount === 1 ? '' : 's'}.`;
    } catch (error) {
      accountStatsMessage = String(error);
    } finally {
      syncingAccountStats = false;
    }
  }

  async function resetAccountStats(): Promise<void> {
    resettingAccountStats = true;
    accountStatsMessage = 'Resetting account stats...';
    try {
      const result = await invoke<AccountStatsResetResult>('reset_accountstats_stash', {
        stashPath: settingsStore.settings.runewordPlannerStashPath,
      });
      settingsStore.resetAccountStatsAchievementProgress();
      accountStatsMessage = `Reset account stats from ${result.previousTotalKills.toLocaleString()} Monster Kills. Backup saved to app data.`;
      confirmAccountStatsReset = false;
    } catch (error) {
      accountStatsMessage = String(error);
    } finally {
      resettingAccountStats = false;
    }
  }

  onMount(() => {
    const unlisteners: Array<() => void> = [];
    listen<{ active: boolean }>('overlay-edit-mode', (event) => {
      overlayLayoutEditing = event.payload.active;
    }).then((u) => unlisteners.push(u));
    listen<{ message: string }>('overlay-layout-message', (event) => {
      overlayLayoutMessage = event.payload.message;
    }).then((u) => unlisteners.push(u));

    return () => {
      unlisteners.forEach((u) => u());
    };
  });
</script>

<section class="tab-content">
  {#if confirmAccountStatsReset}
    <div class="confirm-backdrop" role="presentation">
      <div class="confirm-dialog" role="dialog" aria-modal="true" aria-label="Reset account stats">
        <h3>Reset Account Stats?</h3>
        <p>This cannot be undone. Diablo II must be closed before resetting account stats.</p>
        <div class="confirm-actions">
          <Button variant="secondary" size="sm" onclick={() => (confirmAccountStatsReset = false)}>Cancel</Button>
          <Button variant="danger" size="sm" disabled={resettingAccountStats} onclick={resetAccountStats}>
            {resettingAccountStats ? 'Resetting...' : 'Reset Account Stats'}
          </Button>
        </div>
      </div>
    </div>
  {/if}

  <div class="settings-section">
    <h2 class="section-title">Appearance</h2>
    <div class="setting-row">
      <div class="setting-info">
        <span class="setting-label">Theme</span>
        <span class="setting-hint">Choose the visual style used by the app and overlays.</span>
      </div>
      <ThemeToggle />
    </div>
  </div>

  <div class="settings-section">
    <h2 class="section-title">Overlays</h2>
    <div class="setting-row">
      <div class="setting-info">
        <span class="setting-label">Always Show Overlays</span>
        <span class="setting-hint">Keep notification and tracker overlays visible even when Diablo II is not in focus.</span>
      </div>
      <Toggle checked={settingsStore.settings.alwaysShowOverlays} onchange={setAlwaysShowOverlays} />
    </div>

    <div class="setting-row">
      <div class="setting-info">
        <span class="setting-label">Edit Overlay Layout</span>
        <span class="setting-hint">Drag every movable overlay in native edit windows over the Diablo II window.</span>
      </div>
      <div class="overlay-edit-control">
        <Button variant={overlayLayoutEditing ? 'secondary' : 'primary'} size="sm" onclick={toggleOverlayLayoutEditor}>
          {overlayLayoutEditing ? 'Done Editing' : 'Edit Layout'}
        </Button>
        {#if overlayLayoutMessage}
          <span class="overlay-edit-status">{overlayLayoutMessage}</span>
        {/if}
      </div>
    </div>
  </div>

  <div class="settings-section">
    <h2 class="section-title">Hotkeys</h2>

    {#each HOTKEY_ROWS as row (row.id)}
      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">{row.label}</span>
          <span class="setting-hint">{@html row.hint.replace(/`([^`]+)`/g, '<code>$1</code>')}</span>
        </div>
        <HotkeyInput
          value={hotkeyValues[row.id]}
          onchange={(h) => handleHotkeyChange(row.id, h)}
        />
      </div>
    {/each}

    <div class="automation-panel">
      <button
        class="automation-summary"
        type="button"
        aria-expanded={newGameAutomationOpen}
        onclick={() => (newGameAutomationOpen = !newGameAutomationOpen)}
      >
        <div class="setting-info">
          <span class="setting-label">New Game Automation</span>
          <span class="setting-hint">Hotkey, difficulty, Single Player click coordinates, and script timing.</span>
        </div>
        <span class="automation-chevron">{newGameAutomationOpen ? 'Hide' : 'Show'}</span>
      </button>

      {#if newGameAutomationOpen}
        <div class="setting-row reset-automation-row">
          <div class="setting-info">
            <span class="setting-label">Automation Hotkey</span>
            <span class="setting-hint">Runs Save & Exit, waits for menu state, clicks Single Player, confirms the character, then selects difficulty.</span>
          </div>
          <HotkeyInput
            value={settingsStore.settings.gameResetHotkey}
            onchange={handleGameResetHotkeyChange}
          />
        </div>

        <div class="setting-row reset-automation-row">
          <div class="setting-info">
            <span class="setting-label">Script Config</span>
            <span class="setting-hint">Save these values after changing automation settings.</span>
          </div>
          <Button variant="secondary" size="sm" onclick={saveResetAutomationConfig}>Save Script Config</Button>
        </div>

        <div class="automation-grid">
          <label class="automation-field">
            <span>Difficulty</span>
            <select
              value={settingsStore.settings.saveExitAutomationDifficulty}
              onchange={(event) => settingsStore.set('saveExitAutomationDifficulty', (event.currentTarget as HTMLSelectElement).value as 'Normal' | 'Nightmare' | 'Hell')}
            >
              <option value="Normal">Normal (R)</option>
              <option value="Nightmare">Nightmare (N)</option>
              <option value="Hell">Hell (H)</option>
            </select>
          </label>

          <label class="automation-field">
            <span>Single Player X</span>
            <input
              type="number"
              min="0"
              value={settingsStore.settings.saveExitAutomationClickX}
              oninput={(event) => settingsStore.set('saveExitAutomationClickX', numberInput(event))}
            />
          </label>

          <label class="automation-field">
            <span>Single Player Y</span>
            <input
              type="number"
              min="0"
              value={settingsStore.settings.saveExitAutomationClickY}
              oninput={(event) => settingsStore.set('saveExitAutomationClickY', numberInput(event))}
            />
          </label>

          <label class="automation-field">
            <span>Step Delay (ms)</span>
            <input
              type="number"
              min="50"
              max="2000"
              value={settingsStore.settings.saveExitAutomationDelayMs}
              oninput={(event) => settingsStore.set('saveExitAutomationDelayMs', numberInput(event))}
            />
          </label>

          <label class="automation-toggle">
            <span>Use percent coordinates</span>
            <Toggle
              checked={settingsStore.settings.saveExitAutomationCoordinateModePercent}
              onchange={(enabled) => settingsStore.set('saveExitAutomationCoordinateModePercent', enabled)}
            />
          </label>
        </div>

        {#if saveExitAutomationStatus}
          <div class="automation-status">{saveExitAutomationStatus}</div>
        {/if}
      {/if}
    </div>
  </div>

  <div class="settings-section accountstats-panel">
    <div>
      <h2 class="section-title">Account Stats</h2>
      <p class="section-description">Sync SoE account stats for achievements or reset the local account stats file.</p>
    </div>

    <div class="accountstats-actions">
      <Button variant="primary" size="sm" disabled={syncingAccountStats} onclick={syncAccountStats}>
        {syncingAccountStats ? 'Syncing...' : 'Sync Account Stats'}
      </Button>
      <div class="reset-accountstats-block">
        <Button
          variant="danger"
          size="sm"
          disabled={gameStatus !== 'unknown' || resettingAccountStats}
          onclick={() => (confirmAccountStatsReset = true)}
        >
          Reset Account Stats
        </Button>
        <small>Game must be closed</small>
      </div>
    </div>

    {#if accountStatsMessage}
      <p class="accountstats-message">{accountStatsMessage}</p>
    {/if}
  </div>
</section>

<style>
  .automation-panel {
    display: flex;
    flex-direction: column;
    gap: 12px;
    margin-top: var(--space-3);
    padding-top: var(--space-3);
    border-top: 1px solid var(--border-primary);
  }

  .overlay-edit-control {
    display: flex;
    flex-direction: column;
    align-items: flex-end;
    gap: 6px;
    min-width: 210px;
  }

  .overlay-edit-status {
    max-width: 280px;
    color: var(--accent-primary);
    font-size: 12px;
    line-height: 1.3;
    text-align: right;
  }

  .automation-summary {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-3);
    width: 100%;
    padding: 12px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
    color: var(--text-primary);
    cursor: pointer;
    text-align: left;
  }

  .automation-summary:hover {
    border-color: var(--accent-primary);
    background: var(--bg-elevated);
  }

  .automation-chevron {
    color: var(--accent-primary);
    font-family: var(--font-mono);
    font-size: 12px;
    text-transform: uppercase;
  }

  .automation-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 10px 14px;
    padding: 12px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
  }

  .automation-field,
  .automation-toggle {
    display: flex;
    flex-direction: column;
    gap: 6px;
    color: var(--text-secondary);
    font-size: 13px;
  }

  .automation-field select,
  .automation-field input {
    width: 100%;
    padding: 8px 10px;
    border: 1px solid var(--border-primary);
    border-radius: 6px;
    background: var(--bg-primary);
    color: var(--text-primary);
  }

  .automation-toggle {
    flex-direction: row;
    align-items: center;
    justify-content: space-between;
  }

  .automation-status {
    color: var(--text-muted);
    font-family: var(--font-mono);
    font-size: 12px;
  }

  .accountstats-panel {
    display: grid;
    grid-template-columns: minmax(0, 1fr) auto;
    align-items: center;
    gap: var(--space-3);
  }

  .accountstats-actions {
    display: grid;
    gap: var(--space-2);
    min-width: 170px;
  }

  .reset-accountstats-block small,
  .accountstats-message {
    color: var(--text-secondary);
    font-size: 12px;
  }

  .reset-accountstats-block {
    display: grid;
    gap: 4px;
    justify-items: center;
  }

  .accountstats-message {
    grid-column: 1 / -1;
    margin: 0;
  }

  .confirm-backdrop {
    position: fixed;
    inset: 0;
    z-index: 1000;
    display: grid;
    place-items: center;
    background: rgba(0, 0, 0, 0.62);
  }

  .confirm-dialog {
    width: min(420px, calc(100vw - 36px));
    display: grid;
    gap: var(--space-3);
    padding: var(--space-5);
    border: 1px solid var(--accent-primary);
    background: var(--bg-secondary);
    box-shadow: var(--shadow-lg);
  }

  .confirm-dialog h3,
  .confirm-dialog p {
    margin: 0;
  }

  .confirm-dialog p {
    color: var(--text-secondary);
  }

  .confirm-actions {
    display: flex;
    justify-content: flex-end;
    gap: var(--space-2);
  }
</style>
