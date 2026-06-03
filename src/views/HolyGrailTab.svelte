<script lang="ts">
  import { onMount } from 'svelte';
  import { invoke } from "@tauri-apps/api/core";
  import { Toggle, Button, SubTabs } from '../components';
  import { itemsDictionaryStore, settingsStore } from '../stores';
  import { playSound } from '../lib/sound-player';
  import {
    HOLY_GRAIL_CATEGORIES,
    buildHolyGrailItems,
    holyGrailCategoryLabel,
    holyGrailCategoryProgress,
    holyGrailProgress,
    mostRecentHolyGrailFind,
    findHolyGrailItem,
    type HolyGrailCategoryKey,
  } from '../lib/holy-grail';
  import {
    SOE_RUNEWORDS,
    RUNE_NAMES,
    canMakeRuneword,
    emptyRuneInventory,
    missingRunesForRuneword,
    normalizeRuneInventory,
    type RuneInventory,
  } from '../lib/runewords';
  import { detailsForGrailItem, type GrailItemDetails } from '../lib/grail-item-details';

  let { activeSubTab = $bindable('overview'), grailDropCount = 0 }: {
    activeSubTab?: string;
    grailDropCount?: number;
  } = $props();

  const subTabs = [
    { id: 'overview', label: 'Overview' },
    { id: 'runeword-planner', label: 'What RW Can I Make?' },
    { id: 'import', label: 'Install Auto Grail Tracker' },
    { id: 'overlay', label: 'Overlay Settings' },
    { id: 'notifications', label: 'Notifications' },
    { id: 'backup', label: 'Backup' },
  ];

  interface DropHookStatus {
    projectD2Dir: string;
    dllPath: string;
    originalDllPath: string;
    iniPath: string;
    logPath: string;
    projectD2DirExists: boolean;
    dllExists: boolean;
    originalDllExists: boolean;
    iniExists: boolean;
    logExists: boolean;
    logSize: number;
    installed: boolean;
    currentDllHash: string | null;
    bundledDllHash: string;
    message: string;
  }

  let orderedSubTabs = $derived(applyTabOrder(subTabs, settingsStore.settings.holyGrailSubTabOrder));

  let search = $state('');
  let categoryFilter = $state<'all' | HolyGrailCategoryKey>('all');
  let foundFilter = $state<'all' | 'found' | 'missing'>('all');
  let qualitySort = $state<'none' | 'desc' | 'asc'>('none');
  let pendingReset = $state(false);
  let pendingRestore = $state(false);
  let backupBusy = $state(false);
  let backupMessage = $state('');
  let backupStatus = $state<{ backupExists: boolean; backupPath: string; foundCount: number; exportedAt: string | null } | null>(null);

  // Grail log import
  let importBusy = $state(false);
  let importMessage = $state('');
  let importCount = $state(0);
  let hookBusy = $state(false);
  let hookStatus = $state<DropHookStatus | null>(null);
  let projectD2PathDraft = $state(settingsStore.settings.projectD2Path ?? '');
  let runeSyncBusy = $state(false);
  let runeSyncMessage = $state('Runeword planner reads rune counts from the selected pd2_shared.stash Materials tab.');
  let lastRuneSyncAt = $state<string | null>(null);
  let runeInventory = $state<RuneInventory>(emptyRuneInventory());
  let runewordSearch = $state('');
  let runewordMode = $state<'makeable' | 'missing' | 'all'>('makeable');
  let selectedDetails = $state<GrailItemDetails | null>(null);
  let detectedStashPaths = $state<string[]>([]);
  let stashPathDraft = $state('');

  interface RuneStashSyncResult {
    counts: Partial<Record<string, number>>;
    scanned_files: string[];
    message: string;
  }

  let found = $derived(settingsStore.settings.holyGrailFound);
  let grailItems = $derived(buildHolyGrailItems(itemsDictionaryStore.dict));
  let progress = $derived(holyGrailProgress(grailItems, found));
  let latest = $derived(mostRecentHolyGrailFind(found));
  let overlayCategories = $derived(settingsStore.settings.holyGrailOverlayCategories);
  let soundSlots = $derived(settingsStore.settings.sounds);
  let grailSoundChoices = $derived(
    soundSlots
      .map((slot, i) => ({ index: i + 1, slot }))
      .filter(({ slot }) => slot.source.kind !== 'empty'),
  );

  let filteredItems = $derived(
    grailItems.filter((item) => {
      if (categoryFilter !== 'all' && item.category !== categoryFilter) return false;
      const isFound = !!found[item.key];
      if (foundFilter === 'found' && !isFound) return false;
      if (foundFilter === 'missing' && isFound) return false;
      const q = search.trim().toLowerCase();
      if (q && !item.name.toLowerCase().includes(q)) return false;
      return true;
    }).toSorted((a, b) => {
      if (qualitySort === 'none') return 0;
      const aq = a.qualityLevel ?? -1;
      const bq = b.qualityLevel ?? -1;
      const diff = qualitySort === 'desc' ? bq - aq : aq - bq;
      return diff || a.name.localeCompare(b.name);
    }),
  );

  let makeableRunewords = $derived(
    SOE_RUNEWORDS.filter((runeword) => canMakeRuneword(runeword, runeInventory)),
  );

  let filteredRunewords = $derived(
    SOE_RUNEWORDS.filter((runeword) => {
      const makeable = canMakeRuneword(runeword, runeInventory);
      const missing = !found[runeword.key];
      if (runewordMode === 'makeable' && !makeable) return false;
      if (runewordMode === 'missing' && (!makeable || !missing)) return false;
      const q = runewordSearch.trim().toLowerCase();
      if (!q) return true;
      return (
        runeword.name.toLowerCase().includes(q) ||
        runeword.runes.join(' ').toLowerCase().includes(q) ||
        runeword.bases.join(' ').toLowerCase().includes(q)
      );
    }),
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

  function formatDate(value: string | undefined): string {
    if (!value) return '-';
    return new Date(value).toLocaleString([], {
      year: 'numeric', month: 'short', day: 'numeric',
      hour: '2-digit', minute: '2-digit',
    });
  }

  function setGrailItemFound(item: { key: string; name: string; category: HolyGrailCategoryKey }, checked: boolean): void {
    settingsStore.setHolyGrailFound(item.key, item.name, item.category, checked);
  }

  function onGrailCheckboxChange(item: { key: string; name: string; category: HolyGrailCategoryKey }, event: Event): void {
    event.stopPropagation();
    setGrailItemFound(item, (event.currentTarget as HTMLInputElement).checked);
  }

  function handleGrailSoundChange(event: Event): void {
    const value = (event.currentTarget as HTMLSelectElement).value;
    settingsStore.setHolyGrailNewItemSoundSlot(value === '' ? null : Number(value));
  }

  function testGrailSound(): void {
    const slot = settingsStore.settings.holyGrailNewItemSoundSlot;
    if (slot != null) {
      void playSound(slot, settingsStore.settings.soundVolume * settingsStore.settings.holyGrailNewItemSoundVolume);
    }
  }

  function handleGrailSoundVolumeChange(event: Event): void {
    const value = Number((event.currentTarget as HTMLInputElement).value);
    settingsStore.setHolyGrailNewItemSoundVolume(value / 100);
  }

  function soundLabel(index: number): string {
    return soundSlots[index - 1]?.label || `Sound ${index}`;
  }

  async function syncRunesFromSharedStash(): Promise<void> {
    runeSyncBusy = true;
    try {
      const savedPath = settingsStore.settings.runewordPlannerStashPath;
      const result = await invoke<RuneStashSyncResult>('sync_shared_stash_runes', {
        stashPath: savedPath,
      });
      const scanned = normalizeRuneInventory(result.counts);
      runeInventory = scanned;
      lastRuneSyncAt = new Date().toISOString();
      runeSyncMessage = result.message;
    } catch (error) {
      runeInventory = emptyRuneInventory();
      runeSyncMessage = `Shared-stash sync failed: ${error}`;
    } finally {
      runeSyncBusy = false;
    }
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
      runeSyncMessage = `Could not auto-detect shared stash files: ${error}`;
    }
  }

  function selectDetectedStashPath(event: Event): void {
    const value = (event.currentTarget as HTMLSelectElement).value;
    stashPathDraft = value;
    settingsStore.setRunewordPlannerStashPath(value || null);
    if (value) void syncRunesFromSharedStash();
  }

  function saveStashPath(): void {
    const path = stashPathDraft.trim();
    settingsStore.setRunewordPlannerStashPath(path || null);
    runeSyncMessage = path
      ? 'Shared stash path saved. Press Sync Now to refresh rune counts.'
      : 'Shared stash path cleared. Press Detect Stash to auto-detect again.';
  }

  function markRunewordFound(runeword: { key: string; name: string }): void {
    settingsStore.setHolyGrailFound(runeword.key, runeword.name, 'runewords', true);
  }

  function openItemDetails(item: Parameters<typeof detailsForGrailItem>[0]): void {
    selectedDetails = detailsForGrailItem(item);
  }

  function toggleQualitySort(): void {
    qualitySort = qualitySort === 'desc' ? 'asc' : 'desc';
  }

  function qualitySortLabel(): string {
    if (qualitySort === 'desc') return 'Quality Level ↓';
    if (qualitySort === 'asc') return 'Quality Level ↑';
    return 'Quality Level';
  }

  function onRunewordCheckboxChange(runeword: { key: string; name: string }, event: Event): void {
    event.stopPropagation();
    settingsStore.setHolyGrailFound(runeword.key, runeword.name, 'runewords', (event.currentTarget as HTMLInputElement).checked);
  }

  function confirmReset(): void {
    settingsStore.resetHolyGrail();
    pendingReset = false;
    importCount = 0;
    importMessage = '';
    backupMessage = 'Holy Grail reset.';
  }

  function formatBackupDate(value: string | null | undefined): string {
    if (!value) return '-';
    const numeric = Number(value);
    const date = Number.isFinite(numeric) ? new Date(numeric) : new Date(value);
    return Number.isNaN(date.getTime()) ? value : date.toLocaleString();
  }

  async function refreshBackupStatus(): Promise<void> {
    try {
      backupStatus = await settingsStore.getHolyGrailBackupStatus();
    } catch (error) {
      backupMessage = `Could not read backup status: ${error}`;
    }
  }

  async function backupNow(): Promise<void> {
    backupBusy = true;
    backupMessage = '';
    try {
      const status = await settingsStore.backupHolyGrail();
      backupStatus = status;
      backupMessage = `Backup saved with ${status?.foundCount ?? 0} found items.`;
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
      const count = await settingsStore.restoreHolyGrailBackup();
      backupMessage = `Backup restored. ${count} items are now marked found.`;
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
      await settingsStore.openHolyGrailBackupFolder();
    } catch (error) {
      backupMessage = `Could not open backup folder: ${error}`;
    }
  }

  async function refreshHookStatus(): Promise<void> {
    try {
      hookStatus = await invoke<DropHookStatus>('get_drop_hook_status_for_path', { projectD2Dir: settingsStore.settings.projectD2Path });
      if (!settingsStore.settings.projectD2Path && hookStatus.projectD2DirExists) {
        settingsStore.setProjectD2Path(hookStatus.projectD2Dir);
        projectD2PathDraft = hookStatus.projectD2Dir;
      }
    } catch (error) {
      importMessage = `Could not check Auto Grail Tracker status: ${error}`;
    }
  }

  async function autoDetectProjectD2Folder(): Promise<void> {
    hookBusy = true;
    importMessage = 'Searching common ProjectD2 folders...';
    try {
      const paths = await invoke<string[]>('detect_project_d2_dirs');
      const first = paths[0] ?? '';
      if (!first) {
        importMessage = 'No ProjectD2 folder found in common install locations. Paste your ProjectD2 folder path below.';
        return;
      }
      projectD2PathDraft = first;
      settingsStore.setProjectD2Path(first);
      await refreshHookStatus();
    } catch (error) {
      importMessage = `Auto-detect failed: ${error}`;
    } finally {
      hookBusy = false;
    }
  }

  async function chooseProjectD2Folder(): Promise<void> {
    const selected = window.prompt(
      'Enter the full path to your ProjectD2 folder:',
      projectD2PathDraft || settingsStore.settings.projectD2Path || hookStatus?.projectD2Dir || 'C:\\Program Files (x86)\\Diablo II\\ProjectD2',
    );
    if (selected == null) return;
    projectD2PathDraft = selected.trim();
    settingsStore.setProjectD2Path(projectD2PathDraft);
    await refreshHookStatus();
  }

  async function saveProjectD2Folder(): Promise<void> {
    settingsStore.setProjectD2Path(projectD2PathDraft);
    await refreshHookStatus();
  }

  async function installHook(): Promise<void> {
    hookBusy = true;
    importMessage = '';
    try {
      hookStatus = await invoke<DropHookStatus>('install_drop_hook_for_path', { projectD2Dir: settingsStore.settings.projectD2Path });
      if (!settingsStore.settings.projectD2Path && hookStatus.projectD2DirExists) {
        settingsStore.setProjectD2Path(hookStatus.projectD2Dir);
        projectD2PathDraft = hookStatus.projectD2Dir;
      }
      importMessage = hookStatus.message;
    } catch (error) {
      importMessage = `Install failed: ${error}`;
      await refreshHookStatus();
    } finally {
      hookBusy = false;
    }
  }

  // Import from C:\grail_drops.log written by ijl11.dll
  async function importFromLog(): Promise<void> {
    importBusy = true;
    importMessage = '';
    try {
      const drops = await invoke<Array<{ itemName: string; quality: string }>>('read_grail_log');
      let matched = 0;
      let skipped = 0;
      const importedKeys = new Set(Object.keys(found));
      for (const drop of drops) {
        const grailItem = findHolyGrailItem(drop.itemName);
        if (grailItem && !importedKeys.has(grailItem.key)) {
          settingsStore.setHolyGrailFound(grailItem.key, grailItem.name, grailItem.category, true);
          importedKeys.add(grailItem.key);
          matched++;
        } else if (!grailItem) {
          skipped++;
        }
      }
      importCount = matched;
      importMessage = `Imported ${matched} new grail item${matched !== 1 ? 's' : ''} from log.${skipped > 0 ? ` (${skipped} entries not recognized - rares/magic/etc.)` : ''}`;
    } catch (error) {
      importMessage = `Import failed: ${error}`;
    } finally {
      importBusy = false;
    }
  }

  async function clearLog(): Promise<void> {
    try {
      await invoke('clear_grail_log');
      importMessage = 'Log cleared. New session drops will be tracked fresh.';
      importCount = 0;
    } catch (error) {
      importMessage = `Could not clear log: ${error}`;
    }
  }

  $effect(() => {
    void refreshBackupStatus();
    void refreshHookStatus();
  });

  onMount(() => {
    stashPathDraft = settingsStore.settings.runewordPlannerStashPath ?? '';
    void detectSharedStashPaths().then(() => syncRunesFromSharedStash());
    const timer = window.setInterval(() => {
      void syncRunesFromSharedStash();
    }, 30 * 60 * 1000);
    return () => window.clearInterval(timer);
  });
