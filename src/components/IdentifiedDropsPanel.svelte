<script lang="ts">
  import { invoke } from '@tauri-apps/api/core';
  import { onMount } from 'svelte';
  import { Button, Toggle } from '.';
  import { settingsStore } from '../stores';

  interface DropHookStatus {
    projectD2Dir: string;
    dllPath: string;
    originalDllPath: string;
    iniPath: string;
    projectD2DirExists: boolean;
    dllExists: boolean;
    originalDllExists: boolean;
    iniExists: boolean;
    installed: boolean;
    grailInstalled: boolean;
    identifiedInstalled: boolean;
    hookNeedsUpdate: boolean;
    unknownDllPresent: boolean;
    message: string;
  }

  interface DropIdentifiedConfig {
    enabled: boolean;
    magic: boolean;
    rare: boolean;
    set: boolean;
    unique: boolean;
    smallCharm: boolean;
    largeCharm: boolean;
    grandCharm: boolean;
  }

  let dropHookStatus = $state<DropHookStatus | null>(null);
  let dropIdentifiedConfig = $state<DropIdentifiedConfig>({
    enabled: false,
    magic: true,
    rare: true,
    set: true,
    unique: true,
    smallCharm: false,
    largeCharm: false,
    grandCharm: false,
  });
  let identifiedDropsBusy = $state(false);
  let identifiedDropsMessage = $state('');
  let projectD2PathDraft = $state(settingsStore.settings.projectD2Path ?? '');

  onMount(() => {
    void refreshIdentifiedDrops();
  });

  async function refreshIdentifiedDrops(): Promise<void> {
    identifiedDropsBusy = true;
    try {
      const [status, config] = await Promise.all([
        invoke<DropHookStatus>('get_drop_hook_status_for_path', { projectD2Dir: settingsStore.settings.projectD2Path }),
        invoke<DropIdentifiedConfig>('get_drop_identified_config_for_path', { projectD2Dir: settingsStore.settings.projectD2Path }),
      ]);
      dropHookStatus = status;
      dropIdentifiedConfig = config;
      if (!settingsStore.settings.projectD2Path && status.projectD2DirExists) {
        settingsStore.setProjectD2Path(status.projectD2Dir);
        projectD2PathDraft = status.projectD2Dir;
      }
      identifiedDropsMessage = '';
    } catch (err) {
      identifiedDropsMessage = `Could not check identified drops: ${err}`;
    } finally {
      identifiedDropsBusy = false;
    }
  }

  async function installIdentifiedDropsHook(): Promise<void> {
    identifiedDropsBusy = true;
    identifiedDropsMessage = 'Installing drop hook...';
    try {
      dropHookStatus = await invoke<DropHookStatus>('install_identified_drops_hook_for_path', { projectD2Dir: settingsStore.settings.projectD2Path });
      dropIdentifiedConfig = await invoke<DropIdentifiedConfig>('get_drop_identified_config_for_path', { projectD2Dir: settingsStore.settings.projectD2Path });
      identifiedDropsMessage = 'Identified Drops installed. Changes take effect the next time Diablo II starts.';
    } catch (err) {
      identifiedDropsMessage = `Install failed: ${err}`;
    } finally {
      identifiedDropsBusy = false;
    }
  }

  async function uninstallIdentifiedDropsHook(): Promise<void> {
    identifiedDropsBusy = true;
    identifiedDropsMessage = 'Removing Identified Drops...';
    try {
      dropHookStatus = await invoke<DropHookStatus>('uninstall_identified_drops_hook_for_path', { projectD2Dir: settingsStore.settings.projectD2Path });
      dropIdentifiedConfig = await invoke<DropIdentifiedConfig>('get_drop_identified_config_for_path', { projectD2Dir: settingsStore.settings.projectD2Path });
      identifiedDropsMessage = dropHookStatus.message;
    } catch (err) {
      identifiedDropsMessage = `Remove failed: ${err}`;
    } finally {
      identifiedDropsBusy = false;
    }
  }

  async function saveIdentifiedDrops(config: DropIdentifiedConfig): Promise<void> {
    identifiedDropsBusy = true;
    try {
      dropIdentifiedConfig = await invoke<DropIdentifiedConfig>('write_drop_identified_config_for_path', {
        config,
        projectD2Dir: settingsStore.settings.projectD2Path,
      });
      dropHookStatus = await invoke<DropHookStatus>('get_drop_hook_status_for_path', { projectD2Dir: settingsStore.settings.projectD2Path });
      identifiedDropsMessage = 'Saved DropIdentified.ini. Changes take effect the next time Diablo II starts.';
    } catch (err) {
      identifiedDropsMessage = `Save failed: ${err}`;
    } finally {
      identifiedDropsBusy = false;
    }
  }

  async function setIdentifiedDropsEnabled(enabled: boolean): Promise<void> {
    let next = { ...dropIdentifiedConfig, enabled };
    if (enabled && !next.magic && !next.rare && !next.set && !next.unique && !next.smallCharm && !next.largeCharm && !next.grandCharm) {
      next = { ...next, magic: true, rare: true, set: true, unique: true };
    }
    if (enabled && !dropHookStatus?.installed) {
      await installIdentifiedDropsHook();
    }
    await saveIdentifiedDrops(next);
  }

  async function setIdentifiedQuality(key: 'magic' | 'rare' | 'set' | 'unique', enabled: boolean): Promise<void> {
    const next = { ...dropIdentifiedConfig, [key]: enabled };
    next.enabled = next.magic || next.rare || next.set || next.unique || next.smallCharm || next.largeCharm || next.grandCharm;
    await saveIdentifiedDrops(next);
  }

  async function setIdentifiedCharm(key: 'smallCharm' | 'largeCharm' | 'grandCharm', enabled: boolean): Promise<void> {
    const next = { ...dropIdentifiedConfig, [key]: enabled };
    next.enabled = next.magic || next.rare || next.set || next.unique || next.smallCharm || next.largeCharm || next.grandCharm;
    await saveIdentifiedDrops(next);
  }

  async function autoDetectProjectD2Folder(): Promise<void> {
    identifiedDropsBusy = true;
    identifiedDropsMessage = 'Searching common ProjectD2 folders...';
    try {
      const paths = await invoke<string[]>('detect_project_d2_dirs');
      const first = paths[0] ?? '';
      if (!first) {
        identifiedDropsMessage = 'No ProjectD2 folder found in common install locations. Paste your ProjectD2 folder path below.';
        return;
      }
      projectD2PathDraft = first;
      settingsStore.setProjectD2Path(first);
      await refreshIdentifiedDrops();
    } catch (err) {
      identifiedDropsMessage = `Auto-detect failed: ${err}`;
    } finally {
      identifiedDropsBusy = false;
    }
  }

  async function chooseProjectD2Folder(): Promise<void> {
    const selected = window.prompt(
      'Enter the full path to your ProjectD2 folder:',
      projectD2PathDraft || settingsStore.settings.projectD2Path || dropHookStatus?.projectD2Dir || 'C:\\Program Files (x86)\\Diablo II\\ProjectD2',
    );
    if (selected == null) return;
    projectD2PathDraft = selected.trim();
    settingsStore.setProjectD2Path(projectD2PathDraft);
    await refreshIdentifiedDrops();
  }

  async function saveProjectD2Folder(): Promise<void> {
    settingsStore.setProjectD2Path(projectD2PathDraft);
    await refreshIdentifiedDrops();
  }

  function identifiedStatusLabel(): string {
    return dropHookStatus?.identifiedInstalled ? 'Installed' : 'Not Installed';
  }

  function identifiedStatusMessage(): string {
    if (identifiedDropsMessage) return identifiedDropsMessage;
    if (!dropHookStatus) return 'Checking Identified Drops status...';
    if (!dropHookStatus.projectD2DirExists) return 'ProjectD2 folder was not found.';
    if (dropHookStatus.hookNeedsUpdate) {
      return 'Shared hook needs an update before Identified Drops can run.';
    }
    if (dropHookStatus.unknownDllPresent) {
      return 'ProjectD2 has an unknown ijl11.dll. Install the Drops Tracker Hook before enabling Identified Drops.';
    }
    if (dropHookStatus.identifiedInstalled && dropHookStatus.grailInstalled) {
      return 'Identified Drops are installed. Drops Tracker Hook is also enabled.';
    }
    if (dropHookStatus.identifiedInstalled) {
      return 'Identified Drops are installed. Drops Tracker Hook is off.';
    }
    if (dropHookStatus.grailInstalled) {
      return 'Drops Tracker Hook is installed. Identified Drops are off.';
    }
    if (dropHookStatus.installed) {
      return 'Shared SoE hook is installed. Identified Drops are off.';
    }
    return 'Identified Drops are not installed.';
  }
