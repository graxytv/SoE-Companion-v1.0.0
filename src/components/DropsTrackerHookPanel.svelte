<script lang="ts">
  import { invoke } from '@tauri-apps/api/core';
  import { onMount } from 'svelte';
  import { Button } from '.';
  import { settingsStore } from '../stores';

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
    currentDllHash: string | null;
    bundledDllHash: string;
    message: string;
  }

  let dropHookStatus = $state<DropHookStatus | null>(null);
  let hookBusy = $state(false);
  let hookMessage = $state('');
  let projectD2PathDraft = $state(settingsStore.settings.projectD2Path ?? '');

  onMount(() => {
    void refreshHookStatus();
  });

  async function refreshHookStatus(): Promise<void> {
    hookBusy = true;
    try {
      dropHookStatus = await invoke<DropHookStatus>('get_drop_hook_status_for_path', {
        projectD2Dir: settingsStore.settings.projectD2Path,
      });
      if (!settingsStore.settings.projectD2Path && dropHookStatus.projectD2DirExists) {
        settingsStore.setProjectD2Path(dropHookStatus.projectD2Dir);
        projectD2PathDraft = dropHookStatus.projectD2Dir;
      }
      hookMessage = '';
    } catch (error) {
      hookMessage = `Could not check Drops Tracker Hook: ${error}`;
    } finally {
      hookBusy = false;
    }
  }

  async function autoDetectProjectD2Folder(): Promise<void> {
    hookBusy = true;
    hookMessage = 'Searching common ProjectD2 folders...';
    try {
      const paths = await invoke<string[]>('detect_project_d2_dirs');
      const first = paths[0] ?? '';
      if (!first) {
        hookMessage = 'No ProjectD2 folder found in common install locations. Paste your ProjectD2 folder path below.';
        return;
      }
      projectD2PathDraft = first;
      settingsStore.setProjectD2Path(first);
      await refreshHookStatus();
    } catch (error) {
      hookMessage = `ProjectD2 detection failed: ${error}`;
    } finally {
      hookBusy = false;
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
    await refreshHookStatus();
  }

  async function saveProjectD2Folder(): Promise<void> {
    settingsStore.setProjectD2Path(projectD2PathDraft.trim() || null);
    await refreshHookStatus();
  }

  async function installHook(): Promise<void> {
    hookBusy = true;
    hookMessage = dropHookStatus?.hookNeedsUpdate ? 'Updating Drops Tracker Hook...' : 'Installing Drops Tracker Hook...';
    try {
      dropHookStatus = await invoke<DropHookStatus>('install_drop_hook_for_path', {
        projectD2Dir: settingsStore.settings.projectD2Path,
      });
      if (!settingsStore.settings.projectD2Path && dropHookStatus.projectD2DirExists) {
        settingsStore.setProjectD2Path(dropHookStatus.projectD2Dir);
        projectD2PathDraft = dropHookStatus.projectD2Dir;
      }
      hookMessage = dropHookStatus.message;
    } catch (error) {
      hookMessage = `Install failed: ${error}`;
      await refreshHookStatus();
    } finally {
      hookBusy = false;
    }
  }

  async function restoreOriginalDll(): Promise<void> {
    const confirmed = window.confirm(
      'Restore the original ijl11.dll backup and disable SoE Companion hook features? Close Diablo II before continuing.',
    );
    if (!confirmed) return;
    hookBusy = true;
    hookMessage = 'Restoring original ijl11.dll...';
    try {
      dropHookStatus = await invoke<DropHookStatus>('restore_original_ijl11_for_path', {
        projectD2Dir: settingsStore.settings.projectD2Path,
      });
      hookMessage = dropHookStatus.message;
    } catch (error) {
      hookMessage = `Restore failed: ${error}`;
      await refreshHookStatus();
    } finally {
      hookBusy = false;
    }
  }

  function statusMessage(): string {
    if (hookMessage) return hookMessage;
    if (!dropHookStatus) return 'Checking Drops Tracker Hook status...';
    if (!dropHookStatus.projectD2DirExists) return 'ProjectD2 folder was not found.';
    if (dropHookStatus.hookNeedsUpdate) {
      return 'Drops Tracker Hook is installed but needs an update for this app version.';
    }
    if (dropHookStatus.unknownDllPresent) {
      return 'An ijl11.dll is present, but it is not recognized as a SoE Companion hook. Installing will back it up first.';
    }
    if (dropHookStatus.grailInstalled) {
      return dropHookStatus.message || 'Drops Tracker Hook is installed.';
    }
    if (dropHookStatus.installed) {
      return 'Shared SoE hook is present, but Drops Tracker Hook is not enabled.';
    }
    return 'Drops Tracker Hook is required for SoE Companion drop tracking.';
  }

  function statusLabel(): string {
    if (!dropHookStatus) return 'Checking';
    if (dropHookStatus.hookNeedsUpdate) return 'Update Required';
    if (dropHookStatus.unknownDllPresent) return 'Unknown ijl11.dll';
    if (dropHookStatus.grailInstalled) return 'Installed';
    return 'Not Installed';
  }

  function installButtonLabel(): string {
    if (dropHookStatus?.hookNeedsUpdate) return 'Update Drops Tracker Hook';
    if (dropHookStatus?.unknownDllPresent) return 'Replace with Drops Tracker Hook';
    if (dropHookStatus?.grailInstalled) return 'Installed';
    return 'Install Drops Tracker Hook';
  }

  function hookDllStatus(): string {
    if (!dropHookStatus?.dllExists) return 'Missing';
    if (dropHookStatus.hookNeedsUpdate) return 'Needs Update';
    if (dropHookStatus.unknownDllPresent) return 'Unknown';
    if (dropHookStatus.dllMatchesBundled) return 'Current';
    return 'Present';
  }
</script>

<div class="settings-section drops-hook-panel">
  <div class="panel-heading">
    <div>
      <h2 class="section-title">Install Drops Tracker Hook</h2>
      <p class="section-description">
        Required for accurate drop tracking, Holy Grail updates, Fate Cards, material/rune counts, and tracker overlays. Close Diablo II before installing or changing hook settings.
      </p>
    </div>
    <span class="required-badge">Required</span>
  </div>

  <div class="hook-card">
    <div class="hook-status">
      <span
        class="status-dot"
        class:active={dropHookStatus?.grailInstalled}
        class:warning={dropHookStatus?.hookNeedsUpdate || dropHookStatus?.unknownDllPresent}
      ></span>
      <div>
        <strong>{statusLabel()}</strong>
        <span>{statusMessage()}</span>
      </div>
    </div>
    <div class="hook-actions">
      <Button variant="primary" size="sm" disabled={hookBusy || (dropHookStatus?.grailInstalled && !dropHookStatus?.hookNeedsUpdate)} onclick={installHook}>
        {installButtonLabel()}
      </Button>
      {#if dropHookStatus?.canRestoreOriginal}
        <Button variant="danger" size="sm" disabled={hookBusy} onclick={restoreOriginalDll}>Restore Original ijl11.dll</Button>
      {/if}
      <Button variant="secondary" size="sm" disabled={hookBusy} onclick={refreshHookStatus}>Refresh Status</Button>
    </div>
  </div>

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
      <Button variant="secondary" size="sm" disabled={hookBusy} onclick={autoDetectProjectD2Folder}>Auto Detect</Button>
      <Button variant="secondary" size="sm" disabled={hookBusy} onclick={chooseProjectD2Folder}>Select Folder</Button>
      <Button variant="primary" size="sm" disabled={hookBusy} onclick={saveProjectD2Folder}>Use Folder</Button>
    </div>
  </div>

  <div class="hook-detail-grid">
    <div><span>ProjectD2 Folder</span><strong>{dropHookStatus?.projectD2DirExists ? 'Found' : 'Missing'}</strong></div>
    <div><span>Hook DLL</span><strong>{hookDllStatus()}</strong></div>
    <div><span>Hook Version</span><strong>{dropHookStatus?.hookVersionMatches ? 'Current' : `Expected ${dropHookStatus?.expectedHookVersion ?? '-'}`}</strong></div>
    <div><span>Original DLL Backup</span><strong>{dropHookStatus?.originalDllExists ? 'Found' : 'Missing'}</strong></div>
    <div><span>Config File</span><strong>{dropHookStatus?.iniExists ? 'Found' : 'Missing'}</strong></div>
    <div><span>Drop Event Log</span><strong>{dropHookStatus?.logExists ? `${dropHookStatus.logSize} bytes` : 'No drops yet'}</strong></div>
  </div>

  <p class="hook-note">
    Identified Drops can be enabled separately in the Identified Drops sub-tab after the required Drops Tracker Hook is installed.
  </p>
</div>

<style>
  .drops-hook-panel {
    display: grid;
    gap: 12px;
  }

  .panel-heading {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    gap: 16px;
  }

  .required-badge {
    padding: 4px 8px;
    border: 1px solid var(--notify-gold);
    border-radius: 4px;
    color: var(--notify-gold);
    font-size: 11px;
    font-weight: 800;
    text-transform: uppercase;
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

  .status-dot.warning {
    background: var(--notify-gold);
    box-shadow: 0 0 6px var(--notify-gold);
  }

  .hook-actions,
  .project-d2-actions {
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

  .hook-detail-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
    gap: 10px;
  }

  .hook-detail-grid div {
    display: grid;
    gap: 3px;
    padding: 10px;
    border: 1px solid var(--border-primary);
    border-radius: 6px;
    background: var(--bg-secondary);
  }

  .hook-detail-grid span,
  .hook-note {
    color: var(--text-secondary);
    font-size: 12px;
  }

  .hook-detail-grid strong {
    color: var(--text-primary);
  }

  .hook-note {
    margin: 0;
  }

  @media (max-width: 760px) {
    .panel-heading,
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
