<script lang="ts">
  import { invoke } from '@tauri-apps/api/core';
  import { LogicalSize } from '@tauri-apps/api/dpi';
  import { emit, listen } from '@tauri-apps/api/event';
  import { getCurrentWebviewWindow } from '@tauri-apps/api/webviewWindow';
  import { onMount } from 'svelte';
  import Notification from '../components/Notification.svelte';
  import {
    DROP_TRACKER_CATEGORIES,
    RUNE_NAMES,
    categoryLabel,
    countTotal,
    runeCategory,
    type DropTrackerCategoryKey,
    type DropTrackerCounts,
  } from '../lib/drop-tracker-categories';
  import {
    HOLY_GRAIL_CATEGORIES,
    buildHolyGrailItems,
    holyGrailCategoryLabel,
    holyGrailCategoryProgress,
    holyGrailProgress,
    mostRecentHolyGrailFind,
  } from '../lib/holy-grail';
  import { evaluateAchievements, unlockedAchievementCount } from '../lib/achievements';
  import { MATERIAL_TRACKER_NAMES } from '../lib/material-tracker';
  import { overlayLayoutKindFromWindowLabel, type OverlayLayoutKind } from '../lib/overlay-layout';
  import { fateCardTrackerRows } from '../lib/soe-13-items';
  import { itemsDictionaryStore, settingsStore, type DropTrackerStateSnapshot, type OverlayPosition } from '../stores';

  interface GameWindowRect {
    left: number;
    top: number;
    width: number;
    height: number;
  }

  const TRACKER_TIMER_CARD_REFRESH_MS = 5_000;

  const window = getCurrentWebviewWindow();
  const kind = overlayLayoutKindFromWindowLabel(window.label);
  const sampleNotificationItem = {
    unit_id: -1,
    class: 0,
    quality: 'Unique',
    name: 'Sample Unique Drop',
    base_name: 'Gilded Shield',
    stats: '',
    is_ethereal: false,
    is_identified: true,
  };

  let overlayRefreshCounter = 0;
  let overlayRefreshNonce = $state(0);
  let notificationFontSize = $derived(settingsStore.settings.notificationFontSize);
  let notificationOpacity = $derived(settingsStore.settings.notificationOpacity);
  let notificationOverlayEnabled = $derived(settingsStore.settings.notificationOverlayEnabled);
  let notificationWidth = $derived(settingsStore.settings.notificationWidth);
  let notificationHeight = $derived(settingsStore.settings.notificationHeight);
  let dropsTrackerCounts = $derived(settingsStore.settings.dropsTrackerCounts);
  let totalDropsTrackerCounts = $derived(settingsStore.settings.totalDropsTrackerCounts);
  let dropsTrackerEnabled = $derived(settingsStore.settings.dropsTrackerEnabled);
  let totalDropsTrackerEnabled = $derived(settingsStore.settings.totalDropsTrackerEnabled);
  let dropsTrackerCategories = $derived(settingsStore.settings.dropsTrackerCategories);
  let totalDropsTrackerCategories = $derived(settingsStore.settings.totalDropsTrackerCategories);
  let dropsTrackerRunCounterEnabled = $derived(settingsStore.settings.dropsTrackerRunCounterEnabled);
  let dropsTrackerRunTimerEnabled = $derived(settingsStore.settings.dropsTrackerRunTimerEnabled);
  let dropsTrackerSessionTimerEnabled = $derived(settingsStore.settings.dropsTrackerSessionTimerEnabled);
  let dropsTrackerRunCount = $derived(settingsStore.settings.dropsTrackerRunCount);
  let timerNow = $state(Date.now());
  let dropsTrackerRunElapsedMs = $derived(settingsStore.getDropsTrackerRunDisplayElapsedMs(timerNow));
  let dropsTrackerSessionElapsedMs = $derived(settingsStore.getDropsTrackerSessionDisplayElapsedMs(timerNow));
  let holyGrailFound = $derived(settingsStore.settings.holyGrailFound);
  let holyGrailOverlayShowTotal = $derived(settingsStore.settings.holyGrailOverlayShowTotal);
  let holyGrailOverlayShowLatest = $derived(settingsStore.settings.holyGrailOverlayShowLatest);
  let holyGrailOverlayCategories = $derived(settingsStore.settings.holyGrailOverlayCategories);
  let runeTrackerCounts = $derived(settingsStore.settings.runeTrackerCounts);
  let runeTrackerOverlayRunes = $derived(settingsStore.settings.runeTrackerOverlayRunes);
  let materialTrackerCounts = $derived(settingsStore.settings.materialTrackerCounts);
  let materialTrackerOverlayEnabled = $derived(settingsStore.settings.materialTrackerOverlayEnabled);
  let materialTrackerOverlayMaterials = $derived(settingsStore.settings.materialTrackerOverlayMaterials);
  let materialTrackerRows = $derived(
    MATERIAL_TRACKER_NAMES
      .filter((material) => materialTrackerOverlayMaterials[material])
      .map((material) => ({ key: material, label: material, count: materialTrackerCounts[material] ?? 0 })),
  );
  let runeTrackerRows = $derived(
    RUNE_NAMES
      .filter((rune) => runeTrackerOverlayRunes[rune])
      .map((rune) => ({ key: rune, label: rune, tier: runeCategory(rune), count: runeTrackerCounts[rune] ?? 0 })),
  );
  let runeTrackerOverlayEnabled = $derived(settingsStore.settings.runeTrackerOverlayEnabled);
  let fateCardTrackerCounts = $derived(settingsStore.settings.fateCardTrackerCounts);
  let fateCardTrackerOverlayEnabled = $derived(settingsStore.settings.fateCardTrackerOverlayEnabled);
  let fateCardTrackerOverlayCards = $derived(settingsStore.settings.fateCardTrackerOverlayCards);
  let fateCardTrackerOverlayTiers = $derived(settingsStore.settings.fateCardTrackerOverlayTiers);
  let fateCardRows = $derived(
    fateCardTrackerRows(fateCardTrackerCounts, fateCardTrackerOverlayTiers, fateCardTrackerOverlayCards),
  );
  let holyGrailItems = $derived(buildHolyGrailItems(itemsDictionaryStore.dict));
  let holyGrailOverlayEnabled = $derived(settingsStore.settings.holyGrailOverlayEnabled);
  let holyGrailTotalProgress = $derived(holyGrailProgress(holyGrailItems, holyGrailFound));
  let holyGrailLatestFind = $derived(mostRecentHolyGrailFind(holyGrailFound));
  let achievementStats = $derived(settingsStore.settings.achievementStats);
  let achievementProgress = $derived(evaluateAchievements({
    stats: achievementStats,
    holyGrailFound,
    holyGrailItems,
    runeTrackerCounts,
  }));
  let achievementUnlockedCount = $derived(unlockedAchievementCount(achievementProgress));
  let achievementTotalCount = $derived(achievementProgress.length);
  let achievementPercent = $derived(achievementTotalCount > 0 ? (achievementUnlockedCount / achievementTotalCount) * 100 : 0);
  let achievementProgressOverlayEnabled = $derived(settingsStore.settings.achievementProgressOverlayEnabled);
  let achievementSettings = $derived(settingsStore.settings.achievementSettings);
  let achievementPopupOverlayWidth = $derived(settingsStore.settings.achievementPopupOverlayWidth);
  let achievementPopupOverlayHeight = $derived(settingsStore.settings.achievementPopupOverlayHeight);
  let monsterKillsTotal = $derived(achievementStats.totalKills ?? 0);
  let monsterKillsOverlayEnabled = $derived(settingsStore.settings.monsterKillsOverlayEnabled);
  let dropsTrackerMulingMode = $derived(settingsStore.settings.dropsTrackerMulingMode);
  let mulingIndicatorOverlayEnabled = $derived(settingsStore.settings.mulingIndicatorOverlayEnabled);
  let mulingModeHotkey = $derived(settingsStore.settings.mulingModeHotkey);
  let overlayEnabled = $derived(isOverlayEnabled(kind));
  let timerRowsVisible = $derived(
    kind === 'drops' && overlayEnabled && (dropsTrackerRunTimerEnabled || dropsTrackerSessionTimerEnabled),
  );
  let dropsTrackerRows = $derived(visibleCategoryRows(dropsTrackerCounts, dropsTrackerCategories, overlayRefreshNonce));
  let totalDropsTrackerRows = $derived(visibleCategoryRows(totalDropsTrackerCounts, totalDropsTrackerCategories, overlayRefreshNonce));
  let totalDropsTrackerTotal = $derived(countTotal(totalDropsTrackerCounts, totalDropsTrackerCategories) + overlayRefreshNonce * 0);
  let dragState = $state<null | {
    pointerId: number;
    startScreenX: number;
    startScreenY: number;
    startWindowX: number;
    startWindowY: number;
    gameRect: GameWindowRect;
    scaleFactor: number;
    windowWidth: number;
    windowHeight: number;
  }>(null);
  let resizeState = $state<null | {
    pointerId: number;
    startScreenX: number;
    startScreenY: number;
    startWidth: number;
    startHeight: number;
    latestWidth: number;
    latestHeight: number;
    gameRect: GameWindowRect;
    scaleFactor: number;
    windowX: number;
    windowY: number;
  }>(null);
  let lastLivePersistAt = 0;

  function nudgeRender(): void {
    overlayRefreshCounter = (overlayRefreshCounter + 1) % 1_000_000;
    overlayRefreshNonce = overlayRefreshCounter;
  }

  function visibleCategoryRows(counts: DropTrackerCounts, enabled: Record<DropTrackerCategoryKey, boolean>, _refreshNonce = 0) {
    return DROP_TRACKER_CATEGORIES
      .filter((category) => enabled[category.key])
      .map((category) => ({
        key: category.key,
        label: categoryLabel(category.key),
        count: counts[category.key] ?? 0,
      }));
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

  function isOverlayEnabled(nextKind: OverlayLayoutKind): boolean {
    if (nextKind === 'notifications') return notificationOverlayEnabled;
    if (nextKind === 'drops') return dropsTrackerEnabled;
    if (nextKind === 'total') return totalDropsTrackerEnabled;
    if (nextKind === 'grail') return holyGrailOverlayEnabled;
    if (nextKind === 'materials') return materialTrackerOverlayEnabled;
    if (nextKind === 'runes') return runeTrackerOverlayEnabled;
    if (nextKind === 'fate-cards') return fateCardTrackerOverlayEnabled;
    if (nextKind === 'achievements') return achievementProgressOverlayEnabled;
    if (nextKind === 'achievement-popup') return achievementSettings.overlayEnabled;
    if (nextKind === 'kills') return monsterKillsOverlayEnabled;
    return mulingIndicatorOverlayEnabled;
  }

  function persistPosition(nextKind: OverlayLayoutKind, position: OverlayPosition, gameRect: GameWindowRect, scaleFactor: number): void {
    if (nextKind === 'notifications') {
      const gameSize = logicalGameSize(gameRect, scaleFactor);
      const nextX = gameSize.width > 0 ? (Number(position.x ?? 0) / gameSize.width) * 100 : 0;
      const nextY = gameSize.height > 0 ? (Number(position.y ?? 0) / gameSize.height) * 100 : 0;
      settingsStore.setNotificationPosition(
        Math.max(0, Math.min(100, Math.round(nextX * 10) / 10)),
        Math.max(0, Math.min(100, Math.round(nextY * 10) / 10)),
      );
    } else if (nextKind === 'drops') {
      settingsStore.setDropsTrackerOverlayPosition(position);
    } else if (nextKind === 'total') {
      settingsStore.setTotalDropsOverlayPosition(position);
    } else if (nextKind === 'grail') {
      settingsStore.setHolyGrailOverlayPosition(position);
    } else if (nextKind === 'materials') {
      settingsStore.setMaterialTrackerOverlayPosition(position);
    } else if (nextKind === 'runes') {
      settingsStore.setRuneTrackerOverlayPosition(position);
    } else if (nextKind === 'fate-cards') {
      settingsStore.setFateCardTrackerOverlayPosition(position);
    } else if (nextKind === 'achievements') {
      settingsStore.setAchievementProgressOverlayPosition(position);
    } else if (nextKind === 'achievement-popup') {
      settingsStore.setAchievementPopupOverlayPosition(position);
    } else if (nextKind === 'kills') {
      settingsStore.setMonsterKillsOverlayPosition(position);
    } else {
      settingsStore.setMulingIndicatorOverlayPosition(position);
    }
  }

  function sizeBounds(_nextKind: OverlayLayoutKind): { minWidth: number; maxWidth: number; minHeight: number; maxHeight: number } {
    return { minWidth: 1, maxWidth: 4000, minHeight: 1, maxHeight: 4000 };
  }

  function clampSize(nextKind: OverlayLayoutKind, width: number, height: number): { width: number; height: number } {
    const bounds = sizeBounds(nextKind);
    return {
      width: Math.max(bounds.minWidth, Math.min(bounds.maxWidth, Math.round(width))),
      height: Math.max(bounds.minHeight, Math.min(bounds.maxHeight, Math.round(height))),
    };
  }

  function safeScaleFactor(scaleFactor: number): number {
    return Number.isFinite(scaleFactor) && scaleFactor > 0 ? scaleFactor : 1;
  }

  function logicalGameSize(gameRect: GameWindowRect, scaleFactor: number): Pick<GameWindowRect, 'width' | 'height'> {
    const scale = safeScaleFactor(scaleFactor);
    return {
      width: Math.max(1, Math.round(gameRect.width / scale)),
      height: Math.max(1, Math.round(gameRect.height / scale)),
    };
  }

  function relativeLogicalPosition(screenX: number, screenY: number, gameRect: GameWindowRect, scaleFactor: number): OverlayPosition {
    const scale = safeScaleFactor(scaleFactor);
    return {
      x: Math.round((screenX - gameRect.left) / scale),
      y: Math.round((screenY - gameRect.top) / scale),
    };
  }

  function currentViewportSize(): { width: number; height: number } {
    return clampSize(
      kind,
      globalThis.innerWidth || 1,
      globalThis.innerHeight || 1,
    );
  }

  function persistSize(nextKind: OverlayLayoutKind, width: number, height: number): void {
    const next = clampSize(nextKind, width, height);
    if (nextKind === 'notifications') {
      settingsStore.set('notificationWidth', next.width);
      settingsStore.set('notificationHeight', next.height);
    } else if (nextKind === 'drops') {
      settingsStore.setDropsTrackerOverlayWidth(next.width);
      settingsStore.setDropsTrackerOverlayHeight(next.height);
    } else if (nextKind === 'total') {
      settingsStore.setTotalDropsOverlayWidth(next.width);
      settingsStore.setTotalDropsOverlayHeight(next.height);
    } else if (nextKind === 'grail') {
      settingsStore.setHolyGrailOverlayWidth(next.width);
      settingsStore.setHolyGrailOverlayHeight(next.height);
    } else if (nextKind === 'materials') {
      settingsStore.setMaterialTrackerOverlayWidth(next.width);
      settingsStore.setMaterialTrackerOverlayHeight(next.height);
    } else if (nextKind === 'runes') {
      settingsStore.setRuneTrackerOverlayWidth(next.width);
      settingsStore.setRuneTrackerOverlayHeight(next.height);
    } else if (nextKind === 'fate-cards') {
      settingsStore.setFateCardTrackerOverlayWidth(next.width);
      settingsStore.setFateCardTrackerOverlayHeight(next.height);
    } else if (nextKind === 'achievements') {
      settingsStore.setAchievementProgressOverlayWidth(next.width);
      settingsStore.setAchievementProgressOverlayHeight(next.height);
    } else if (nextKind === 'achievement-popup') {
      settingsStore.setAchievementPopupOverlayWidth(next.width);
      settingsStore.setAchievementPopupOverlayHeight(next.height);
    } else if (nextKind === 'kills') {
      settingsStore.setMonsterKillsOverlayWidth(next.width);
      settingsStore.setMonsterKillsOverlayHeight(next.height);
    } else {
      settingsStore.setMulingIndicatorOverlayWidth(next.width);
      settingsStore.setMulingIndicatorOverlayHeight(next.height);
    }
  }

  function clampScreenPosition(screenX: number, screenY: number, gameRect: GameWindowRect, width: number, height: number): { x: number; y: number } {
    const maxX = gameRect.left + Math.max(0, gameRect.width - width);
    const maxY = gameRect.top + Math.max(0, gameRect.height - height);
    return {
      x: Math.max(gameRect.left, Math.min(maxX, Math.round(screenX))),
      y: Math.max(gameRect.top, Math.min(maxY, Math.round(screenY))),
    };
  }

  function emitPositionPreview(position: OverlayPosition, gameRect: GameWindowRect, scaleFactor: number, width?: number, height?: number): void {
    const gameSize = logicalGameSize(gameRect, scaleFactor);
    void emit('overlay-layout-position-preview', {
      kind,
      x: position.x ?? 0,
      y: position.y ?? 0,
      width,
      height,
      gameWidth: gameSize.width,
      gameHeight: gameSize.height,
    });
  }

  function persistLivePosition(screenX: number, screenY: number, force = false): void {
    if (!dragState) return;
    const clamped = clampScreenPosition(
      screenX,
      screenY,
      dragState.gameRect,
      dragState.windowWidth,
      dragState.windowHeight,
    );
    const position = relativeLogicalPosition(clamped.x, clamped.y, dragState.gameRect, dragState.scaleFactor);
    emitPositionPreview(position, dragState.gameRect, dragState.scaleFactor);

    const now = performance.now();
    if (force || now - lastLivePersistAt > 80) {
      lastLivePersistAt = now;
      persistPosition(kind, position, dragState.gameRect, dragState.scaleFactor);
    }
  }

  async function saveCurrentPosition(): Promise<void> {
    try {
      const [windowPosition, gameRect, scaleFactor] = await Promise.all([
        window.outerPosition(),
        invoke<GameWindowRect>('get_game_window_rect'),
        window.scaleFactor(),
      ]);
      const position = relativeLogicalPosition(windowPosition.x, windowPosition.y, gameRect, scaleFactor);
      emitPositionPreview(position, gameRect, scaleFactor);
      persistPosition(kind, position, gameRect, scaleFactor);
    } catch (err) {
      console.error('[TrackerOverlayCardWindow] Failed to save overlay position:', err);
    }
  }

  async function startDrag(event: PointerEvent): Promise<void> {
    if (event.button !== 0) return;
    event.preventDefault();
    const target = event.currentTarget as HTMLElement | null;
    try {
      target?.setPointerCapture(event.pointerId);
    } catch {
      // Best effort; document-level listeners continue the drag if capture is unavailable.
    }

    try {
      const [position, gameRect, size, scaleFactor] = await Promise.all([
        window.outerPosition(),
        invoke<GameWindowRect>('get_game_window_rect'),
        window.outerSize(),
        window.scaleFactor(),
      ]);
      dragState = {
        pointerId: event.pointerId,
        startScreenX: event.screenX,
        startScreenY: event.screenY,
        startWindowX: position.x,
        startWindowY: position.y,
        gameRect,
        scaleFactor,
        windowWidth: size.width,
        windowHeight: size.height,
      };
      persistLivePosition(position.x, position.y, true);
    } catch (err) {
      console.error('[TrackerOverlayCardWindow] Failed to start manual drag:', err);
    }
  }

  function moveDrag(event: PointerEvent): void {
    if (resizeState) return;
    if (!dragState) return;
    event.preventDefault();
    const unclampedX = Math.round(dragState.startWindowX + event.screenX - dragState.startScreenX);
    const unclampedY = Math.round(dragState.startWindowY + event.screenY - dragState.startScreenY);
    const next = clampScreenPosition(
      unclampedX,
      unclampedY,
      dragState.gameRect,
      dragState.windowWidth,
      dragState.windowHeight,
    );
    persistLivePosition(next.x, next.y);
    invoke('move_overlay_editor_window', {
      label: window.label,
      screenX: next.x,
      screenY: next.y,
    }).catch((err) => {
      console.error('[TrackerOverlayCardWindow] Failed to move overlay window:', err);
    });
  }

  function endDrag(): void {
    if (!dragState) return;
    dragState = null;
    void saveCurrentPosition();
  }

  async function startResize(event: PointerEvent): Promise<void> {
    if (event.button !== 0) return;
    event.preventDefault();
    event.stopPropagation();
    const target = event.currentTarget as HTMLElement | null;
    try {
      target?.setPointerCapture(event.pointerId);
    } catch {
      // Best effort; document-level listeners continue the resize if capture is unavailable.
    }

    try {
      const [position, gameRect, scaleFactor] = await Promise.all([
        window.outerPosition(),
        invoke<GameWindowRect>('get_game_window_rect'),
        window.scaleFactor(),
      ]);
      const size = currentViewportSize();
      resizeState = {
        pointerId: event.pointerId,
        startScreenX: event.screenX,
        startScreenY: event.screenY,
        startWidth: size.width,
        startHeight: size.height,
        latestWidth: size.width,
        latestHeight: size.height,
        gameRect,
        scaleFactor,
        windowX: position.x,
        windowY: position.y,
      };
    } catch (err) {
      console.error('[TrackerOverlayCardWindow] Failed to start resize:', err);
    }
  }

  function resizeDrag(event: PointerEvent): void {
    if (!resizeState) return;
    event.preventDefault();
    const next = clampSize(
      kind,
      resizeState.startWidth + event.screenX - resizeState.startScreenX,
      resizeState.startHeight + event.screenY - resizeState.startScreenY,
    );
    resizeState.latestWidth = next.width;
    resizeState.latestHeight = next.height;
    window.setSize(new LogicalSize(next.width, next.height)).catch((err) => {
      console.error('[TrackerOverlayCardWindow] Failed to set logical overlay window size:', err);
    });
    invoke('resize_overlay_editor_window', {
      label: window.label,
      width: next.width,
      height: next.height,
    }).catch((err) => {
      console.error('[TrackerOverlayCardWindow] Failed to resize overlay window:', err);
    });

    const position = relativeLogicalPosition(resizeState.windowX, resizeState.windowY, resizeState.gameRect, resizeState.scaleFactor);
    emitPositionPreview(position, resizeState.gameRect, resizeState.scaleFactor, next.width, next.height);

    const now = performance.now();
    if (now - lastLivePersistAt > 80) {
      lastLivePersistAt = now;
      persistSize(kind, next.width, next.height);
    }
  }

  function endResize(): void {
    if (!resizeState) return;
    const finalState = resizeState;
    resizeState = null;
    const size = clampSize(kind, finalState.latestWidth, finalState.latestHeight);
    persistSize(kind, size.width, size.height);
    const position = relativeLogicalPosition(finalState.windowX, finalState.windowY, finalState.gameRect, finalState.scaleFactor);
    emitPositionPreview(position, finalState.gameRect, finalState.scaleFactor, size.width, size.height);
  }

  $effect(() => {
    dropsTrackerCounts;
    totalDropsTrackerCounts;
    dropsTrackerRunCount;
    holyGrailFound;
    materialTrackerCounts;
    materialTrackerOverlayMaterials;
    runeTrackerCounts;
    runeTrackerOverlayRunes;
    fateCardTrackerCounts;
    fateCardTrackerOverlayCards;
    fateCardTrackerOverlayTiers;
    achievementStats;
    dropsTrackerMulingMode;
    mulingModeHotkey;
    overlayEnabled;
    nudgeRender();
  });

  onMount(() => {
    void itemsDictionaryStore.init();
    const unlisteners: Array<() => void> = [];
    const timer = kind === 'drops'
      ? globalThis.setInterval(() => {
          if (timerRowsVisible) {
            timerNow = Date.now();
          }
        }, TRACKER_TIMER_CARD_REFRESH_MS)
      : null;
    let pendingTimerPause: number | null = null;
    const clearPendingTimerPause = () => {
      if (pendingTimerPause == null) return;
      globalThis.clearTimeout(pendingTimerPause);
      pendingTimerPause = null;
    };
    let saveTimeout: ReturnType<typeof setTimeout> | null = null;
    const debouncedSave = () => {
      if (saveTimeout) clearTimeout(saveTimeout);
      saveTimeout = setTimeout(() => {
        void saveCurrentPosition();
      }, 120);
    };

    window.onMoved(debouncedSave).then((u) => unlisteners.push(u));
    listen<string>('game-status', (event) => {
      const ingame = event.payload === 'ingame';
      if (ingame) {
        clearPendingTimerPause();
        settingsStore.setDropsTrackerTimersDisplayActive(true);
        if (kind === 'drops') timerNow = Date.now();
        return;
      }

      clearPendingTimerPause();
      pendingTimerPause = globalThis.setTimeout(() => {
        pendingTimerPause = null;
        settingsStore.setDropsTrackerTimersDisplayActive(false);
        if (kind === 'drops') timerNow = Date.now();
      }, 2500);
    }).then((u) => unlisteners.push(u));
    invoke('get_game_status').then((status: unknown) => {
      settingsStore.setDropsTrackerTimersDisplayActive(status === 'ingame');
      if (kind === 'drops') timerNow = Date.now();
    }).catch(() => {
      settingsStore.setDropsTrackerTimersDisplayActive(false);
    });

    listen<DropTrackerStateSnapshot>('drop-tracker-state-updated', (event) => {
      settingsStore.mergeDropTrackerStateSnapshot(event.payload);
      nudgeRender();
      requestAnimationFrame(() => nudgeRender());
      globalThis.setTimeout(() => nudgeRender(), 120);
    }).then((u) => unlisteners.push(u));

    listen<DropTrackerStateSnapshot['holyGrailFound']>('holy-grail-found-updated', (event) => {
      if (!event.payload) return;
      settingsStore.applyHolyGrailFoundSnapshot(event.payload);
      nudgeRender();
      requestAnimationFrame(() => nudgeRender());
      globalThis.setTimeout(() => nudgeRender(), 120);
    }).then((u) => unlisteners.push(u));

    document.addEventListener('pointermove', moveDrag);
    document.addEventListener('pointermove', resizeDrag);
    document.addEventListener('pointerup', endDrag);
    document.addEventListener('pointerup', endResize);
    document.addEventListener('pointercancel', endDrag);
    document.addEventListener('pointercancel', endResize);

    return () => {
      if (timer !== null) globalThis.clearInterval(timer);
      clearPendingTimerPause();
      if (saveTimeout) clearTimeout(saveTimeout);
      unlisteners.forEach((u) => u());
      document.removeEventListener('pointermove', moveDrag);
      document.removeEventListener('pointermove', resizeDrag);
      document.removeEventListener('pointerup', endDrag);
      document.removeEventListener('pointerup', endResize);
      document.removeEventListener('pointercancel', endDrag);
      document.removeEventListener('pointercancel', endResize);
    };
  });
</script>

<main class="card-window">
  <div
    class="tracker-card {kind}-card"
    class:disabled={!overlayEnabled}
    class:compact-stat-card={kind === 'achievements' || kind === 'kills'}
    class:muling-active={kind === 'muling' && dropsTrackerMulingMode}
    role="complementary"
    onpointerdown={startDrag}
  >
    {#if kind === 'notifications'}
      <div class="tracker-header">
        <span class="tracker-title">Notifications</span>
      </div>
      <div class="notification-preview">
        <Notification
          item={sampleNotificationItem}
          fontSize={notificationFontSize}
          opacity={notificationOpacity}
          compactName
        />
      </div>
    {:else if kind === 'muling'}
      {@const _hkLabel = (mulingModeHotkey?.keyCode ?? 0) === 0 && (mulingModeHotkey?.modifiers ?? 0) === 0 ? 'None' : (mulingModeHotkey?.display ?? 'None')}
      <span class="mi-key">{_hkLabel}</span>
      <div class="mi-mbox">M</div>
    {:else if kind === 'drops'}
      <div class="tracker-header">
        <span class="tracker-title">Drops Tracker</span>
      </div>
      <div class="tracker-rows">
        {#each dropsTrackerRows as row (row.key + '-' + overlayRefreshNonce)}
          <div class="tracker-row">
            <span class="tracker-label category-{row.key}">{row.label}</span>
            <span class="tracker-count">{row.count}</span>
          </div>
        {/each}
        {#if dropsTrackerRunCounterEnabled}
          <div class="tracker-row run-row">
            <span class="tracker-label run">Runs</span>
            <span class="tracker-count">{dropsTrackerRunCount}</span>
          </div>
        {/if}
        {#if dropsTrackerRunTimerEnabled}
          <div class="tracker-row timer-row">
            <span class="tracker-label run">Run Time</span>
            <span class="tracker-count">{formatRunTime(dropsTrackerRunElapsedMs)}</span>
          </div>
        {/if}
        {#if dropsTrackerSessionTimerEnabled}
          <div class="tracker-row timer-row">
            <span class="tracker-label run">Total Session Time</span>
            <span class="tracker-count">{formatSessionTime(dropsTrackerSessionElapsedMs)}</span>
          </div>
        {/if}
      </div>
    {:else if kind === 'total'}
      <div class="tracker-header">
        <span class="tracker-title">Total Drops</span>
      </div>
      <div class="tracker-rows">
        {#each totalDropsTrackerRows as row (row.key + '-' + overlayRefreshNonce)}
          <div class="tracker-row">
            <span class="tracker-label category-{row.key}">{row.label}</span>
            <span class="tracker-count">{row.count}</span>
          </div>
        {/each}
        <div class="tracker-row total-row">
          <span class="tracker-label total">Total</span>
          <span class="tracker-count">{totalDropsTrackerTotal}</span>
        </div>
      </div>
    {:else if kind === 'grail'}
      <div class="tracker-header">
        <span class="tracker-title">Grail Progress</span>
      </div>
      <div class="tracker-rows">
        {#if holyGrailOverlayShowTotal}
          <div class="tracker-row grail-total-row">
            <span class="tracker-label grail-total">{holyGrailTotalProgress.percent.toFixed(1)}% Complete</span>
            <span class="tracker-count">{holyGrailTotalProgress.found}/{holyGrailTotalProgress.total}</span>
          </div>
        {/if}
        {#each HOLY_GRAIL_CATEGORIES.filter((category) => holyGrailOverlayCategories[category.key]) as category (category.key + '-' + overlayRefreshNonce)}
          {@const progress = holyGrailCategoryProgress(holyGrailItems, holyGrailFound, category.key)}
          <div class="tracker-row">
            <span class="tracker-label category-{category.key}">{holyGrailCategoryLabel(category.key)}</span>
            <span class="tracker-count">{progress.found}/{progress.total}</span>
          </div>
        {/each}
        {#if holyGrailOverlayShowLatest}
          <div class="tracker-row latest-row">
            <span class="tracker-label latest">Latest</span>
            <span class="tracker-count latest-name">{holyGrailLatestFind?.name ?? '\u2014'}</span>
          </div>
        {/if}
      </div>
    {:else if kind === 'materials'}
      <div class="tracker-header">
        <span class="tracker-title">Mats Tracker</span>
      </div>
      <div class="tracker-rows material-overlay-rows">
        {#each materialTrackerRows as row (row.key + '-' + overlayRefreshNonce)}
          <div class="tracker-row material-overlay-row">
            <span class="tracker-label material-name">{row.label}</span>
            <span class="tracker-count">{row.count}</span>
          </div>
        {/each}
      </div>
    {:else if kind === 'fate-cards'}
      <div class="tracker-header">
        <span class="tracker-title">Fate Cards</span>
      </div>
      <div class="tracker-rows fate-card-overlay-rows">
        {#each fateCardRows as row (row.key + '-' + overlayRefreshNonce)}
          <div class="tracker-row fate-card-overlay-row">
            <span class="tracker-label fate-card-name fate-card-tier-{row.tier}">{row.label}</span>
            <span class="tracker-count">{row.count}</span>
          </div>
        {/each}
      </div>
    {:else if kind === 'achievements'}
      <div class="tracker-header">
        <span class="tracker-title">Achievements</span>
      </div>
      <div class="compact-stat-body">
        <strong>{achievementUnlockedCount}/{achievementTotalCount}</strong>
        <span>{achievementPercent.toFixed(1)}% Complete</span>
      </div>
    {:else if kind === 'achievement-popup'}
      <div class="achievement-popup-preview" style={`font-size:${achievementSettings.overlayFontSize}px;opacity:${achievementSettings.overlayOpacity};`}>
        <span class="achievement-popup-kicker">Achievement Unlocked!</span>
        <strong>Complete the Rune Grail Category</strong>
      </div>
    {:else if kind === 'kills'}
      <div class="tracker-header">
        <span class="tracker-title">Total Kills</span>
      </div>
      <div class="compact-stat-body kills-stat-body">
        <strong>{monsterKillsTotal.toLocaleString()}</strong>
      </div>
    {:else}
      <div class="tracker-header">
        <span class="tracker-title">Rune Tracker</span>
      </div>
      <div class="tracker-rows rune-overlay-rows">
        {#each runeTrackerRows as row (row.key + '-' + overlayRefreshNonce)}
          <div class="tracker-row rune-overlay-row">
            <span class="tracker-label rune-name rune-tier-{row.tier}">{row.label}</span>
            <span class="tracker-count">{row.count}</span>
          </div>
        {/each}
      </div>
    {/if}
    <button class="resize-grip" type="button" aria-label="Resize overlay" title="Resize overlay" onpointerdown={startResize}></button>
  </div>
</main>

<style>
  :global(html),
  :global(body),
  :global(#app) {
    width: 100vw;
    height: 100vh;
    margin: 0;
    overflow: hidden;
    background: transparent !important;
  }

  .card-window {
    width: 100vw;
    height: 100vh;
    display: flex;
    align-items: flex-start;
    justify-content: flex-start;
    background: transparent;
    padding: 0;
    user-select: none;
  }

  .tracker-card {
    position: relative;
    width: 100vw;
    height: 100vh;
    box-sizing: border-box;
    overflow: hidden;
    background: transparent !important;
    border: 1px solid transparent !important;
    border-radius: 6px;
    padding: 6px 10px 8px;
    font-family: var(--font-mono, monospace);
    font-size: 12px;
    line-height: 1.25;
    color: var(--text-primary, #e8e8e8);
    cursor: move;
    text-rendering: geometricPrecision;
    user-select: none;
  }

  .tracker-card > :not(.resize-grip) {
    opacity: 0 !important;
  }

  .tracker-card.disabled {
    opacity: 0.58;
    filter: grayscale(0.35);
  }

  .resize-grip {
    position: absolute;
    right: 2px;
    bottom: 2px;
    z-index: 3;
    width: 18px;
    height: 18px;
    border: 0;
    border-radius: 3px;
    background: transparent;
    cursor: nwse-resize;
    opacity: 0;
    touch-action: none;
  }

  .resize-grip:hover {
    background: transparent;
  }

  .notification-preview {
    width: 100%;
    display: grid;
    gap: 6px;
    align-content: start;
  }

  .achievement-popup-card {
    padding: 0;
    border-color: transparent;
    background: transparent;
  }

  .achievement-popup-preview {
    width: 100%;
    height: 100%;
    min-height: 0;
    display: grid;
    align-content: center;
    justify-items: center;
    gap: 2px;
    box-sizing: border-box;
    padding: 9px 13px;
    border: 1px solid var(--accent-primary);
    background:
      linear-gradient(180deg, color-mix(in srgb, var(--bg-secondary) 96%, transparent), color-mix(in srgb, var(--bg-primary) 94%, transparent));
    box-shadow: 0 0 24px color-mix(in srgb, var(--accent-secondary) 32%, transparent);
    color: var(--text-primary);
    font-family: var(--font-display);
    text-align: center;
  }

  .achievement-popup-kicker {
    color: var(--accent-primary);
    font-family: var(--font-display);
    font-size: 0.9em;
    line-height: 1.05;
  }

  .achievement-popup-preview strong {
    max-width: 100%;
    font-size: 1.08em;
    font-weight: 400;
    line-height: 1.12;
    overflow-wrap: anywhere;
  }

  .muling-card {
    width: 100vw;
    height: 100vh;
    display: inline-flex;
    flex-direction: column;
    align-items: center;
    gap: 3px;
    justify-content: center;
    box-sizing: border-box;
    overflow: hidden;
    padding: 4px;
    background: var(--overlay-panel-bg, rgba(10, 10, 16, 0.82));
    border: 1.5px solid var(--overlay-panel-border, rgba(150, 150, 150, 0.45));
    border-radius: 6px;
    font-family: var(--font-mono, monospace);
  }

  .muling-card.muling-active {
    border-color: var(--accent-primary, rgba(255, 210, 50, 0.95));
    box-shadow: 0 0 12px var(--accent-primary-muted, rgba(255, 200, 40, 0.5));
  }

  .mi-key {
    display: block;
    max-width: 100%;
    overflow: hidden;
    color: var(--accent-primary, #ffd580);
    font-size: 10px;
    font-weight: 800;
    letter-spacing: 0;
    text-align: center;
    text-overflow: ellipsis;
    text-transform: uppercase;
    white-space: nowrap;
  }

  .muling-card.muling-active .mi-key {
    color: var(--text-primary, #fff5cc);
  }

  .mi-mbox {
    display: flex;
    align-items: center;
    justify-content: center;
    width: min(28px, calc(100% - 4px));
    height: min(28px, calc(100% - 20px));
    min-height: 20px;
    border: 2px solid var(--border-secondary, rgba(180, 180, 180, 0.55));
    border-radius: 4px;
    color: var(--text-primary, #e0e0e0);
    font-size: 16px;
    font-weight: 900;
    line-height: 1;
  }

  .muling-card.muling-active .mi-mbox {
    border-color: var(--accent-primary, rgba(255, 210, 50, 0.95));
    color: var(--text-primary, #fff5cc);
  }

  .tracker-header {
    display: grid;
    grid-template-columns: 1fr auto 1fr;
    align-items: center;
    gap: 8px;
    margin-bottom: 6px;
  }

  .tracker-title {
    grid-column: 2;
    justify-self: center;
    color: var(--accent-primary, #f6c33d);
    font-size: 12px;
    font-weight: 800;
    text-align: center;
    text-transform: uppercase;
  }

  .tracker-rows {
    display: flex;
    flex-direction: column;
    gap: 3px;
  }

  .compact-stat-body {
    display: grid;
    gap: 2px;
    place-items: center;
    text-align: center;
  }

  .compact-stat-card {
    display: flex;
    flex-direction: column;
    justify-content: center;
    overflow: hidden;
    border-color: var(--overlay-panel-border, rgba(201, 168, 93, 0.58));
  }

  .compact-stat-body strong {
    color: var(--text-primary, #fff);
    font-family: var(--font-display, var(--font-mono, monospace));
    font-size: clamp(20px, 14px + 1vw, 34px);
    line-height: 1;
  }

  .compact-stat-body span {
    color: var(--accent-primary, #f6c33d);
    font-size: 11px;
    font-weight: 700;
    text-transform: uppercase;
  }

  .kills-stat-body {
    min-height: 0;
  }

  .tracker-row {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 12px;
    line-height: 1.25;
  }

  .tracker-label {
    color: var(--text-primary, #f7f7f7);
    font-size: 12px;
    font-weight: 700;
    min-width: 0;
    white-space: nowrap;
  }

  .tracker-count {
    color: var(--text-primary, #fff);
    font-size: 12px;
    font-weight: 800;
    min-width: 24px;
    text-align: right;
  }

  .latest-name {
    max-width: 150px;
    overflow: hidden;
    font-size: 12px;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .category-unique,
  .category-su { color: #ff9f2f; }
  .category-hellforged,
  .category-ssu { color: #ff4b3e; }
  .category-sssu { color: #ff4f75; }
  .category-relics { color: #d060ff; }
  .category-sets { color: #ffd84a; }
  .category-runes { color: #4da6ff; }
  .category-runewords { color: #f7f2df; }
  .category-fateCards { color: #d7a8ff; }
  .category-hatredOrbs { color: #ff6d6d; }
  .category-uniqueJewels,
  .category-unique_jewels { color: #ff5cff; }
  .category-greatRunes,
  .category-enchantedRunes { color: #55ccff; }
  .category-angelics { color: #ffef9a; }
  .category-arcaneCrystals,
  .category-arcaneShards { color: #8bd3ff; }
  .category-heavenlySouls { color: #d8f3ff; }
  .category-riftstones { color: #bba7ff; }
  .category-oilOfAugmentation,
  .category-oilOfConjuration,
  .category-oilOfGreaterLuck,
  .category-oilOfIntensity { color: #c6f68d; }
  .category-belladonnaExtract { color: #f59bd8; }
  .category-essences { color: #f6c177; }
  .category-ascendancy { color: #76e4c4; }
  .category-lowRune { color: #cfd6df; }
  .category-midRune { color: #ff5a5a; }
  .category-highRune { color: #4da6ff; }
  .category-smallCycle,
  .category-mediumCycle,
  .category-largeCycle,
  .category-goldenCycle { color: #ffd166; }

  .total-card {
    border-color: var(--overlay-panel-border, rgba(120, 190, 255, 0.35));
  }

  .grail-card {
    border-color: var(--overlay-panel-border, rgba(214, 162, 58, 0.45));
  }

  .runes-card,
  .materials-card,
  .fate-cards-card {
    border-color: var(--overlay-panel-border, rgba(201, 168, 93, 0.58));
  }

  .tracker-label.total,
  .tracker-label.run,
  .tracker-label.grail-total,
  .tracker-label.latest {
    color: var(--text-primary, #ffffff);
  }

  .total-row,
  .run-row,
  .latest-row {
    margin-top: 3px;
    padding-top: 3px;
    border-top: 1px solid var(--border-primary);
  }

  .timer-row {
    margin-top: 1px;
  }

  .rune-overlay-rows {
    max-height: calc(100vh - 28px);
    overflow: hidden;
    gap: 1px;
  }

  .rune-overlay-row {
    gap: 6px;
  }

  .material-overlay-rows {
    max-height: calc(100vh - 28px);
    overflow: hidden;
    gap: 2px;
  }

  .material-overlay-row {
    gap: 6px;
  }

  .fate-card-overlay-rows {
    max-height: calc(100vh - 28px);
    overflow: hidden;
    gap: 2px;
  }

  .fate-card-overlay-row {
    gap: 6px;
  }

  .tracker-label.rune-name {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    font-size: 12px;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .tracker-label.material-name {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    color: #d8b26a;
    font-size: 12px;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .tracker-label.fate-card-name {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    color: #d7a8ff;
    font-size: 12px;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .rune-tier-lowRune { color: #cfd6df; }
  .rune-tier-midRune { color: #ff5a5a; }
  .rune-tier-highRune { color: #00f0ff; }
  .fate-card-tier-0 { color: #ff8ee8; }
  .fate-card-tier-1 { color: #d7a8ff; }
  .fate-card-tier-2 { color: #8bd3ff; }
  .fate-card-tier-3 { color: #ffd166; }
  .fate-card-tier-4 { color: #c6f68d; }
  .fate-card-tier-5 { color: #f7f2df; }
</style>
