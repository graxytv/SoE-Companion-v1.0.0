<script lang="ts">
  import { onMount } from 'svelte';
  import { invoke } from '@tauri-apps/api/core';
  import { Button, HotkeyInput, SubTabs, Toggle } from '../components';
  import { settingsStore, type HotkeyConfig } from '../stores';
  import {
    STASH_SORTER_CATEGORIES,
    STASH_SORTER_DEFAULT_INVENTORY_ROWS,
    STASH_SORTER_DEFAULT_SPEED,
    STASH_SORTER_GRID_COLUMNS,
    STASH_SORTER_GRID_ROWS,
    STASH_SORTER_MAX_SPEED,
    STASH_SORTER_MIN_SPEED,
    STASH_SORTER_SHARED_TAB_COUNT,
    defaultStashSorterBlacklist,
    defaultStashSorterProtectedCells,
    stashSorterCellKey,
    stashSorterCategoryLabel,
  } from '../lib/stash-sorter';

  type StashSorterSubTab = 'settings' | 'rules' | 'inventory-cells' | 'log';

  const UNBOUND: HotkeyConfig = { keyCode: 0, modifiers: 0, display: 'None' };
  const subTabs: Array<{ id: StashSorterSubTab; label: string }> = [
    { id: 'settings', label: 'Settings' },
    { id: 'rules', label: 'Rules' },
    { id: 'inventory-cells', label: 'Configure Inventory Cells' },
    { id: 'log', label: 'Log' },
  ];
  const SHARED_TABS = Array.from({ length: STASH_SORTER_SHARED_TAB_COUNT }, (_, index) => index + 1);
  const GRID_COLUMNS = Array.from({ length: STASH_SORTER_GRID_COLUMNS }, (_, index) => index);
  const GRID_ROWS = Array.from({ length: STASH_SORTER_GRID_ROWS }, (_, index) => index);
  const REQUIRED_SHARED_STASH_HOTKEYS = [
    'Shared Tab 1 = Num Pad 1',
    'Shared Tab 2 = Num Pad 2',
    'Shared Tab 3 = Num Pad 3',
    'Shared Tab 4 = Num Pad 4',
    'Shared Tab 5 = Num Pad 5',
    'Shared Tab 6 = Num Pad 6',
    'Shared Tab 7 = Num Pad 7',
    'Shared Tab 8 = Num Pad 8',
    'Shared Tab 9 = Num Pad 9',
  ];

  let activeSubTab = $state<StashSorterSubTab>('settings');
  let blacklistDraft = $state(settingsStore.settings.stashSorterBlacklist.join('\n'));
  let configStatus = $state('');
  let cellStatus = $state('');
  let logText = $state('');
  let logStatus = $state('No stash sorter log yet.');
  let draftMatchValue = $state<string>(STASH_SORTER_CATEGORIES[0].key);
  let draftTargetTab = $state(1);
  let draftError = $state('');

  let rules = $derived(settingsStore.settings.stashSorterRules);
  let lastRun = $derived(settingsStore.settings.stashSorterLastRun);
  let protectedCells = $derived(settingsStore.settings.stashSorterProtectedCells);
  let protectedCellKeys = $derived(new Set(protectedCells.map((cell) => stashSorterCellKey(cell.x, cell.y))));
  let protectedCount = $derived(protectedCells.length);
  let allowedCount = $derived((STASH_SORTER_GRID_COLUMNS * STASH_SORTER_GRID_ROWS) - protectedCount);

  function isBound(hotkey: HotkeyConfig): boolean {
    return hotkey.keyCode !== 0 || hotkey.modifiers !== 0;
  }

  function sameHotkey(left: HotkeyConfig, right: HotkeyConfig): boolean {
    return isBound(left) && isBound(right) && left.keyCode === right.keyCode && left.modifiers === right.modifiers;
  }

  async function saveConfigNow(): Promise<void> {
    configStatus = 'Saving config...';
    try {
      const ok = await settingsStore.writeStashSorterConfig();
      configStatus = ok
        ? 'Config saved to C:\\SoECompanion\\config\\stash_sorter.ini'
        : 'Could not write config. Check app permissions and try again.';
    } catch (error) {
      configStatus = `Could not write config: ${error}`;
    }
  }

  async function refreshLog(): Promise<void> {
    try {
      logText = await invoke<string>('read_stash_sorter_log_tail', { maxBytes: 8192 });
      logStatus = logText.trim() ? '' : 'No stash sorter log yet.';
    } catch (error) {
      logStatus = `Could not read stash sorter log: ${error}`;
    }
  }

  function handleEnable(enabled: boolean): void {
    settingsStore.setStashSorterEnabled(enabled);
  }

  function handleAutomaticMaterialsTab(enabled: boolean): void {
    settingsStore.setStashSorterAutomaticMaterialsTabEnabled(enabled);
  }

  function handleHotkey(hotkey: HotkeyConfig): void {
    if (sameHotkey(hotkey, settingsStore.settings.stashSorterStopHotkey)) {
      settingsStore.setStashSorterStopHotkey(UNBOUND);
      configStatus = 'Stop hotkey was cleared because it matched the sort hotkey.';
    }
    settingsStore.setStashSorterHotkey(hotkey);
  }

  function handleStopHotkey(hotkey: HotkeyConfig): void {
    if (sameHotkey(hotkey, settingsStore.settings.stashSorterHotkey)) {
      settingsStore.setStashSorterHotkey(UNBOUND);
      configStatus = 'Sort hotkey was cleared because it matched the stop hotkey.';
    }
    settingsStore.setStashSorterStopHotkey(hotkey);
  }

  function handleSpeed(value: number): void {
    settingsStore.setStashSorterSpeed(value);
  }

  function addRule(): void {
    if (!draftMatchValue) {
      draftError = 'Choose a category before saving the rule.';
      return;
    }
    settingsStore.addStashSorterRule({
      matchType: 'category',
      matchValue: draftMatchValue,
      targetTab: draftTargetTab,
      enabled: true,
    });
    draftError = '';
  }

  function applyBlacklist(): void {
    settingsStore.setStashSorterBlacklist(
      blacklistDraft
        .split(/\r?\n/)
        .map((entry) => entry.trim())
        .filter(Boolean),
    );
  }

  function resetBlacklist(): void {
    blacklistDraft = defaultStashSorterBlacklist().join('\n');
    applyBlacklist();
  }

  function isProtectedCell(x: number, y: number): boolean {
    return protectedCellKeys.has(stashSorterCellKey(x, y));
  }

  function toggleProtectedCell(x: number, y: number): void {
    settingsStore.toggleStashSorterProtectedCell(x, y);
    cellStatus = isProtectedCell(x, y)
      ? `Cell ${x + 1},${y + 1} will be allowed.`
      : `Cell ${x + 1},${y + 1} will be protected.`;
  }

  function protectDefaultCharmRows(): void {
    const cells = [];
    for (let y = STASH_SORTER_DEFAULT_INVENTORY_ROWS; y < STASH_SORTER_GRID_ROWS; y += 1) {
      for (let x = 0; x < STASH_SORTER_GRID_COLUMNS; x += 1) {
        cells.push({ x, y });
      }
    }
    settingsStore.setStashSorterProtectedCells(cells);
    cellStatus = 'Lower 4 rows protected.';
  }

  function allowAllCells(): void {
    settingsStore.setStashSorterProtectedCells(defaultStashSorterProtectedCells());
    cellStatus = 'All inventory cells are allowed.';
  }

  function protectAllCells(): void {
    settingsStore.setStashSorterProtectedCells(
      GRID_ROWS.flatMap((y) => GRID_COLUMNS.map((x) => ({ x, y }))),
    );
    cellStatus = 'All inventory cells protected.';
  }

  function formatLastRun(): string {
    if (!lastRun.timestampMs) return lastRun.message;
    const when = new Date(lastRun.timestampMs).toLocaleString();
    return `${when} | ${lastRun.message}`;
  }

  function setupWarnings(): string[] {
    const warnings: string[] = [];
    if (!settingsStore.settings.stashSorterEnabled) warnings.push('Enable Stash Sorter.');
    if (!isBound(settingsStore.settings.stashSorterHotkey)) warnings.push('Choose a sort hotkey.');
    if (rules.length === 0 && !settingsStore.settings.stashSorterAutomaticMaterialsTabEnabled) {
      warnings.push('Save at least one rule.');
    }
    if (!logText.trim()) warnings.push('Start Diablo II with the updated SoE Hook installed to see hook activity here.');
    return warnings;
  }

  onMount(() => {
    void refreshLog();
    const timer = window.setInterval(() => {
      void refreshLog();
    }, 5000);
    return () => window.clearInterval(timer);
  });
