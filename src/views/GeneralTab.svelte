<script lang="ts">
  import { invoke } from '@tauri-apps/api/core';
  import { Button, HotkeyInput, ThemeToggle, Toggle, UpdateButton } from '../components';
  import DropsTrackerHookPanel from '../components/DropsTrackerHookPanel.svelte';
  import { settingsStore, type HotkeyConfig } from '../stores';

  let saveExitAutomationStatus = $state('');
  let newGameAutomationOpen = $state(false);

  type HotkeyId = 'toggleWindow';
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
  ];

  const HOTKEY_GETTERS: Record<HotkeyId, () => HotkeyConfig> = {
    toggleWindow: () => settingsStore.settings.toggleWindowHotkey,
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

</script>

<section class="tab-content">
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
    <h2 class="section-title">Updates</h2>
    <div class="setting-row">
      <div class="setting-info">
        <span class="setting-label">App Updates</span>
        <span class="setting-hint">Check GitHub Releases for a newer SoE Companion build.</span>
      </div>
      <UpdateButton />
    </div>
  </div>

  <div class="settings-section">
    <h2 class="section-title">Sync</h2>
    <div class="setting-row">
      <div class="setting-info">
        <span class="setting-label">Automatic Sync on Save & Exit</span>
        <span class="setting-hint">When enabled, SoE Companion syncs drops, shared stash, Fate Cards, account stats, and character levels after you save and exit. When disabled, only the header Sync All button refreshes data.</span>
      </div>
      <Toggle
        checked={settingsStore.settings.autoSyncOnSaveExit}
        onchange={(enabled) => settingsStore.setAutoSyncOnSaveExit(enabled)}
      />
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

  <DropsTrackerHookPanel
    showInstallAction={false}
    title="SoE Hook Maintenance"
    description="Shared hook configuration for tracking, Holy Grail, Fate Cards, materials, runes, item sounds, overlays, Identified Drops, and Stash Sorter."
  />

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

</style>
