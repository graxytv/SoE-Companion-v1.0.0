<script lang="ts">
  import { onMount } from 'svelte';
  import { invoke } from '@tauri-apps/api/core';
  import { emit, listen } from '@tauri-apps/api/event';
  import Button from './Button.svelte';
  import HotkeyInput from './HotkeyInput.svelte';
  import Toggle from './Toggle.svelte';
  import { settingsStore, type HotkeyConfig } from '../stores';
  import { playSound } from '../lib/sound-player';

  interface Props {
    onClose: () => void;
    onNavigate?: (tab: string) => void;
  }

  interface DropHookStatus {
    installed: boolean;
    grailInstalled: boolean;
    identifiedInstalled: boolean;
    dllMatchesBundled: boolean;
    managedHookPresent: boolean;
    hookNeedsUpdate: boolean;
    unknownDllPresent: boolean;
    canRestoreOriginal: boolean;
    hookVersionMatches: boolean;
    hookVersion: string | null;
    expectedHookVersion: string;
    projectD2Dir: string;
    projectD2DirExists: boolean;
    originalDllExists: boolean;
    iniExists: boolean;
    logExists: boolean;
    logSize: number;
    message: string;
    dllPath: string;
  }

  interface SoeFilterProfile {
    name: string;
    modified?: string | null;
  }

  type StepStatus = 'done' | 'needs' | 'optional';
  type StepId =
    | 'welcome'
    | 'launcher'
    | 'drops-hook'
    | 'filter'
    | 'stash'
    | 'notifications'
    | 'overlays'
    | 'new-game'
    | 'finish';

  const steps: Array<{ id: StepId; label: string }> = [
    { id: 'welcome', label: 'Welcome' },
    { id: 'launcher', label: 'Launcher' },
    { id: 'drops-hook', label: 'Install Drops Tracker' },
    { id: 'filter', label: 'Loot Filter' },
    { id: 'stash', label: 'Shared Stash & Stats' },
    { id: 'notifications', label: 'Notifications & Sounds' },
    { id: 'overlays', label: 'Overlays' },
    { id: 'new-game', label: 'New Game Automation' },
    { id: 'finish', label: 'Finish' },
  ];

  let { onClose, onNavigate }: Props = $props();
  let activeStep = $state<StepId>('welcome');
  let busy = $state('');
  let message = $state('');
  let launcherDetectedPath = $state<string | null>(null);
  let hookStatus = $state<DropHookStatus | null>(null);
  let projectD2PathDraft = $state(settingsStore.settings.projectD2Path ?? '');
  let stashPaths = $state<string[]>([]);
  let stashPathDraft = $state(settingsStore.settings.runewordPlannerStashPath ?? '');
  let accountStatsMessage = $state('');
  let filterMessage = $state('');
  let filterProfiles = $state<SoeFilterProfile[]>([]);
  let automationMessage = $state('');
  let overlayLayoutEditing = $state(false);
  let overlayLayoutMessage = $state('');

  const currentStepIndex = $derived(steps.findIndex((step) => step.id === activeStep));
  const soundSlots = $derived(settingsStore.settings.sounds);
  const soundChoices = $derived(
    soundSlots
      .map((slot, i) => ({ index: i + 1, slot }))
      .filter(({ slot }) => slot.source.kind !== 'empty'),
  );

  function statusFor(step: StepId): StepStatus {
    if (step === 'welcome' || step === 'finish') return 'optional';
    if (step === 'launcher') return settingsStore.settings.soeLauncherPath || launcherDetectedPath ? 'done' : 'needs';
    if (step === 'drops-hook') return hookStatus?.grailInstalled ? 'done' : 'needs';
    if (step === 'filter') return 'optional';
    if (step === 'stash') return settingsStore.settings.runewordPlannerStashPath || stashPaths.length > 0 ? 'done' : 'needs';
    if (step === 'notifications') return settingsStore.settings.notificationOverlayEnabled ? 'done' : 'optional';
    if (step === 'overlays') {
      return settingsStore.settings.notificationOverlayEnabled ||
        settingsStore.settings.dropsTrackerEnabled ||
        settingsStore.settings.totalDropsTrackerEnabled ||
        settingsStore.settings.holyGrailOverlayEnabled ||
        settingsStore.settings.fateCardTrackerOverlayEnabled ||
        settingsStore.settings.materialTrackerOverlayEnabled ||
        settingsStore.settings.runeTrackerOverlayEnabled ||
        settingsStore.settings.achievementSettings.overlayEnabled ||
        settingsStore.settings.achievementProgressOverlayEnabled ||
        settingsStore.settings.monsterKillsOverlayEnabled ||
        settingsStore.settings.mulingIndicatorOverlayEnabled
        ? 'done'
        : 'optional';
    }
    if (step === 'new-game') {
      const hotkey = settingsStore.settings.gameResetHotkey;
      return hotkey.keyCode || hotkey.modifiers ? 'done' : 'optional';
    }
    return 'optional';
  }

  function statusLabel(step: StepId, status: StepStatus): string {
    if (status === 'done') return 'Done';
    if (step === 'drops-hook') return 'Required';
    if (status === 'needs') return 'Needs Action';
    return 'Optional';
  }

  function profileName(name: string): string {
    const wanted = name.toLowerCase();
    return filterProfiles.find((profile) => profile.name.toLowerCase() === wanted)?.name ?? name;
  }

  function go(delta: number): void {
    const next = Math.max(0, Math.min(steps.length - 1, currentStepIndex + delta));
    activeStep = steps[next].id;
    message = '';
  }

  function openTab(tab: string): void {
    onNavigate?.(tab);
    onClose();
  }

  async function toggleOverlayLayoutEditor(): Promise<void> {
    const active = !overlayLayoutEditing;
    overlayLayoutEditing = active;
    if (active) overlayLayoutMessage = '';
    await emit('overlay-edit-mode', { active });
  }

  async function detectLauncher(): Promise<void> {
    busy = 'launcher';
    message = 'Detecting SoE launcher...';
    try {
      launcherDetectedPath = await invoke<string | null>('detect_soe_launcher_path');
      if (launcherDetectedPath) {
        settingsStore.setSoeLauncherPath(launcherDetectedPath);
        message = `Launcher found: ${launcherDetectedPath}`;
      } else {
        message = 'No launcher found in common install folders.';
      }
    } catch (error) {
      message = `Launcher detection failed: ${error}`;
    } finally {
      busy = '';
    }
  }

  function setManualLauncherPath(): void {
    const selected = window.prompt(
      'Enter the full path to pd2-soe-launcher.exe:',
      settingsStore.settings.soeLauncherPath ?? launcherDetectedPath ?? 'C:\\Program Files\\PD2 Sanctuary of Exile\\pd2-soe-launcher.exe',
    );
    const path = selected?.trim();
    if (!path) return;
    settingsStore.setSoeLauncherPath(path);
    launcherDetectedPath = path;
    message = 'Launcher path saved.';
  }

  async function testLauncher(): Promise<void> {
    busy = 'launcher';
    message = 'Opening launcher...';
    try {
      await invoke('launch_soe_launcher', { path: settingsStore.settings.soeLauncherPath ?? launcherDetectedPath });
      message = 'SoE launcher opened.';
    } catch (error) {
      message = String(error);
    } finally {
      busy = '';
    }
  }

  async function refreshHookStatus(): Promise<void> {
    busy = 'drops-hook-status';
    try {
      hookStatus = await invoke<DropHookStatus>('get_drop_hook_status_for_path', { projectD2Dir: settingsStore.settings.projectD2Path });
      if (!settingsStore.settings.projectD2Path && hookStatus.projectD2DirExists) {
        settingsStore.setProjectD2Path(hookStatus.projectD2Dir);
        projectD2PathDraft = hookStatus.projectD2Dir;
      }
    } catch (error) {
      message = `Could not check Drops Tracker Hook: ${error}`;
    } finally {
      busy = '';
    }
  }

  async function detectProjectD2Folder(): Promise<void> {
    busy = 'drops-hook-detect';
    message = 'Searching common ProjectD2 folders...';
    try {
      const paths = await invoke<string[]>('detect_project_d2_dirs');
      const first = paths[0] ?? '';
      if (!first) {
        message = 'No ProjectD2 folder found in common install locations. Paste your ProjectD2 folder path manually.';
        return;
      }
      projectD2PathDraft = first;
      settingsStore.setProjectD2Path(first);
      await refreshHookStatus();
    } catch (error) {
      message = `ProjectD2 detection failed: ${error}`;
    } finally {
      busy = '';
    }
  }

  async function setManualProjectD2Path(): Promise<void> {
    const selected = window.prompt(
      'Enter the full path to your ProjectD2 folder:',
      projectD2PathDraft || settingsStore.settings.projectD2Path || hookStatus?.projectD2Dir || 'C:\\Program Files (x86)\\Diablo II\\ProjectD2',
    );
    const path = selected?.trim();
    if (!path) return;
    projectD2PathDraft = path;
    settingsStore.setProjectD2Path(path);
    await refreshHookStatus();
  }

  async function installHook(): Promise<void> {
    busy = 'drops-hook-install';
    message = hookStatus?.hookNeedsUpdate ? 'Updating Drops Tracker Hook...' : 'Installing Drops Tracker Hook...';
    try {
      hookStatus = await invoke<DropHookStatus>('install_drop_hook_for_path', { projectD2Dir: settingsStore.settings.projectD2Path });
      if (!settingsStore.settings.projectD2Path && hookStatus.projectD2DirExists) {
        settingsStore.setProjectD2Path(hookStatus.projectD2Dir);
        projectD2PathDraft = hookStatus.projectD2Dir;
      }
      message = hookStatus.message;
    } catch (error) {
      message = `Install failed: ${error}`;
      await refreshHookStatus();
    } finally {
      busy = '';
    }
  }

  function hookStatusLabel(): string {
    if (!hookStatus) return 'Checking';
    if (hookStatus.hookNeedsUpdate) return 'Update Required';
    if (hookStatus.unknownDllPresent) return 'Unknown ijl11.dll';
    if (hookStatus.grailInstalled) return 'Installed';
    return 'Not Installed';
  }

  function installHookButtonLabel(): string {
    if (hookStatus?.hookNeedsUpdate) return 'Update Drops Tracker Hook';
    if (hookStatus?.unknownDllPresent) return 'Replace with Drops Tracker Hook';
    if (hookStatus?.grailInstalled) return 'Installed';
    return 'Install Drops Tracker Hook';
  }

  function hookDetailsMessage(): string {
    if (!hookStatus) return 'Checking status...';
    if (hookStatus.hookNeedsUpdate) return 'Drops Tracker Hook needs an update for this app version.';
    if (hookStatus.unknownDllPresent) return hookStatus.message;
    return hookStatus.message;
  }

  async function copyFilterText(): Promise<void> {
    busy = 'filter';
    filterMessage = 'Copying filter text...';
    try {
      const active = settingsStore.settings.activeProfile;
      const useActive = active && active !== 'Hiim_SOE' && active !== 'Kryzard SoE' && active !== 'Kryzard_SoE';
      const text = useActive
        ? await invoke<string>('load_soe_filter_profile', { name: active })
        : await invoke<string>('get_bundled_soe_filter');
      await navigator.clipboard.writeText(text);
      filterMessage = 'Filter text copied.';
    } catch (error) {
      filterMessage = `Could not copy filter text: ${error}`;
    } finally {
      busy = '';
    }
  }

  async function refreshFilterProfiles(): Promise<void> {
    try {
      filterProfiles = await invoke<SoeFilterProfile[]>('list_soe_filter_profiles');
    } catch (error) {
      filterMessage = `Could not load filter presets: ${error}`;
    }
  }

  async function copyFilterProfile(name: string): Promise<void> {
    const resolved = profileName(name);
    busy = `filter-${name}`;
    filterMessage = `Copying ${resolved}...`;
    try {
      const text = resolved === 'Hiim_SOE'
        ? await invoke<string>('get_bundled_soe_filter')
        : await invoke<string>('load_soe_filter_profile', { name: resolved });
      await navigator.clipboard.writeText(text);
      filterMessage = `${resolved} copied. Paste it into the filter file Diablo II says it loaded.`;
    } catch (error) {
      filterMessage = `Could not copy ${resolved}: ${error}`;
    } finally {
      busy = '';
    }
  }

  async function detectStash(): Promise<void> {
    busy = 'stash-detect';
    accountStatsMessage = 'Detecting shared stash...';
    try {
      stashPaths = await invoke<string[]>('detect_shared_stash_paths');
      const saved = settingsStore.settings.runewordPlannerStashPath;
      const path = saved || stashPaths[0] || '';
      stashPathDraft = path;
      if (path) settingsStore.setRunewordPlannerStashPath(path);
      accountStatsMessage = path ? `Shared stash selected: ${path}` : 'No pd2_shared.stash found.';
    } catch (error) {
      accountStatsMessage = `Could not detect shared stash: ${error}`;
    } finally {
      busy = '';
    }
  }

  function saveStashPath(): void {
    const path = stashPathDraft.trim();
    settingsStore.setRunewordPlannerStashPath(path || null);
    accountStatsMessage = path
      ? 'Shared stash path saved. Use the header Sync button to refresh stash, account stats, and character levels.'
      : 'Shared stash path cleared.';
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

  async function saveAutomationConfig(): Promise<void> {
    busy = 'automation';
    automationMessage = 'Saving automation config...';
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
      });
      automationMessage = `Saved to ${path}`;
    } catch (error) {
      automationMessage = `Could not save automation config: ${error}`;
    } finally {
      busy = '';
    }
  }

  function numberInput(event: Event): number {
    return Number((event.currentTarget as HTMLInputElement).value);
  }

  function finish(): void {
    if (!hookStatus?.grailInstalled) {
      activeStep = 'drops-hook';
      message = 'Install Drops Tracker Hook before finishing setup.';
      return;
    }
    settingsStore.set('setupWizardCompleted', true);
    onClose();
  }

  onMount(() => {
    const unlisteners: Array<() => void> = [];
    listen<{ active: boolean }>('overlay-edit-mode', (event) => {
      overlayLayoutEditing = event.payload.active;
    }).then((unlisten) => unlisteners.push(unlisten));
    listen<{ message: string }>('overlay-layout-message', (event) => {
      overlayLayoutMessage = event.payload.message;
    }).then((unlisten) => unlisteners.push(unlisten));

    void detectLauncher();
    void refreshHookStatus();
    void detectStash();
    void refreshFilterProfiles();

    return () => {
      unlisteners.forEach((unlisten) => unlisten());
    };
  });