</script>

<section class="tab-content holy-grail-tab">
  <div class="grail-hero">
    <div>
      <h2 class="section-title grail-title">Holy Grail</h2>
      <p class="section-description grail-description">
        Tracks unique and set items found in Sanctuary of Exile. Items auto-check via the drop log when ijl11.dll is active.
      </p>
    </div>
    <div class="progress-card">
      <span class="progress-percent">{progress.percent.toFixed(1)}%</span>
      <span class="progress-count">{progress.found} / {progress.total} Found</span>
      <span class="progress-latest">Latest: {latest?.name ?? '-'}</span>
    </div>
  </div>

  <SubTabs
    tabs={orderedSubTabs}
    bind:activeTab={activeSubTab}
    ariaLabel="Holy Grail sections"
    onReorder={(ids) => settingsStore.setHolyGrailSubTabOrder(ids)}
  />

  {#if activeSubTab === 'runeword-planner'}
  <div class="settings-section">
    <div class="toolbar">
      <div>
        <h2 class="section-title">What RW Can I Make?</h2>
        <p class="section-description">
          Syncs rune counts from your ProjectD2 shared stash about every 30 minutes, or when you press Sync Now. Makeable missing runewords are filtered against your Grail checklist.
        </p>
      </div>
      <div class="runeword-actions">
        <Button variant="secondary" size="sm" onclick={detectSharedStashPaths}>
          Detect Stash
        </Button>
        <Button variant="primary" size="sm" disabled={runeSyncBusy} onclick={syncRunesFromSharedStash}>
          {runeSyncBusy ? 'Syncing...' : 'Sync Now'}
        </Button>
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

    <p class="materials-note">
      This only reads rune counts from the <strong>Materials</strong> tab inside <code>pd2_shared.stash</code>. Runes in character inventory, cube, personal stash, or normal shared-bank item slots are not counted.
    </p>

    <div class="runeword-sync-card">
      <div>
        <strong>{makeableRunewords.length}</strong>
        <span>makeable runeword{makeableRunewords.length !== 1 ? 's' : ''}</span>
      </div>
      <div>
        <strong>{SOE_RUNEWORDS.length}</strong>
        <span>total wiki runewords</span>
      </div>
      <div>
        <strong>{lastRuneSyncAt ? formatDate(lastRuneSyncAt) : 'Never'}</strong>
        <span>last sync</span>
      </div>
    </div>

    <p class="import-message">{runeSyncMessage}</p>

    <div class="rune-inventory-grid">
      {#each RUNE_NAMES as rune}
        <div class="rune-inventory-cell">
          <span>{rune}</span>
          <strong>{runeInventory[rune]}</strong>
        </div>
      {/each}
    </div>

    <div class="filters runeword-filters">
      <input class="filter-input" bind:value={runewordSearch} placeholder="Search runewords, runes, or bases..." />
      <select class="filter-select" bind:value={runewordMode}>
        <option value="makeable">Makeable</option>
        <option value="missing">Makeable + Missing Grail</option>
        <option value="all">All Runewords</option>
      </select>
    </div>

    <div class="runeword-table">
      <div class="runeword-row runeword-header-row">
        <span>Found</span>
        <span>Runeword</span>
        <span>Runes</span>
        <span>Bases</span>
        <span>Can Make</span>
      </div>
      {#each filteredRunewords as runeword (runeword.key)}
        {@const isFound = !!found[runeword.key]}
        {@const canMake = canMakeRuneword(runeword, runeInventory)}
        <div class="runeword-row">
          <span><input type="checkbox" checked={isFound} onchange={(event) => onRunewordCheckboxChange(runeword, event)} /></span>
          <button type="button" class="runeword-name item-name-button" onclick={() => openItemDetails({ key: runeword.key, name: runeword.name, category: 'runewords', runes: [...runeword.runes], bases: [...runeword.bases], requiredLevel: runeword.requiredLevel })}>
            <strong>{runeword.name}</strong>
            {#if runeword.requiredLevel}<small>Req {runeword.requiredLevel}</small>{/if}
          </button>
          <span class="runeword-runes">{runeword.runes.join(' · ')}</span>
          <span class="runeword-bases">{runeword.bases.join(', ')}</span>
          <span class:makeable={canMake} class="runeword-status">
            {#if canMake}
              Ready
              {#if !isFound}
                <Button variant="secondary" size="sm" onclick={() => markRunewordFound(runeword)}>Mark Made</Button>
              {/if}
            {:else}
              Missing {missingRunesForRuneword(runeword, runeInventory).join(', ')}
            {/if}
          </span>
        </div>
      {/each}
    </div>
  </div>
  {/if}

  {#if activeSubTab === 'import'}
  <div class="settings-section">
    <h2 class="section-title">Install Auto Grail Tracker</h2>
    <p class="section-description">
      Automatic grail tracking requires the SoE drop hook. It installs as <code>ijl11.dll</code> in the ProjectD2 folder, keeps the original as <code>ijl11_orig.dll</code>, and writes unique/set drops to <code>C:\grail_drops.log</code>.
    </p>

    <div class="hook-card">
      <div class="hook-status">
        <span class="import-status-dot" class:active={hookStatus?.installed}></span>
        <div>
          <strong>{hookStatus?.installed ? 'Installed' : 'Not Installed'}</strong>
          <span>{hookStatus?.message ?? 'Checking Auto Grail Tracker status...'}</span>
        </div>
      </div>
      <div class="hook-actions">
        <Button variant="primary" size="sm" disabled={hookBusy || hookStatus?.installed} onclick={installHook}>
          {hookBusy ? 'Installing...' : 'Install Auto Grail Tracker'}
        </Button>
        <Button variant="secondary" size="sm" disabled={hookBusy} onclick={refreshHookStatus}>
          Refresh Status
        </Button>
      </div>
    </div>

    <div class="project-d2-picker">
      <label class="hook-path-field">
        <span>ProjectD2 Folder</span>
        <input
          value={projectD2PathDraft}
          placeholder="C:\Program Files (x86)\Diablo II\ProjectD2"
          oninput={(event) => (projectD2PathDraft = (event.currentTarget as HTMLInputElement).value)}
        />
      </label>
      <div class="hook-path-actions">
        <Button variant="secondary" size="sm" disabled={hookBusy} onclick={autoDetectProjectD2Folder}>Auto Detect</Button>
        <Button variant="secondary" size="sm" disabled={hookBusy} onclick={chooseProjectD2Folder}>Select Folder</Button>
        <Button variant="primary" size="sm" disabled={hookBusy} onclick={saveProjectD2Folder}>Use Folder</Button>
      </div>
    </div>

    <div class="hook-detail-grid">
      <div><span>ProjectD2 Folder</span><strong>{hookStatus?.projectD2DirExists ? 'Found' : 'Missing'}</strong></div>
      <div><span>Original DLL Backup</span><strong>{hookStatus?.originalDllExists ? 'Found' : 'Missing'}</strong></div>
      <div><span>Config File</span><strong>{hookStatus?.iniExists ? 'Found' : 'Missing'}</strong></div>
      <div><span>Drop Log</span><strong>{hookStatus?.logExists ? `${hookStatus.logSize} bytes` : 'No drops yet'}</strong></div>
    </div>

    <div class="import-card">
      <div class="import-info">
        <div class="import-status">
          <span class="import-status-dot" class:active={grailDropCount > 0}></span>
          <span>
            {#if grailDropCount > 0}
              Live - {grailDropCount} drop{grailDropCount !== 1 ? 's' : ''} this session
            {:else}
              Waiting for live grail drops
            {/if}
          </span>
        </div>
        {#if importCount > 0}
          <span class="import-new-count">{importCount} new grail item{importCount !== 1 ? 's' : ''} found this session</span>
        {/if}
      </div>
      <div class="import-actions">
        <Button variant="primary" size="sm" disabled={importBusy} onclick={importFromLog}>
          Import Log Now
        </Button>
        <Button variant="ghost" size="sm" disabled={importBusy} onclick={clearLog}>
          Clear Log
        </Button>
      </div>
    </div>

    {#if importMessage}
      <p class="import-message">{importMessage}</p>
    {/if}

    <div class="import-note">
      <strong>Note:</strong> Close Diablo II before installing. If Windows blocks the install under Program Files, run SoE Companion as administrator and try again.
      <br />
      <span class="hook-path">{hookStatus?.dllPath ?? 'C:\\Program Files (x86)\\Diablo II\\ProjectD2\\ijl11.dll'}</span>
    </div>
  </div>
  {/if}

  {#if activeSubTab === 'backup'}
  <div class="settings-section">
    <h2 class="section-title">Grail Backup</h2>
    <p class="section-description">
      Your grail data is saved in app settings and mirrored to a backup file. On startup, SoE Companion merges the backup if the settings copy is missing entries.
    </p>
    <div class="backup-card">
      <div class="backup-info">
        <span><strong>Backup file:</strong> {backupStatus?.backupExists ? 'Found' : 'Not created yet'}</span>
        <span><strong>Backed up items:</strong> {backupStatus?.foundCount ?? 0}</span>
        <span><strong>Last backup:</strong> {formatBackupDate(backupStatus?.exportedAt)}</span>
        <span class="backup-path">{backupStatus?.backupPath ?? ''}</span>
      </div>
      <div class="backup-actions">
        <Button variant="secondary" size="sm" disabled={backupBusy} onclick={backupNow}>Backup Now</Button>
        <Button variant="secondary" size="sm" disabled={backupBusy || !backupStatus?.backupExists} onclick={() => { pendingRestore = true; }}>Restore/Merge</Button>
        <Button variant="ghost" size="sm" onclick={openBackupFolder}>Open Folder</Button>
      </div>
    </div>
    {#if backupMessage}
      <p class="backup-message">{backupMessage}</p>
    {/if}
  </div>
  {/if}

  {#if activeSubTab === 'notifications'}
  <div class="settings-section">
    <h2 class="section-title">New Grail Drop Notification</h2>
    <p class="section-description">
      Play a sound and show a notification when a first-time Holy Grail item drops.
    </p>

    <div class="setting-row">
      <div class="setting-info">
        <span class="setting-label">Show New Item notification</span>
        <span class="setting-hint">Only fires for items newly added to the Grail checklist.</span>
      </div>
      <Toggle checked={settingsStore.settings.holyGrailNewItemNotificationEnabled} onchange={(enabled) => settingsStore.setHolyGrailNewItemNotificationEnabled(enabled)} />
    </div>

    <div class="setting-row">
      <div class="setting-info">
        <span class="setting-label">New Grail Item Sound</span>
        <span class="setting-hint">Select a sound slot to play on a brand-new Grail find.</span>
      </div>
      <div class="grail-sound-control">
        <select
          class="filter-select grail-sound-select"
          value={settingsStore.settings.holyGrailNewItemSoundSlot ?? ''}
          onchange={handleGrailSoundChange}
        >
          <option value="">None</option>
          {#each grailSoundChoices as { index, slot } (index)}
            <option value={index}>{slot.label || `Sound ${index}`}</option>
          {/each}
        </select>
        <Button variant="secondary" size="sm" disabled={settingsStore.settings.holyGrailNewItemSoundSlot == null} onclick={testGrailSound}>
          Test
        </Button>
      </div>
    </div>

    <div class="setting-row">
      <div class="setting-info">
        <span class="setting-label">New Grail Sound Volume</span>
      </div>
      <div class="grail-volume-control">
        <input
          type="range" min="0" max="100"
          value={Math.round(settingsStore.settings.holyGrailNewItemSoundVolume * 100)}
          oninput={handleGrailSoundVolumeChange}
        />
        <span>{Math.round(settingsStore.settings.holyGrailNewItemSoundVolume * 100)}%</span>
      </div>
    </div>
  </div>
  {/if}

  {#if activeSubTab === 'overlay'}
  <div class="settings-section">
    <h2 class="section-title">Grail Progress Overlay</h2>

    <div class="setting-row">
      <div class="setting-info">
        <span class="setting-label">Show Grail Progress overlay</span>
      </div>
      <Toggle checked={settingsStore.settings.holyGrailOverlayEnabled} onchange={(enabled) => settingsStore.setHolyGrailOverlayEnabled(enabled)} />
    </div>

    <div class="setting-row">
      <div class="setting-info">
        <span class="setting-label">Show Total Progress</span>
      </div>
      <Toggle checked={settingsStore.settings.holyGrailOverlayShowTotal} onchange={(enabled) => settingsStore.setHolyGrailOverlayShowTotal(enabled)} />
    </div>

    <div class="setting-row">
      <div class="setting-info">
        <span class="setting-label">Show Latest Find</span>
      </div>
      <Toggle checked={settingsStore.settings.holyGrailOverlayShowLatest} onchange={(enabled) => settingsStore.setHolyGrailOverlayShowLatest(enabled)} />
    </div>

    <div class="category-card">
      <h3>Category Rows on Overlay</h3>
      <div class="category-grid">
        {#each HOLY_GRAIL_CATEGORIES as category}
          <label class="category-toggle">
            <span>{category.label}</span>
            <Toggle checked={overlayCategories[category.key]} onchange={(enabled) => settingsStore.setHolyGrailOverlayCategory(category.key, enabled)} />
          </label>
        {/each}
      </div>
    </div>
  </div>
  {/if}

  {#if activeSubTab === 'overview'}
  <div class="settings-section">
    <div class="toolbar">
      <div>
        <h2 class="section-title">Checklist</h2>
      </div>
      <Button variant="danger" size="sm" onclick={() => { pendingReset = true; }}>Reset Holy Grail</Button>
    </div>

    <div class="filters">
      <input class="filter-input" bind:value={search} placeholder="Search items..." />
      <select class="filter-select" bind:value={categoryFilter}>
        <option value="all">All Categories</option>
        {#each HOLY_GRAIL_CATEGORIES as category}
          <option value={category.key}>{category.label}</option>
        {/each}
      </select>
      <select class="filter-select" bind:value={foundFilter}>
        <option value="all">All Items</option>
        <option value="found">Found</option>
        <option value="missing">Missing</option>
      </select>
    </div>

    <div class="category-summary-grid">
      {#each HOLY_GRAIL_CATEGORIES as category}
        {@const categoryProgress = holyGrailCategoryProgress(grailItems, found, category.key)}
        <div class="category-summary-card">
          <span>{category.label}</span>
          <strong>{categoryProgress.found} / {categoryProgress.total}</strong>
        </div>
      {/each}
    </div>

    {#if grailItems.length === 0}
      <div class="empty-state">No Holy Grail items loaded.</div>
    {:else}
      <div class="grail-table">
        <div class="grail-row grail-header-row">
          <span>Found</span>
          <span>Item</span>
          <button type="button" class="header-sort-button" onclick={toggleQualitySort}>{qualitySortLabel()}</button>
          <span>Category</span>
          <span>First Found</span>
        </div>
        {#each filteredItems as item (item.key)}
          {@const entry = found[item.key]}
          <div class="grail-row">
            <span><input type="checkbox" checked={!!entry} onchange={(event) => onGrailCheckboxChange(item, event)} /></span>
            <button type="button" class="item-name item-name-button category-{item.category}" onclick={() => openItemDetails(item)}>
              <strong>{item.name}</strong>
              {#if item.category === 'runewords'}
                <small>{item.runes?.join(' · ')} · {item.bases?.join(', ')}</small>
              {/if}
            </button>
            <span class="quality-level-cell">{item.qualityLevel != null ? item.qualityLevel : '-'}</span>
            <span>{holyGrailCategoryLabel(item.category)}</span>
            <span>{formatDate(entry?.firstFoundAt)}</span>
          </div>
        {/each}
      </div>
    {/if}
  </div>
  {/if}

  {#if pendingRestore}
    <div class="confirm-backdrop" role="presentation">
      <div class="confirm-dialog" role="dialog" aria-modal="true">
        <p>Restore and merge the backup into your current checklist? Existing found items will be kept.</p>
        <div class="confirm-actions">
          <Button variant="primary" size="sm" disabled={backupBusy} onclick={confirmRestore}>Yes</Button>
          <Button variant="secondary" size="sm" disabled={backupBusy} onclick={() => { pendingRestore = false; }}>No</Button>
        </div>
      </div>
    </div>
  {/if}

  {#if pendingReset}
    <div class="confirm-backdrop" role="presentation">
      <div class="confirm-dialog" role="dialog" aria-modal="true">
        <p>Reset the Holy Grail? This cannot be undone.</p>
        <div class="confirm-actions">
          <Button variant="danger" size="sm" onclick={confirmReset}>Yes</Button>
          <Button variant="secondary" size="sm" onclick={() => { pendingReset = false; }}>No</Button>
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
  .holy-grail-tab { display: flex; flex-direction: column; gap: var(--space-4); }
  .grail-hero {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-4);
    padding: var(--space-5);
    border: 1px solid var(--border-primary);
    background: var(--bg-secondary);
    box-shadow: var(--shadow-sm);
  }
  .grail-title { color: #d6a23a; }
  .grail-description { color: rgba(214, 162, 58, 0.85); }
  .grail-hero .grail-description { margin: 0; }
  .section-description { margin: -6px 0 14px; color: var(--text-muted); font-size: 13px; }
  code { font-family: var(--font-mono); font-size: 12px; background: var(--bg-tertiary); padding: 1px 4px; border-radius: 3px; }

  .progress-card {
    min-width: 180px;
    display: grid;
    gap: 8px;
    padding: 18px;
    border: 1px solid color-mix(in srgb, var(--accent-primary) 58%, transparent);
    border-radius: 8px;
    background: color-mix(in srgb, var(--bg-primary) 82%, transparent);
    text-align: right;
  }
  .progress-percent {
    color: var(--accent-primary);
    font-family: var(--font-display);
    font-size: 32px;
  }
  .progress-count, .progress-latest { color: var(--text-primary); font-size: 12px; }

  .hook-card { display: flex; align-items: center; justify-content: space-between; gap: 16px; padding: 14px; margin-bottom: 12px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); }
  .hook-status { display: flex; align-items: center; gap: 10px; min-width: 0; color: var(--text-secondary); font-size: 13px; }
  .hook-status div { display: flex; min-width: 0; flex-direction: column; gap: 3px; }
  .hook-status strong { color: var(--text-primary); }
  .hook-status span:last-child { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .hook-actions { display: flex; flex-wrap: wrap; justify-content: flex-end; gap: 8px; flex-shrink: 0; }
  .project-d2-picker { display: grid; grid-template-columns: minmax(0, 1fr) auto; align-items: end; gap: 12px; padding: 12px; margin-bottom: 12px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); }
  .hook-path-field { display: grid; gap: 6px; color: var(--text-secondary); font-size: 13px; }
  .hook-path-field input { width: 100%; padding: 8px 10px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-primary); color: var(--text-primary); }
  .hook-path-actions { display: flex; flex-wrap: wrap; justify-content: flex-end; gap: 8px; }
  .hook-detail-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); gap: 8px; margin-bottom: 12px; }
  .hook-detail-grid div { display: flex; justify-content: space-between; gap: 8px; padding: 8px 10px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); color: var(--text-muted); font-size: 12px; }
  .hook-detail-grid strong { color: var(--text-primary); }
  .hook-path { color: var(--text-secondary); font-family: var(--font-mono); }

  .import-card { display: flex; align-items: center; justify-content: space-between; gap: 16px; padding: 14px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); }
  .import-info { display: flex; flex-direction: column; gap: 6px; }
  .import-status { display: flex; align-items: center; gap: 8px; font-size: 13px; color: var(--text-secondary); }
  .import-status-dot { width: 8px; height: 8px; border-radius: 50%; background: var(--text-muted); flex-shrink: 0; }
  .import-status-dot.active { background: #4aaa3e; box-shadow: 0 0 6px #4aaa3e; }
  .import-new-count { font-size: 13px; color: #d6a23a; font-weight: 600; }
  .import-actions { display: flex; gap: 8px; flex-shrink: 0; }
  .import-message { margin: 10px 0 0; color: var(--text-secondary); font-size: 13px; }
  .import-note { margin-top: 14px; padding: 12px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); color: var(--text-muted); font-size: 12px; line-height: 1.5; }

  .backup-card { display: flex; align-items: flex-start; justify-content: space-between; gap: 16px; padding: 14px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); }
  .backup-info { display: flex; flex-direction: column; gap: 5px; min-width: 0; color: var(--text-secondary); font-size: 13px; }
  .backup-path { max-width: 620px; overflow: hidden; color: var(--text-muted); text-overflow: ellipsis; white-space: nowrap; }
  .backup-actions { display: flex; flex-wrap: wrap; justify-content: flex-end; gap: 8px; }
  .backup-message { margin: 10px 0 0; color: var(--text-secondary); font-size: 13px; }

  .runeword-actions { display: flex; flex-wrap: wrap; justify-content: flex-end; gap: 8px; }
  .stash-path-card { display: grid; grid-template-columns: minmax(220px, 0.7fr) minmax(300px, 1fr) auto; align-items: end; gap: 10px; margin-bottom: 10px; padding: 12px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); }
  .stash-path-card label { display: flex; min-width: 0; flex-direction: column; gap: 6px; color: var(--text-secondary); font-size: 12px; }
  .stash-path-input { min-width: 0; }
  .materials-note { margin: 0 0 12px; padding: 10px 12px; border: 1px solid color-mix(in srgb, var(--accent-primary) 38%, var(--border-primary)); border-radius: 8px; background: color-mix(in srgb, var(--accent-primary) 8%, var(--bg-secondary)); color: var(--text-secondary); font-size: 12px; line-height: 1.45; }
  .materials-note strong { color: var(--text-primary); }
  .runeword-sync-card { display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); gap: 8px; padding: 12px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); }
  .runeword-sync-card div { display: flex; flex-direction: column; gap: 3px; }
  .runeword-sync-card strong { color: var(--accent-primary); font-family: var(--font-mono); font-size: 18px; }
  .runeword-sync-card span { color: var(--text-muted); font-size: 12px; }
  .rune-inventory-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(80px, 1fr)); overflow: hidden; border-top: 1px solid var(--border-primary); border-left: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); }
  .rune-inventory-cell { display: flex; align-items: center; justify-content: space-between; gap: 8px; padding: 7px 9px; border-right: 1px solid color-mix(in srgb, var(--accent-primary) 55%, var(--border-primary)); border-bottom: 1px solid var(--border-primary); color: var(--text-secondary); font-size: 12px; }
  .rune-inventory-cell span { color: var(--accent-primary); font-weight: 700; }
  .rune-inventory-cell strong { color: var(--text-primary); font-family: var(--font-mono); }
  .runeword-filters { grid-template-columns: minmax(220px, 1fr) 220px; margin-bottom: 0; }
  .runeword-table { overflow: hidden; border: 1px solid var(--border-primary); border-radius: 8px; }
  .runeword-row { display: grid; grid-template-columns: 58px minmax(160px, 0.9fr) minmax(160px, 0.75fr) minmax(220px, 1.2fr) minmax(150px, 0.7fr); align-items: center; gap: 10px; padding: 9px 12px; border-bottom: 1px solid var(--border-primary); background: var(--bg-secondary); color: var(--text-secondary); font-size: 13px; }
  .runeword-row:last-child { border-bottom: none; }
  .runeword-header-row { background: var(--bg-tertiary, var(--bg-secondary)); color: var(--text-primary); font-size: 12px; font-weight: 700; text-transform: uppercase; }
  .runeword-name { display: flex; min-width: 0; flex-direction: column; gap: 2px; color: #f7f2df; }
  .runeword-name strong { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .runeword-name small { color: var(--text-muted); font-size: 11px; }
  .runeword-runes { color: var(--accent-primary); font-weight: 700; }
  .runeword-bases { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .runeword-status { display: flex; align-items: center; justify-content: space-between; gap: 8px; color: var(--text-muted); }
  .runeword-status.makeable { color: #55e589; font-weight: 700; }

  .grail-sound-control { display: flex; align-items: center; justify-content: flex-end; gap: 8px; }
  .grail-sound-select { min-width: 190px; }
  .category-card { margin-top: 14px; padding: 14px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); }
  .category-card h3 { margin: 0 0 12px; font-size: 14px; color: var(--text-primary); }
  .category-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    overflow: hidden;
    border-top: 1px solid color-mix(in srgb, var(--border-primary) 72%, transparent);
    border-left: 1px solid color-mix(in srgb, var(--border-primary) 72%, transparent);
    border-radius: 6px;
    background: color-mix(in srgb, var(--bg-primary) 42%, transparent);
  }
  .category-toggle {
    display: grid;
    grid-template-columns: minmax(0, max-content) auto;
    align-items: center;
    justify-content: start;
    gap: 8px;
    min-width: 0;
    padding: 7px 10px;
    border-right: 1px solid color-mix(in srgb, var(--accent-primary) 64%, var(--border-primary));
    border-bottom: 1px solid color-mix(in srgb, var(--border-primary) 72%, transparent);
    color: var(--text-secondary);
    font-size: 13px;
  }
  .category-toggle span {
    min-width: 0;
    overflow: hidden;
    color: var(--text-primary);
    font-weight: 650;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .toolbar { display: flex; align-items: flex-start; justify-content: space-between; gap: 16px; }
  .filters { display: grid; grid-template-columns: minmax(180px, 1fr) 180px 150px; gap: 10px; margin-bottom: 14px; }
  .filter-input, .filter-select { width: 100%; padding: 8px 10px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); color: var(--text-primary); font-size: 13px; }
  .category-summary-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 8px; margin-bottom: 14px; }
  .category-summary-card { display: flex; justify-content: space-between; gap: 8px; padding: 8px 10px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); color: var(--text-secondary); font-size: 13px; }
  .category-summary-card strong { color: var(--text-primary); }
  .empty-state { padding: 18px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); color: var(--text-muted); text-align: center; }

  .grail-table { overflow: hidden; border: 1px solid var(--border-primary); border-radius: 8px; }
  .grail-row { display: grid; grid-template-columns: 60px minmax(180px, 1fr) 118px 140px 190px; align-items: center; gap: 10px; padding: 9px 12px; border-bottom: 1px solid var(--border-primary); background: var(--bg-secondary); color: var(--text-secondary); font-size: 13px; }
  .grail-row:last-child { border-bottom: none; }
  .grail-header-row { background: var(--bg-tertiary, var(--bg-secondary)); color: var(--text-primary); font-size: 12px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.04em; }
  .header-sort-button { appearance: none; justify-self: start; border: 0; background: transparent; color: inherit; cursor: pointer; font: inherit; font-weight: 800; padding: 0; text-align: left; text-transform: inherit; letter-spacing: inherit; }
  .header-sort-button:hover,
  .header-sort-button:focus-visible { color: var(--accent-primary); text-decoration: underline; text-underline-offset: 3px; outline: none; }
  .quality-level-cell { color: var(--accent-primary); font-family: var(--font-mono); font-weight: 800; }
  .item-name { display: flex; min-width: 0; flex-direction: column; gap: 2px; color: var(--text-primary); font-weight: 600; }
  .item-name-button { appearance: none; align-items: flex-start; border: 0; background: transparent; cursor: pointer; padding: 0; text-align: left; font: inherit; }
  .item-name-button:hover strong,
  .item-name-button:focus-visible strong { text-decoration: underline; text-underline-offset: 3px; }
  .item-name-button:focus-visible { outline: 1px solid var(--accent-primary); outline-offset: 3px; border-radius: 4px; }
  .item-name strong { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .item-name small { color: var(--text-muted); font-size: 11px; font-weight: 500; }
  .item-name.category-su,
  .item-name.category-ssu { color: #ff9f2f; }
  .item-name.category-sets { color: #d6a23a; }
  .item-name.category-runes { color: #ffdf6f; }
  .item-name.category-runewords { color: #f7f2df; }

  .confirm-backdrop { position: fixed; inset: 0; z-index: 50; display: flex; align-items: center; justify-content: center; background: rgba(0, 0, 0, 0.45); }
  .confirm-dialog { width: min(420px, calc(100vw - 48px)); padding: 20px; border: 1px solid var(--border-primary); border-radius: 10px; background: var(--bg-primary); box-shadow: 0 16px 40px rgba(0, 0, 0, 0.35); }
  .confirm-dialog p { margin: 0 0 18px; color: var(--text-primary); line-height: 1.45; }
  .confirm-actions { display: flex; justify-content: flex-end; gap: 10px; }

  .item-detail-dialog { width: min(640px, calc(100vw - 48px)); max-height: min(720px, calc(100vh - 48px)); overflow: auto; padding: 18px; border: 1px solid var(--border-primary); border-radius: 10px; background: var(--bg-primary); box-shadow: 0 18px 48px rgba(0, 0, 0, 0.45); }
  .item-detail-header { display: flex; align-items: flex-start; justify-content: space-between; gap: 16px; margin-bottom: 14px; padding-bottom: 12px; border-bottom: 1px solid var(--border-primary); }
  .item-detail-header p { margin: 0 0 4px; color: var(--accent-primary); font-size: 12px; font-weight: 700; text-transform: uppercase; }
  .item-detail-header h3 { margin: 0; color: var(--text-primary); font-size: 22px; }
  .item-detail-meta { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 8px; margin-bottom: 14px; }
  .item-detail-meta div { display: flex; flex-direction: column; gap: 2px; padding: 8px 10px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); }
  .item-detail-meta span { color: var(--text-muted); font-size: 11px; text-transform: uppercase; }
  .item-detail-meta strong { color: var(--text-primary); font-size: 13px; line-height: 1.35; }
  .item-detail-properties { display: grid; gap: 5px; padding: 12px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); color: #65b8ff; font-size: 13px; line-height: 1.35; }
  .item-detail-empty { padding: 12px; border: 1px solid var(--border-primary); border-radius: 8px; background: var(--bg-secondary); color: var(--text-muted); font-size: 13px; }
  .item-detail-note { margin: 12px 0 0; color: var(--text-secondary); font-size: 12px; line-height: 1.45; }

  .grail-volume-control { display: flex; align-items: center; gap: 0.75rem; min-width: 220px; }
  .grail-volume-control input { width: 150px; }
  .grail-volume-control span { min-width: 3rem; text-align: right; color: var(--text-secondary); font-size: 0.875rem; }
</style>