</script>

<div class="settings-section identified-drops-panel">
  <div class="hook-card">
    <div class="hook-status">
      <span class="status-dot" class:active={dropHookStatus?.identifiedInstalled}></span>
      <div>
        <strong>{identifiedStatusLabel()}</strong>
        <span>{identifiedStatusMessage()}</span>
      </div>
    </div>
    <div class="hook-actions">
      <Button variant="primary" size="sm" disabled={identifiedDropsBusy || dropHookStatus?.identifiedInstalled} onclick={installIdentifiedDropsHook}>
        {dropHookStatus?.identifiedInstalled ? 'Installed' : 'Install Identified Drops'}
      </Button>
      {#if dropHookStatus?.identifiedInstalled}
        <Button variant="danger" size="sm" disabled={identifiedDropsBusy} onclick={uninstallIdentifiedDropsHook}>Remove</Button>
      {/if}
      <Button variant="secondary" size="sm" disabled={identifiedDropsBusy} onclick={refreshIdentifiedDrops}>Refresh Status</Button>
    </div>
  </div>

  {#if dropHookStatus && !dropHookStatus.projectD2DirExists}
  <div class="project-d2-picker">
    <label class="project-d2-field">
      <span>ProjectD2 Folder</span>
      <input
        value={projectD2PathDraft}
        placeholder="C:\Program Files (x86)\Diablo II\ProjectD2"
        oninput={(event) => (projectD2PathDraft = (event.currentTarget as HTMLInputElement).value)}
      />
    </label>
    <div class="project-d2-actions">
      <Button variant="secondary" size="sm" disabled={identifiedDropsBusy} onclick={autoDetectProjectD2Folder}>Auto Detect</Button>
      <Button variant="secondary" size="sm" disabled={identifiedDropsBusy} onclick={chooseProjectD2Folder}>Select Folder</Button>
      <Button variant="primary" size="sm" disabled={identifiedDropsBusy} onclick={saveProjectD2Folder}>Use Folder</Button>
    </div>
  </div>
  {/if}

  {#if dropHookStatus?.identifiedInstalled || dropIdentifiedConfig.enabled}
  <div class="identified-options">
  <div class="setting-row">
    <div class="setting-info">
      <span class="setting-label">Enable Identified Drops</span>
    </div>
    <Toggle checked={dropIdentifiedConfig.enabled} onchange={setIdentifiedDropsEnabled} />
  </div>

  <div class="identified-quality-grid">
    <label class="identified-quality">
      <span>Magic</span>
      <Toggle checked={dropIdentifiedConfig.magic} onchange={(enabled) => setIdentifiedQuality('magic', enabled)} />
    </label>
    <label class="identified-quality">
      <span>Rare</span>
      <Toggle checked={dropIdentifiedConfig.rare} onchange={(enabled) => setIdentifiedQuality('rare', enabled)} />
    </label>
    <label class="identified-quality">
      <span>Set</span>
      <Toggle checked={dropIdentifiedConfig.set} onchange={(enabled) => setIdentifiedQuality('set', enabled)} />
    </label>
    <label class="identified-quality">
      <span>Unique</span>
      <Toggle checked={dropIdentifiedConfig.unique} onchange={(enabled) => setIdentifiedQuality('unique', enabled)} />
    </label>
    <label class="identified-quality">
      <span>Small Charms</span>
      <Toggle checked={dropIdentifiedConfig.smallCharm} onchange={(enabled) => setIdentifiedCharm('smallCharm', enabled)} />
    </label>
    <label class="identified-quality">
      <span>Large Charms</span>
      <Toggle checked={dropIdentifiedConfig.largeCharm} onchange={(enabled) => setIdentifiedCharm('largeCharm', enabled)} />
    </label>
    <label class="identified-quality">
      <span>Grand Charms</span>
      <Toggle checked={dropIdentifiedConfig.grandCharm} onchange={(enabled) => setIdentifiedCharm('grandCharm', enabled)} />
    </label>
  </div>
  </div>
  {/if}
</div>

<style>
  .identified-drops-panel {
    display: grid;
    gap: 12px;
  }

  .hook-card {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
    padding: 14px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
  }

  .hook-status {
    display: flex;
    align-items: center;
    gap: 10px;
    min-width: 0;
    color: var(--text-secondary);
    font-size: 13px;
  }

  .hook-status div {
    display: flex;
    min-width: 0;
    flex-direction: column;
    gap: 3px;
  }

  .hook-status strong {
    color: var(--text-primary);
  }

  .hook-status span:last-child {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .status-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: var(--text-muted);
    flex-shrink: 0;
  }

  .status-dot.active {
    background: #4aaa3e;
    box-shadow: 0 0 6px #4aaa3e;
  }

  .hook-actions {
    display: flex;
    flex-wrap: wrap;
    justify-content: flex-end;
    gap: 8px;
    flex-shrink: 0;
  }

  .project-d2-picker {
    display: grid;
    grid-template-columns: minmax(0, 1fr) auto;
    align-items: end;
    gap: var(--space-3);
    padding: 12px;
    border: 1px solid var(--border-primary);
    border-radius: 8px;
    background: var(--bg-secondary);
  }

  .project-d2-field {
    display: flex;
    flex-direction: column;
    gap: 6px;
    color: var(--text-secondary);
    font-size: 13px;
  }

  .project-d2-field input {
    width: 100%;
    padding: 8px 10px;
    border: 1px solid var(--border-primary);
    border-radius: 6px;
    background: var(--bg-primary);
    color: var(--text-primary);
  }

  .project-d2-actions {
    display: flex;
    gap: var(--space-2);
    flex-wrap: wrap;
    justify-content: flex-end;
  }

  .identified-options {
    display: grid;
    gap: 10px;
  }

  .identified-options .setting-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-3);
    padding: 10px;
    border: 1px solid var(--border-primary);
    border-radius: 6px;
    background: var(--bg-secondary);
  }

  .identified-options .setting-info {
    display: grid;
    gap: 2px;
  }

  .identified-options .setting-label {
    color: var(--text-primary);
    font-weight: 700;
  }

  .identified-quality-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: 8px;
  }

  .identified-quality {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-2);
    min-height: 38px;
    padding: 8px 10px;
    border: 1px solid var(--border-primary);
    border-radius: 6px;
    background: var(--bg-secondary);
    color: var(--text-primary);
    font-weight: 600;
  }

  @media (max-width: 760px) {
    .hook-card,
    .project-d2-picker {
      display: grid;
      grid-template-columns: 1fr;
    }

    .hook-actions,
    .project-d2-actions {
      justify-content: flex-start;
    }

    .hook-status span:last-child {
      white-space: normal;
    }
  }
</style>