</script>

<section class="tab-content stash-sorter-tab">
  <div class="stash-sorter-warning">
    <div class="warning-copy">
      <strong>Warning: You must configure your shared stash hotkeys!</strong>
      <span>Set Shared Tab 1-9 to Num Pad 1-9 in Diablo II or the sorter cannot reliably switch to the requested stash tab.</span>
    </div>
    <div class="required-hotkeys">
      {#each REQUIRED_SHARED_STASH_HOTKEYS as binding}
        <span>{binding}</span>
      {/each}
    </div>
  </div>

  <SubTabs tabs={subTabs} bind:activeTab={activeSubTab} ariaLabel="Stash Sorter sections" />

  {#if activeSubTab === 'settings'}
    <div class="settings-section">
      <h2 class="section-title">Stash Sorter</h2>
      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Enable Stash Sorter</span>
          <span class="setting-hint">When enabled, the installed drops tracker hook can sort matching inventory items into shared stash tabs 1-9.</span>
        </div>
        <Toggle checked={settingsStore.settings.stashSorterEnabled} onchange={handleEnable} />
      </div>

      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Automatic Materials Tab</span>
          <span class="setting-hint">Known material-tab items use Shift + Right Click and the game's built-in material affinity without switching to a numbered shared tab first.</span>
        </div>
        <Toggle
          checked={settingsStore.settings.stashSorterAutomaticMaterialsTabEnabled}
          onchange={handleAutomaticMaterialsTab}
        />
      </div>

      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Sort Hotkey</span>
          <span class="setting-hint">Press in game with the shared stash open. The sorter uses Num Pad 1-9 to pick the tab, then Shift + Right Click to move items.</span>
        </div>
        <div class="hotkey-actions">
          <HotkeyInput value={settingsStore.settings.stashSorterHotkey} onchange={handleHotkey} />
          {#if isBound(settingsStore.settings.stashSorterHotkey)}
            <Button variant="secondary" size="sm" onclick={() => handleHotkey(UNBOUND)}>Clear</Button>
          {/if}
        </div>
      </div>

      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Stop Sorter Hotkey</span>
          <span class="setting-hint">Press while sorting to stop after the current movement step. Keep this easy to reach in case a rule catches the wrong item.</span>
        </div>
        <div class="hotkey-actions">
          <HotkeyInput value={settingsStore.settings.stashSorterStopHotkey} onchange={handleStopHotkey} />
          {#if isBound(settingsStore.settings.stashSorterStopHotkey)}
            <Button variant="secondary" size="sm" onclick={() => handleStopHotkey(UNBOUND)}>Clear</Button>
          {/if}
        </div>
      </div>

      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Sorting Speed</span>
          <span class="setting-hint">Higher is faster. Lower is safer for slower PCs or full stash tabs.</span>
        </div>
        <div class="speed-control">
          <input
            type="range"
            min={STASH_SORTER_MIN_SPEED}
            max={STASH_SORTER_MAX_SPEED}
            step="10"
            value={settingsStore.settings.stashSorterSpeed}
            oninput={(event) => handleSpeed(Number((event.currentTarget as HTMLInputElement).value))}
          />
          <span>{settingsStore.settings.stashSorterSpeed}%</span>
          <Button variant="secondary" size="sm" onclick={() => handleSpeed(STASH_SORTER_DEFAULT_SPEED)}>Reset</Button>
        </div>
      </div>

      <div class="status-line">
        <span>{formatLastRun()}</span>
        <Button variant="secondary" size="sm" onclick={saveConfigNow}>Write Config</Button>
      </div>
      {#if configStatus}
        <p class="config-status">{configStatus}</p>
      {/if}
      <p class="section-note">
        If the log does not change after pressing the hotkey, update or reinstall the SoE Hook and restart Diablo II so the game is using the sorter-capable DLL.
      </p>
      <div class:ready={setupWarnings().length === 0} class="setup-checklist">
        {#if setupWarnings().length === 0}
          Ready once Diablo II is focused and the shared stash is open.
        {:else}
          {#each setupWarnings() as warning}
            <span>{warning}</span>
          {/each}
        {/if}
      </div>
    </div>
  {:else if activeSubTab === 'rules'}
    <div class="settings-section rules-panel">
      <div class="section-heading-row compact-heading">
        <div>
          <h2 class="section-title">Rules</h2>
          <p class="section-note">Rules run top to bottom. First enabled match wins.</p>
        </div>
        <span class="rule-count">{rules.length} saved</span>
      </div>

      <div class="rule-builder compact-rule-builder">
        <label>
          <span>Category</span>
          <select
            value={draftMatchValue}
            onchange={(event) => { draftMatchValue = (event.currentTarget as HTMLSelectElement).value; draftError = ''; }}
          >
            {#each STASH_SORTER_CATEGORIES as category}
              <option value={category.key}>{category.label}</option>
            {/each}
          </select>
        </label>

        <label>
          <span>Target</span>
          <select
            value={draftTargetTab}
            onchange={(event) => { draftTargetTab = Number((event.currentTarget as HTMLSelectElement).value); }}
          >
            {#each SHARED_TABS as tab}
              <option value={tab}>Shared Stash {tab}</option>
            {/each}
          </select>
        </label>

        <Button variant="primary" size="sm" onclick={addRule}>Add Rule</Button>
      </div>

      {#if draftError}
        <p class="config-status error">{draftError}</p>
      {/if}

      {#if rules.length === 0}
        <div class="empty-state compact-empty">
          No saved rules yet.
        </div>
      {:else}
        <div class="compact-rules">
          {#each rules as rule, index (rule.id)}
            <div class:disabled={!rule.enabled} class="compact-rule-row">
              <span class="rule-number">{index + 1}</span>
              <Toggle
                checked={rule.enabled}
                onchange={(enabled) => settingsStore.updateStashSorterRule(rule.id, { enabled })}
              />
              <select
                class="rule-category-select"
                aria-label={`Rule ${index + 1} category`}
                value={rule.matchValue}
                onchange={(event) => settingsStore.updateStashSorterRule(rule.id, { matchType: 'category', matchValue: (event.currentTarget as HTMLSelectElement).value })}
              >
                {#each STASH_SORTER_CATEGORIES as category}
                  <option value={category.key}>{category.label}</option>
                {/each}
              </select>
              <select
                class="rule-tab-select"
                aria-label={`Rule ${index + 1} target stash tab`}
                value={rule.targetTab}
                onchange={(event) => settingsStore.updateStashSorterRule(rule.id, { targetTab: Number((event.currentTarget as HTMLSelectElement).value) })}
              >
                {#each SHARED_TABS as tab}
                  <option value={tab}>Tab {tab}</option>
                {/each}
              </select>
              <div class="compact-rule-actions">
                <Button variant="secondary" size="sm" disabled={index === 0} onclick={() => settingsStore.moveStashSorterRule(rule.id, -1)}>Up</Button>
                <Button variant="secondary" size="sm" disabled={index === rules.length - 1} onclick={() => settingsStore.moveStashSorterRule(rule.id, 1)}>Down</Button>
                <Button variant="danger" size="sm" onclick={() => settingsStore.removeStashSorterRule(rule.id)}>Remove</Button>
              </div>
            </div>
          {/each}
        </div>
      {/if}
    </div>

    <details class="settings-section compact-disclosure">
      <summary>
        <span>Blacklist</span>
        <small>{settingsStore.settings.stashSorterBlacklist.length} protected entries</small>
      </summary>
      <p class="section-note">
        One item name or item code per line. The hook also hard-blocks cube and tome codes.
      </p>
      <textarea
        class="blacklist-box"
        bind:value={blacklistDraft}
        spellcheck="false"
        onblur={applyBlacklist}
      ></textarea>
      <div class="blacklist-actions">
        <Button variant="secondary" size="sm" onclick={applyBlacklist}>Save Blacklist</Button>
        <Button variant="secondary" size="sm" onclick={resetBlacklist}>Reset Defaults</Button>
      </div>
    </details>

  {:else if activeSubTab === 'inventory-cells'}
    <div class="settings-section">
      <div class="section-heading-row">
        <div>
          <h2 class="section-title">Configure Inventory Cells</h2>
          <p class="section-note">
            Click cells that should be protected from sorting. Protected cells are safe and will never be clicked by the sorter.
          </p>
        </div>
        <span class="cell-summary">{allowedCount} allowed / {protectedCount} protected</span>
      </div>

      <div class="cell-actions">
        <Button variant="secondary" size="sm" onclick={allowAllCells}>Allow All Cells</Button>
        <Button variant="secondary" size="sm" onclick={protectDefaultCharmRows}>Protect Lower 4 Rows</Button>
        <Button variant="secondary" size="sm" onclick={protectAllCells}>Protect All Cells</Button>
      </div>

      {#if cellStatus}
        <p class="config-status">{cellStatus}</p>
      {/if}

      <div class="protected-grid-wrap">
        <div class="protected-grid" style={`--grid-columns: ${STASH_SORTER_GRID_COLUMNS};`}>
          {#each GRID_ROWS as y}
            {#each GRID_COLUMNS as x}
              <button
                type="button"
                class:protected={isProtectedCell(x, y)}
                class:charm-row={y >= STASH_SORTER_DEFAULT_INVENTORY_ROWS}
                onclick={() => toggleProtectedCell(x, y)}
                title={isProtectedCell(x, y) ? `Protected cell ${x + 1},${y + 1}` : `Allowed cell ${x + 1},${y + 1}`}
              >
                {x + 1},{y + 1}
              </button>
            {/each}
          {/each}
        </div>
        <div class="protected-legend">
          <span><i class="legend-swatch allowed"></i>Allowed</span>
          <span><i class="legend-swatch charm"></i>Charm row</span>
          <span><i class="legend-swatch protected"></i>Protected</span>
        </div>
      </div>
    </div>
  {:else if activeSubTab === 'log'}
    <div class="settings-section">
      <div class="section-heading-row">
        <h2 class="section-title">Sorter Log</h2>
        <Button variant="secondary" size="sm" onclick={refreshLog}>Refresh</Button>
      </div>
      <pre class="log-box">{logText.trim() || logStatus}</pre>
    </div>
  {/if}
</section>

<style>
  .stash-sorter-tab {
    display: grid;
    align-items: start;
    gap: var(--space-4);
  }

  :global(.stash-sorter-tab .sub-tabs-list) {
    align-self: start;
    align-items: center;
    width: fit-content;
    max-width: 100%;
  }

  :global(.stash-sorter-tab .sub-tab) {
    min-height: 0;
    white-space: nowrap;
  }

  .stash-sorter-warning {
    display: grid;
    grid-template-columns: 1fr;
    gap: var(--space-2) var(--space-3);
    align-items: center;
    padding: var(--space-3);
    border: 1px solid color-mix(in srgb, var(--status-warning-text) 70%, var(--border-primary));
    border-radius: var(--radius-sm);
    background: rgba(255, 176, 32, 0.1);
    color: var(--text-primary);
  }

  .warning-copy {
    display: grid;
    gap: var(--space-1);
  }

  .warning-copy strong {
    color: var(--status-warning-text);
    font-size: var(--text-base);
  }

  .warning-copy span {
    color: var(--text-secondary);
    font-size: var(--text-sm);
  }

  .required-hotkeys {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: 4px;
  }

  .required-hotkeys span {
    padding: 4px 6px;
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-sm);
    background: var(--bg-secondary);
    color: var(--accent-gold);
    font-family: var(--font-mono);
    font-size: var(--text-xs);
  }

  .section-heading-row,
  .hotkey-actions,
  .speed-control,
  .blacklist-actions,
  .status-line {
    display: flex;
    align-items: center;
    gap: var(--space-2);
  }

  .section-heading-row,
  .status-line {
    justify-content: space-between;
  }

  .section-heading-row {
    align-items: flex-start;
  }

  .status-line {
    padding: var(--space-3);
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-sm);
    background: var(--bg-secondary);
    color: var(--text-secondary);
    font-size: var(--text-sm);
  }

  .speed-control {
    min-width: min(360px, 100%);
  }

  .speed-control input {
    min-width: 180px;
    padding: 0;
  }

  .speed-control span {
    min-width: 46px;
    color: var(--accent-gold);
    font-family: var(--font-mono);
    font-size: var(--text-sm);
    text-align: right;
  }

  .config-status,
  .section-note {
    margin: var(--space-2) 0 0;
    color: var(--text-secondary);
    font-size: var(--text-sm);
  }

  .config-status.error {
    color: var(--status-error-text);
  }

  .log-box {
    margin: 0;
    min-height: 220px;
    max-height: 420px;
    overflow: auto;
    padding: var(--space-3);
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-sm);
    background: var(--bg-secondary);
    color: var(--text-secondary);
    font-family: var(--font-mono);
    font-size: var(--text-xs);
    line-height: 1.45;
    white-space: pre-wrap;
  }

  .setup-checklist {
    display: grid;
    gap: var(--space-1);
    margin-top: var(--space-3);
    padding: var(--space-3);
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-sm);
    background: var(--bg-secondary);
    color: var(--status-warning-text);
    font-size: var(--text-sm);
  }

  .setup-checklist.ready {
    color: var(--status-success-text);
  }

  .cell-actions {
    display: flex;
    flex-wrap: wrap;
    justify-content: flex-end;
    gap: var(--space-2);
    margin-top: var(--space-3);
  }

  .protected-grid-wrap {
    display: grid;
    gap: var(--space-2);
    margin-top: var(--space-3);
  }

  .protected-grid {
    display: grid;
    grid-template-columns: repeat(var(--grid-columns), 62px);
    grid-auto-rows: 44px;
    gap: 3px;
    width: max-content;
    max-width: 100%;
    padding: var(--space-2);
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-sm);
    background: var(--bg-secondary);
  }

  .protected-grid button {
    min-width: 0;
    min-height: 0;
    border: 1px solid #f5b83b;
    border-radius: 2px;
    background: #050505;
    color: #f8d36a;
    font-family: var(--font-mono);
    font-size: 11px;
    font-weight: 700;
    cursor: pointer;
  }

  .protected-grid button.charm-row {
    border-style: dashed;
    background:
      repeating-linear-gradient(135deg, rgba(255, 255, 255, 0.12) 0 4px, transparent 4px 9px),
      #050505;
  }

  .protected-grid button.protected {
    border: 2px solid #60b7ff;
    background: #f6f7fb;
    color: #06121a;
    box-shadow: inset 0 0 0 2px #06121a;
  }

  .protected-grid button:hover {
    border-color: var(--accent-gold);
  }

  .protected-legend {
    display: flex;
    flex-wrap: wrap;
    gap: var(--space-3);
    color: var(--text-secondary);
    font-size: var(--text-xs);
  }

  .protected-legend span,
  .cell-summary {
    display: inline-flex;
    align-items: center;
    gap: var(--space-1);
  }

  .cell-summary {
    flex-shrink: 0;
    color: var(--text-secondary);
    font-size: var(--text-xs);
    text-transform: uppercase;
  }

  .legend-swatch {
    width: 12px;
    height: 12px;
    border: 1px solid #f5b83b;
    border-radius: 2px;
    background: #050505;
  }

  .legend-swatch.charm {
    border-style: dashed;
    background:
      repeating-linear-gradient(135deg, rgba(255, 255, 255, 0.32) 0 2px, transparent 2px 5px),
      #050505;
  }

  .legend-swatch.protected {
    border: 2px solid #60b7ff;
    background: #f6f7fb;
  }

  .empty-state {
    padding: var(--space-4);
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-sm);
    color: var(--text-secondary);
    background: var(--bg-secondary);
  }

  .rules-panel {
    gap: var(--space-3);
  }

  .compact-heading {
    align-items: center;
  }

  .compact-heading .section-note {
    margin-top: var(--space-1);
  }

  .rule-builder {
    display: grid;
    grid-template-columns: minmax(220px, 1fr) minmax(160px, 220px) auto;
    gap: var(--space-2);
    align-items: end;
  }

  label {
    display: grid;
    gap: var(--space-1);
  }

  label > span,
  .rule-count {
    color: var(--text-secondary);
    font-size: var(--text-xs);
    text-transform: uppercase;
    letter-spacing: 0;
  }

  .compact-rules {
    display: grid;
    gap: 6px;
  }

  .compact-rule-row {
    display: grid;
    grid-template-columns: 28px 48px minmax(180px, 1fr) minmax(96px, 130px) auto;
    gap: 6px;
    align-items: center;
    min-height: 42px;
    padding: 6px 8px;
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-sm);
    background: var(--bg-secondary);
  }

  .compact-rule-row.disabled {
    opacity: 0.58;
  }

  .rule-number {
    color: var(--accent-gold);
    font-family: var(--font-mono);
    font-size: var(--text-xs);
    text-align: center;
  }

  .compact-rule-actions {
    display: flex;
    gap: 4px;
    justify-content: flex-end;
  }

  .compact-empty {
    padding: var(--space-3);
    font-size: var(--text-sm);
  }

  .compact-disclosure {
    padding: 0;
  }

  .compact-disclosure summary {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-3);
    padding: var(--space-3);
    color: var(--text-primary);
    cursor: pointer;
    list-style: none;
  }

  .compact-disclosure summary::-webkit-details-marker {
    display: none;
  }

  .compact-disclosure summary::before {
    content: '+';
    color: var(--accent-gold);
    font-family: var(--font-mono);
  }

  .compact-disclosure[open] summary::before {
    content: '-';
  }

  .compact-disclosure summary span {
    flex: 1;
    font-weight: 700;
  }

  .compact-disclosure summary small {
    color: var(--text-secondary);
    font-size: var(--text-xs);
    text-transform: uppercase;
  }

  .compact-disclosure > :not(summary) {
    margin-left: var(--space-3);
    margin-right: var(--space-3);
  }

  .compact-disclosure > :last-child {
    margin-bottom: var(--space-3);
  }

  select,
  input,
  textarea {
    width: 100%;
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-sm);
    background: var(--bg-primary);
    color: var(--text-primary);
    font-family: var(--font-mono);
    font-size: var(--text-sm);
  }

  select,
  input {
    height: 36px;
    padding: 0 var(--space-2);
  }

  .compact-rule-row select {
    height: 30px;
    font-size: var(--text-xs);
  }

  .blacklist-box {
    min-height: 120px;
    padding: var(--space-3);
    resize: vertical;
    line-height: 1.5;
  }

  .blacklist-actions {
    margin-top: var(--space-2);
    justify-content: flex-end;
  }

  @media (max-width: 900px) {
    .rule-builder {
      grid-template-columns: 1fr;
    }

    .compact-rule-row {
      grid-template-columns: 28px 48px 1fr;
    }

    .rule-tab-select {
      grid-column: 3;
    }

    .compact-rule-actions {
      grid-column: 1 / -1;
      justify-content: flex-end;
    }

    .protected-grid {
      grid-template-columns: repeat(var(--grid-columns), 48px);
      grid-auto-rows: 36px;
    }

    .section-heading-row {
      flex-wrap: wrap;
    }
  }
</style>
