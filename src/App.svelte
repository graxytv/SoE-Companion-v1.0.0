<script lang="ts">
  import { invoke } from '@tauri-apps/api/core';
  import { listen } from '@tauri-apps/api/event';
  import { getCurrentWebviewWindow } from '@tauri-apps/api/webviewWindow';
  import { onMount } from 'svelte';
  import {
    MainWindow,
    MulingBannerWindow,
    OverlayWindow,
    TrackerOverlayCardWindow,
  } from './views';
  import { settingsStore } from './stores';
  import { OVERLAY_LAYOUT_WINDOW_LABELS, isOverlayLayoutWindowLabel } from './lib/overlay-layout';

  // Determine which window we're in
  type WindowType =
    | 'main'
    | 'overlay'
    | 'tracker-overlay'
    | 'muling-banner'
    | 'tracker-card-overlay'
    | null;

  let windowType = $state<WindowType>(null);
  let isReady = $state(false);
  let overlayLayoutEditing = $state(false);
  let overlayEditorWindowsHidden = false;

  function isUtilityOverlayWindow(): boolean {
    return windowType === 'muling-banner' ||
      windowType === 'tracker-card-overlay';
  }

  function syncMulingBannerWindow(visible: boolean): void {
    if (!windowType || isUtilityOverlayWindow()) return;
    invoke('set_muling_banner_window_visible', { visible }).catch((err) => {
      console.error('[App] Failed to sync muling banner window:', err);
    });
  }


  function syncOverlayEditorWindows(): void {
    if (!windowType || isUtilityOverlayWindow()) return;
    if (overlayLayoutEditing) return;
    if (overlayEditorWindowsHidden) return;

    overlayEditorWindowsHidden = true;
    for (const label of OVERLAY_LAYOUT_WINDOW_LABELS) {
      invoke('set_overlay_editor_window_visible', {
        label,
        visible: false,
      }).catch((err) => {
        console.error(`[App] Failed to hide overlay editor window ${label}:`, err);
      });
    }
  }

  $effect(() => {
    if (!isReady || !settingsStore.isLoaded) return;
    syncMulingBannerWindow(
      settingsStore.settings.dropsTrackerMulingMode &&
      settingsStore.settings.mulingIndicatorOverlayEnabled,
    );
    syncOverlayEditorWindows();
    invoke('set_always_show_overlays', { enabled: settingsStore.settings.alwaysShowOverlays }).catch((err) => {
      console.error('[App] Failed to sync always-show overlays:', err);
    });
  });

  onMount(async () => {
    const current = getCurrentWebviewWindow();
    const resolvedWindowType = current.label === 'overlay'
      ? 'overlay'
      : current.label === 'tracker-overlay'
        ? 'tracker-overlay'
        : current.label === 'muling-banner'
          ? 'muling-banner'
          : isOverlayLayoutWindowLabel(current.label)
              ? 'tracker-card-overlay'
              : 'main';
    windowType = resolvedWindowType;

    // Add class to html element for overlay styling
    if (windowType === 'overlay') {
      document.documentElement.classList.add('overlay-mode');
      document.body.style.background = 'transparent';
    } else if (windowType === 'tracker-overlay') {
      document.documentElement.classList.add('tracker-overlay-mode');
    } else if (windowType === 'muling-banner') {
      document.documentElement.classList.add('muling-banner-mode');
      document.body.style.background = 'transparent';
    } else if (windowType === 'tracker-card-overlay') {
      document.documentElement.classList.add('overlay-editor-window-mode');
      document.body.style.background = 'transparent';
    }

    // Desktop-feel: suppress the browser context menu except inside the
    // rules editor, inputs/textareas, and the DSL help content where users
    // legitimately need copy/paste.
    window.addEventListener('contextmenu', (e: MouseEvent) => {
      const target = e.target as HTMLElement | null;
      if (!target) return;
      if (
        target.closest('.cm-editor') ||
        target.closest('input') ||
        target.closest('textarea') ||
        target.closest('.syntax-help') ||
        target.closest('.help-content')
      ) {
        return;
      }
      e.preventDefault();
    });

    // Load settings from backend (applies theme automatically)
    await settingsStore.load();
    // Cross-window sync: each webview has its own store instance, so without
    // this a change in one window would be clobbered by a stale save from the other.
    await settingsStore.initSync();
    isReady = true;

    const unlistenEditMode = await listen<{ active: boolean }>('overlay-edit-mode', (event) => {
      overlayLayoutEditing = event.payload.active;
      if (event.payload.active) {
        overlayEditorWindowsHidden = false;
      }
    });

    window.setInterval(() => {
      syncMulingBannerWindow(
        settingsStore.settings.dropsTrackerMulingMode &&
        settingsStore.settings.mulingIndicatorOverlayEnabled,
      );
      syncOverlayEditorWindows();
    }, 5000);

    void unlistenEditMode;
  });
</script>

{#if isReady && windowType === 'overlay'}
  <OverlayWindow mode="game" />
{:else if isReady && windowType === 'tracker-overlay'}
  <OverlayWindow mode="tracker" />
{:else if isReady && windowType === 'muling-banner'}
  <MulingBannerWindow />
{:else if isReady && windowType === 'tracker-card-overlay'}
  <TrackerOverlayCardWindow />
{:else if isReady && windowType === 'main'}
  <MainWindow />
{/if}