</script>

<div class="wizard-backdrop" role="presentation" onclick={onClose}>
  <section class="wizard" role="dialog" aria-modal="true" aria-label="SoE Companion Setup Wizard" onclick={(event) => event.stopPropagation()}>
    <aside class="wizard-sidebar">
      <div>
        <h2>Setup Wizard</h2>
        <p>Guided setup for the companion features most players care about.</p>
      </div>
      <nav aria-label="Setup steps">
        {#each steps as step}
          {@const status = statusFor(step.id)}
          <button
            type="button"
            class:active={activeStep === step.id}
            onclick={() => { activeStep = step.id; message = ''; }}
          >
            <span>{step.label}</span>
            <small class={status}>{statusLabel(step.id, status)}</small>
          </button>
        {/each}
      </nav>
    </aside>

    <div class="wizard-main">
      <header class="wizard-header">
        <div>
          <span>Step {currentStepIndex + 1} of {steps.length}</span>
          <h2>{steps[currentStepIndex].label}</h2>
        </div>
        <button class="wizard-close" type="button" aria-label="Close setup wizard" onclick={onClose}>&times;</button>
      </header>

      <div class="wizard-body">
        {#if activeStep === 'welcome'}
          <div class="intro-panel">
            <h3>Let’s get SoE Companion ready.</h3>
            <p>This wizard checks the launcher, required drops tracker hook, filters, shared stash, notifications, overlays, and the new-game automation. Nothing risky is installed or changed unless you press the button for that step.</p>
            <div class="fresh-run-note">
              <strong>Best used on a fresh run.</strong>
              <span>SoE Companion only reads local character and shared-stash data; it does not modify <code>pd2_shared.stash</code>. If you manually change your <code>Diablo II\Save</code> folder for a fresh run, back it up first, including all character files and <code>pd2_shared.stash</code>.</span>
            </div>
            <p class="quiet">You can close this any time and come back from the Home tab.</p>
          </div>
        {:else if activeStep === 'launcher'}
          <div class="step-grid">
            <div class="step-card">
              <h3>SoE Launcher</h3>
              <p>The Home Play button opens the SoE launcher. If auto-detect misses it, save the launcher path manually.</p>
              <div class="path-box">{settingsStore.settings.soeLauncherPath ?? launcherDetectedPath ?? 'No launcher path saved yet.'}</div>
              <div class="action-row">
                <Button variant="secondary" size="sm" disabled={busy === 'launcher'} onclick={detectLauncher}>Detect</Button>
                <Button variant="secondary" size="sm" onclick={setManualLauncherPath}>Set Path</Button>
                <Button variant="primary" size="sm" disabled={busy === 'launcher'} onclick={testLauncher}>Test Launch</Button>
              </div>
            </div>
          </div>
        {:else if activeStep === 'drops-hook'}
          <div class="step-grid">
            <div class="step-card">
              <h3>Install Drops Tracker</h3>
              <p>Installs the required SoE <code>ijl11.dll</code> drop hook so drops can feed the Drops Tracker, Holy Grail, Fate Cards, materials, runes, sounds, and overlays.</p>
              <div class="status-card" class:good={hookStatus?.grailInstalled}>
                <strong>{hookStatusLabel()}</strong>
                <span>{hookDetailsMessage()}</span>
              </div>
              <div class="hook-details">
                <span>ProjectD2 folder: {hookStatus?.projectD2DirExists ? 'Found' : 'Missing'}</span>
                <span>Original backup: {hookStatus?.originalDllExists ? 'Found' : 'Missing'}</span>
                <span>Config: {hookStatus?.iniExists ? 'Found' : 'Missing'}</span>
                <span>Hook version: {hookStatus?.hookVersionMatches ? 'Current' : `Expected ${hookStatus?.expectedHookVersion ?? '-'}`}</span>
              </div>
              <div class="wizard-path-picker">
                <label>
                  <span>ProjectD2 Folder</span>
                  <input
                    value={projectD2PathDraft}
                    placeholder="C:\Program Files (x86)\Diablo II\ProjectD2"
                    oninput={(event) => (projectD2PathDraft = (event.currentTarget as HTMLInputElement).value)}
                  />
                </label>
                <div class="action-row">
                  <Button variant="secondary" size="sm" disabled={busy !== ''} onclick={detectProjectD2Folder}>Auto Detect</Button>
                  <Button variant="secondary" size="sm" disabled={busy !== ''} onclick={setManualProjectD2Path}>Select Folder</Button>
                  <Button variant="primary" size="sm" disabled={busy !== ''} onclick={async () => { settingsStore.setProjectD2Path(projectD2PathDraft); await refreshHookStatus(); }}>Use Folder</Button>
                </div>
              </div>
              <p class="warning">Required. Close Diablo II before installing. If Windows blocks Program Files writes, run SoE Companion as administrator.</p>
              <div class="action-row">
                <Button variant="secondary" size="sm" disabled={busy !== ''} onclick={refreshHookStatus}>Refresh</Button>
                <Button variant="primary" size="sm" disabled={busy !== '' || (hookStatus?.grailInstalled && !hookStatus?.hookNeedsUpdate)} onclick={installHook}>{installHookButtonLabel()}</Button>
              </div>
            </div>
          </div>
        {:else if activeStep === 'filter'}
          <div class="step-card">
            <h3>Loot Filter Editing</h3>
            <p>SoE Companion is only the editor. Diablo II chooses the filter file it reads, so the safest setup is to let the game tell you exactly which file to edit.</p>
            <ol class="compact-list">
              <li>Open Diablo II and press your <strong>Reload Loot Filter</strong> hotkey. If you do not know it, check <strong>Options &gt; Configure Hotkeys</strong> near the bottom.</li>
              <li>Watch the white text in the top-left corner. You need the line that says <strong>Reloaded Filter</strong>. Ignore <strong>Reloaded Config</strong>.</li>
              <li>The end of that line shows the exact `.filter` file the game loaded. Open that file in Notepad.</li>
              <li>Delete everything inside that file, copy the Hiim_SOE filter below, paste it into Notepad, then save.</li>
              <li>Return to game and press Reload Loot Filter again. When it says <strong>Reloaded Filter</strong>, the new filter is active.</li>
            </ol>
            <div class="action-row">
              <Button variant="primary" size="sm" disabled={busy.startsWith('filter')} onclick={() => copyFilterProfile('Hiim_SOE')}>Copy Hiim_SOE Filter</Button>
              <Button variant="secondary" size="sm" disabled={busy === 'filter'} onclick={copyFilterText}>Copy Current Filter</Button>
              <Button variant="secondary" size="sm" onclick={() => openTab('lootfilter')}>Open Loot Filter Tab</Button>
            </div>
            <p class="quiet">Hiim_SOE is the supported preset because SoE Companion tracking aliases are tuned around it. If you create your own preset in the Loot Filter tab, use <strong>Copy Current Filter</strong> here.</p>
            {#if filterMessage}<p class="step-message">{filterMessage}</p>{/if}
          </div>
        {:else if activeStep === 'stash'}
          <div class="step-card">
            <h3>Shared Stash & Account Stats</h3>
            <p>Runeword planning, Fate Cards, account stats, and character levels sync from your shared stash/save folder. Save the path here, then use the header Sync button to refresh everything at once.</p>
            <label class="field">
              <span>Shared Stash File</span>
              <select value={settingsStore.settings.runewordPlannerStashPath ?? ''} onchange={(event) => {
                const value = (event.currentTarget as HTMLSelectElement).value;
                stashPathDraft = value;
                settingsStore.setRunewordPlannerStashPath(value || null);
              }}>
                <option value="">Auto-detect pd2_shared.stash</option>
                {#each stashPaths as path}
                  <option value={path}>{path}</option>
                {/each}
              </select>
            </label>
            <label class="field">
              <span>Manual Path</span>
              <input bind:value={stashPathDraft} placeholder="C:\Program Files (x86)\Diablo II\Save\pd2_shared.stash" />
            </label>
            <div class="action-row">
              <Button variant="secondary" size="sm" disabled={busy === 'stash-detect'} onclick={detectStash}>Detect Stash</Button>
              <Button variant="secondary" size="sm" onclick={saveStashPath}>Save Path</Button>
            </div>
            {#if accountStatsMessage}<p class="step-message">{accountStatsMessage}</p>{/if}
          </div>
        {:else if activeStep === 'notifications'}
          <div class="step-card">
            <h3>Notifications & Sounds</h3>
            <p>These settings control the SoE Companion popups, especially new Holy Grail discoveries.</p>
            <div class="toggle-row">
              <span>Show notification overlay</span>
              <Toggle checked={settingsStore.settings.notificationOverlayEnabled} onchange={(enabled) => settingsStore.setNotificationOverlayEnabled(enabled)} />
            </div>
            <div class="toggle-row">
              <span>New grail item popup</span>
              <Toggle checked={settingsStore.settings.holyGrailNewItemNotificationEnabled} onchange={(enabled) => settingsStore.set('holyGrailNewItemNotificationEnabled', enabled)} />
            </div>
            <label class="field">
              <span>New Grail Sound</span>
              <select value={settingsStore.settings.holyGrailNewItemSoundSlot ?? ''} onchange={handleGrailSoundChange}>
                <option value="">No dedicated sound</option>
                {#each soundChoices as { index, slot }}
                  <option value={index}>{slot.label || `Sound ${index}`}</option>
                {/each}
              </select>
            </label>
            <div class="action-row">
              <Button variant="secondary" size="sm" onclick={testGrailSound}>Test Sound</Button>
              <Button variant="secondary" size="sm" onclick={() => openTab('sounds')}>Open Sounds Tab</Button>
            </div>
          </div>
        {:else if activeStep === 'overlays'}
          <div class="step-card">
            <h3>Overlays</h3>
            <p>Turn on the overlay cards you want, then place them over Diablo II with the layout editor.</p>
            <div class="overlay-editor-card">
              <div>
                <strong>Overlay layout</strong>
                <span>Start Diablo II first, then move and resize each overlay directly on the game.</span>
              </div>
              <div class="overlay-edit-control">
                <Button variant={overlayLayoutEditing ? 'secondary' : 'primary'} size="sm" onclick={toggleOverlayLayoutEditor}>
                  {overlayLayoutEditing ? 'Done Editing' : 'Move/Resize Overlays'}
                </Button>
                {#if overlayLayoutMessage}
                  <span class="overlay-edit-status">{overlayLayoutMessage}</span>
                {/if}
              </div>
            </div>
            <div class="toggle-row">
              <span>Always show overlays</span>
              <Toggle checked={settingsStore.settings.alwaysShowOverlays} onchange={(enabled) => settingsStore.set('alwaysShowOverlays', enabled)} />
            </div>
            <div class="overlay-toggle-grid">
              <label class="overlay-toggle-card">
                <span>Notifications</span>
                <Toggle checked={settingsStore.settings.notificationOverlayEnabled} onchange={(enabled) => settingsStore.setNotificationOverlayEnabled(enabled)} />
              </label>
              <label class="overlay-toggle-card">
                <span>Drops Tracker</span>
                <Toggle checked={settingsStore.settings.dropsTrackerEnabled} onchange={(enabled) => settingsStore.set('dropsTrackerEnabled', enabled)} />
              </label>
              <label class="overlay-toggle-card">
                <span>Total Drops</span>
                <Toggle checked={settingsStore.settings.totalDropsTrackerEnabled} onchange={(enabled) => settingsStore.setTotalDropsTrackerEnabled(enabled)} />
              </label>
              <label class="overlay-toggle-card">
                <span>Grail Progress</span>
                <Toggle checked={settingsStore.settings.holyGrailOverlayEnabled} onchange={(enabled) => settingsStore.setHolyGrailOverlayEnabled(enabled)} />
              </label>
              <label class="overlay-toggle-card">
                <span>Fate Cards</span>
                <Toggle checked={settingsStore.settings.fateCardTrackerOverlayEnabled} onchange={(enabled) => settingsStore.setFateCardTrackerOverlayEnabled(enabled)} />
              </label>
              <label class="overlay-toggle-card">
                <span>Mats Tracker</span>
                <Toggle checked={settingsStore.settings.materialTrackerOverlayEnabled} onchange={(enabled) => settingsStore.setMaterialTrackerOverlayEnabled(enabled)} />
              </label>
              <label class="overlay-toggle-card">
                <span>Rune Tracker</span>
                <Toggle checked={settingsStore.settings.runeTrackerOverlayEnabled} onchange={(enabled) => settingsStore.setRuneTrackerOverlayEnabled(enabled)} />
              </label>
              <label class="overlay-toggle-card">
                <span>Achievement Pop-up</span>
                <Toggle checked={settingsStore.settings.achievementSettings.overlayEnabled} onchange={(enabled) => settingsStore.setAchievementSettings({ overlayEnabled: enabled })} />
              </label>
              <label class="overlay-toggle-card">
                <span>Achievement Progress</span>
                <Toggle checked={settingsStore.settings.achievementProgressOverlayEnabled} onchange={(enabled) => settingsStore.setAchievementProgressOverlayEnabled(enabled)} />
              </label>
              <label class="overlay-toggle-card">
                <span>Total Kills</span>
                <Toggle checked={settingsStore.settings.monsterKillsOverlayEnabled} onchange={(enabled) => settingsStore.setMonsterKillsOverlayEnabled(enabled)} />
              </label>
              <label class="overlay-toggle-card">
                <span>Muling Indicator</span>
                <Toggle checked={settingsStore.settings.mulingIndicatorOverlayEnabled} onchange={(enabled) => settingsStore.setMulingIndicatorOverlayEnabled(enabled)} />
              </label>
            </div>

            <div class="action-row">
              <Button variant="secondary" size="sm" onclick={() => openTab('drops-tracker')}>Open Drops Tracker</Button>
              <Button variant="secondary" size="sm" onclick={() => openTab('holy-grail')}>Open Holy Grail</Button>
              <Button variant="secondary" size="sm" onclick={() => openTab('fate-cards')}>Open Fate Cards</Button>
              <Button variant="secondary" size="sm" onclick={() => openTab('achievements')}>Open Achievements</Button>
            </div>
          </div>
        {:else if activeStep === 'new-game'}
          <div class="step-card">
            <h3>New Game Automation</h3>
            <p>This saves the helper script config used by the game reset hotkey. Coordinates should point at the Single Player button on the main menu.</p>
            <div class="hotkey-row">
              <span>Automation Hotkey</span>
              <HotkeyInput value={settingsStore.settings.gameResetHotkey} onchange={(hotkey: HotkeyConfig) => settingsStore.setGameResetHotkey(hotkey)} />
            </div>
            <div class="automation-grid">
              <label class="field">
                <span>Difficulty</span>
                <select value={settingsStore.settings.saveExitAutomationDifficulty} onchange={(event) => settingsStore.set('saveExitAutomationDifficulty', (event.currentTarget as HTMLSelectElement).value as 'Normal' | 'Nightmare' | 'Hell')}>
                  <option value="Normal">Normal (R)</option>
                  <option value="Nightmare">Nightmare (N)</option>
                  <option value="Hell">Hell (H)</option>
                </select>
              </label>
              <label class="field">
                <span>Single Player X</span>
                <input type="number" min="0" value={settingsStore.settings.saveExitAutomationClickX} oninput={(event) => settingsStore.set('saveExitAutomationClickX', numberInput(event))} />
              </label>
              <label class="field">
                <span>Single Player Y</span>
                <input type="number" min="0" value={settingsStore.settings.saveExitAutomationClickY} oninput={(event) => settingsStore.set('saveExitAutomationClickY', numberInput(event))} />
              </label>
              <label class="field">
                <span>Step Delay</span>
                <input type="number" min="50" max="2000" value={settingsStore.settings.saveExitAutomationDelayMs} oninput={(event) => settingsStore.set('saveExitAutomationDelayMs', numberInput(event))} />
              </label>
            </div>
            <div class="toggle-row">
              <span>Use percent coordinates</span>
              <Toggle checked={settingsStore.settings.saveExitAutomationCoordinateModePercent} onchange={(enabled) => settingsStore.set('saveExitAutomationCoordinateModePercent', enabled)} />
            </div>
            <div class="action-row">
              <Button variant="primary" size="sm" disabled={busy === 'automation'} onclick={saveAutomationConfig}>Save Script Config</Button>
              <Button variant="secondary" size="sm" onclick={() => openTab('general')}>Open General Tab</Button>
            </div>
            {#if automationMessage}<p class="step-message">{automationMessage}</p>{/if}
          </div>
        {:else if activeStep === 'finish'}
          <div class="intro-panel">
            <h3>Setup is ready.</h3>
            <p>You can revisit this wizard from Home any time. Advanced controls remain in their dedicated tabs.</p>
            <div class="summary-list">
              {#each steps.filter((step) => step.id !== 'welcome' && step.id !== 'finish') as step}
                {@const status = statusFor(step.id)}
                <div><span>{step.label}</span><strong class={status}>{statusLabel(step.id, status)}</strong></div>
              {/each}
            </div>
            <Button variant="primary" onclick={finish}>Finish Setup</Button>
          </div>
        {/if}

        {#if message}
          <p class="step-message">{message}</p>
        {/if}
      </div>

      <footer class="wizard-footer">
        <Button variant="secondary" disabled={currentStepIndex === 0} onclick={() => go(-1)}>Back</Button>
        {#if activeStep === 'finish'}
          <Button variant="primary" onclick={finish}>Finish</Button>
        {:else}
          <Button variant="primary" onclick={() => go(1)}>Next</Button>
        {/if}
      </footer>
    </div>
  </section>
</div>

<style>
  .wizard-backdrop {
    position: fixed;
    inset: 0;
    z-index: 1200;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 24px;
    background: rgba(0, 0, 0, 0.72);
  }

  .wizard {
    display: grid;
    grid-template-columns: 250px minmax(0, 1fr);
    width: min(1040px, 96vw);
    height: min(720px, 92vh);
    overflow: hidden;
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-lg);
    background: var(--bg-primary);
    box-shadow: var(--shadow-lg);
  }

  .wizard-sidebar {
    display: flex;
    flex-direction: column;
    gap: 18px;
    padding: 18px;
    border-right: 1px solid var(--border-primary);
    background: var(--bg-secondary);
  }

  .wizard-sidebar h2,
  .wizard-header h2,
  .step-card h3,
  .intro-panel h3 {
    margin: 0;
    color: var(--text-primary);
  }

  .wizard-sidebar p,
  .intro-panel p,
  .step-card p {
    color: var(--text-secondary);
    line-height: 1.55;
  }

  .wizard-sidebar nav {
    display: flex;
    flex-direction: column;
    gap: 6px;
    min-height: 0;
    overflow-y: auto;
  }

  .wizard-sidebar button {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
    width: 100%;
    padding: 9px 10px;
    border: 1px solid transparent;
    border-radius: 6px;
    background: transparent;
    color: var(--text-secondary);
    cursor: pointer;
    text-align: left;
  }

  .wizard-sidebar button.active,
  .wizard-sidebar button:hover {
    border-color: var(--border-primary);
    background: var(--bg-elevated);
    color: var(--text-primary);
  }

  .wizard-sidebar small,
  .summary-list strong {
    font-family: var(--font-mono);
    font-size: 10px;
    white-space: nowrap;
  }

  .done { color: var(--status-success-text, #7dff9e); }
  .needs { color: var(--status-error-text, #ff836f); }
  .optional { color: var(--text-muted); }

  .wizard-main {
    display: flex;
    flex-direction: column;
    min-width: 0;
    min-height: 0;
  }

  .wizard-header,
  .wizard-footer {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    padding: 16px 18px;
    border-bottom: 1px solid var(--border-primary);
    background: var(--bg-secondary);
  }

  .wizard-header span {
    color: var(--text-muted);
    font-family: var(--font-mono);
    font-size: 11px;
  }

  .wizard-close {
    border: none;
    background: transparent;
    color: var(--text-muted);
    cursor: pointer;
    font-size: 28px;
    line-height: 1;
  }

  .wizard-close:hover {
    color: var(--text-primary);
  }

  .wizard-body {
    flex: 1;
    min-height: 0;
    overflow-y: auto;
    padding: 18px;
  }

  .wizard-footer {
    border-top: 1px solid var(--border-primary);
    border-bottom: 0;
  }

  .intro-panel,
  .step-card {
    display: flex;
    flex-direction: column;
    gap: 14px;
    padding: 18px;
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-md);
    background: var(--panel-bg);
  }

  .fresh-run-note,
  .wizard-path-picker {
    display: grid;
    gap: 8px;
    padding: 12px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
    color: var(--text-secondary);
    font-size: 12px;
    line-height: 1.5;
  }

  .fresh-run-note strong {
    color: var(--accent-primary);
    font-size: 13px;
  }

  .wizard-path-picker label {
    display: grid;
    gap: 6px;
  }

  .wizard-path-picker input {
    width: 100%;
    padding: 9px 10px;
    border: 1px solid var(--border-primary);
    border-radius: 6px;
    background: var(--bg-primary);
    color: var(--text-primary);
  }

  .quiet,
  .warning {
    color: var(--text-muted);
    font-size: 12px;
  }

  .warning {
    color: var(--accent-primary);
  }

  .path-box,
  .status-card,
  .hook-details,
  .step-message {
    padding: 10px 12px;
    border: 1px solid var(--border-primary);
    border-radius: 6px;
    background: var(--bg-secondary);
    color: var(--text-secondary);
    font-family: var(--font-mono);
    font-size: 12px;
    overflow-wrap: anywhere;
  }

  .status-card {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .status-card.good {
    border-color: rgba(82, 255, 143, 0.35);
  }

  .hook-details,
  .summary-list {
    display: grid;
    gap: 7px;
  }

  .summary-list div,
  .toggle-row,
  .hotkey-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 14px;
    padding: 10px 0;
    border-bottom: 1px solid var(--border-primary);
    color: var(--text-secondary);
  }

  .action-row {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
  }

  .compact-list {
    margin: 0;
    padding-left: 20px;
    color: var(--text-secondary);
    line-height: 1.7;
  }

  .field {
    display: flex;
    flex-direction: column;
    gap: 6px;
    color: var(--text-secondary);
    font-size: 12px;
  }

  .field input,
  .field select {
    width: 100%;
    padding: 9px 10px;
    border: 1px solid var(--border-primary);
    border-radius: 6px;
    background: var(--bg-primary);
    color: var(--text-primary);
  }

  .automation-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: 10px;
  }

  .overlay-editor-card {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 14px;
    padding: 14px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
  }

  .overlay-editor-card > div:first-child {
    display: grid;
    gap: 4px;
    min-width: 0;
  }

  .overlay-editor-card strong {
    color: var(--text-primary);
    font-size: 13px;
  }

  .overlay-editor-card span {
    color: var(--text-secondary);
    font-size: 12px;
    line-height: 1.35;
  }

  .overlay-edit-control {
    display: flex;
    flex-direction: column;
    align-items: flex-end;
    gap: 6px;
    flex-shrink: 0;
  }

  .overlay-edit-status {
    max-width: 260px;
    color: var(--accent-primary);
    font-size: 12px;
    line-height: 1.3;
    text-align: right;
  }

  .overlay-toggle-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(210px, 1fr));
    gap: 10px;
  }

  .overlay-toggle-card {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    min-height: 48px;
    padding: 10px 12px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
    color: var(--text-primary);
    font-size: 13px;
  }

  .overlay-toggle-card span {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  @media (max-width: 780px) {
    .wizard {
      grid-template-columns: 1fr;
    }

    .wizard-sidebar {
      max-height: 210px;
      border-right: 0;
      border-bottom: 1px solid var(--border-primary);
    }

    .overlay-editor-card {
      align-items: stretch;
      flex-direction: column;
    }

    .overlay-edit-control {
      align-items: stretch;
    }

    .overlay-edit-status {
      max-width: none;
      text-align: left;
    }
  }
</style>
