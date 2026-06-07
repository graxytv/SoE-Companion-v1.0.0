<script lang="ts">
  import { invoke } from '@tauri-apps/api/core';
  import { emit, listen } from '@tauri-apps/api/event';
  import { getCurrentWebviewWindow } from '@tauri-apps/api/webviewWindow';
  import { onMount } from 'svelte';
  import { LootHistoryPanel, NotificationStack } from '../components';
  import { settingsStore, lootHistoryStore, itemsDictionaryStore, type OverlayPosition } from '../stores';
  import { playSound } from '../lib/sound-player';
  import {
    DROP_TRACKER_CATEGORIES,
    categorizeDrop,
    categoryLabel,
    countTotal,
    RUNE_NAMES,
    runeCategory,
    type DropTrackerCategoryKey,
    type DropTrackerCounts,
  } from '../lib/drop-tracker-categories';
  import {
    HOLY_GRAIL_CATEGORIES,
    buildHolyGrailItems,
    canonicalTrackedItemName,
    cleanTrackedItemName,
    holyGrailCategoryLabel,
    holyGrailCategoryProgress,
    holyGrailItemFromDrop,
    inferHolyGrailCategory,
    holyGrailProgress,
    mostRecentHolyGrailFind,
  } from '../lib/holy-grail';
  import { matchGsfDrop, summarizeGsfNeededBy, type GsfMatch } from '../lib/gsf-tracker';
  import {
    evaluateAchievements,
    materialAchievementNameFromDrop,
    unlockedAchievementCount,
    type AchievementUnlockEntry,
  } from '../lib/achievements';
  import { findItemSoundRuleForDrop } from '../lib/item-sounds';
  import { MATERIAL_TRACKER_NAMES, materialTrackerNameFromDrop } from '../lib/material-tracker';
  import { OVERLAY_LAYOUT_WINDOWS, type OverlayLayoutKind } from '../lib/overlay-layout';
  import { fateCardTrackerRows } from '../lib/soe-13-items';

  interface Props {
    mode?: 'game' | 'tracker';
  }

  type TrackerOverlayKind = 'drops' | 'total' | 'grail' | 'materials' | 'runes' | 'fate-cards' | 'achievements' | 'kills';
  type PositionableOverlayKind = TrackerOverlayKind | 'notifications' | 'achievement-popup' | 'muling';

  interface GameWindowRect {
    left: number;
    top: number;
    width: number;
    height: number;
  }

  interface OverlayLayoutEntry {
    kind: PositionableOverlayKind;
    label: string;
    windowLabel: string;
    enabled: boolean;
    x: number;
    y: number;
    width: number;
    height: number;
  }

  interface OverlayLayoutPositionPreview {
    kind: PositionableOverlayKind;
    x: number;
    y: number;
    width?: number;
    height?: number;
    gameWidth: number;
    gameHeight: number;
  }

  let { mode = 'game' }: Props = $props();
  const overlayWindow = getCurrentWebviewWindow();

  interface NotificationFilter {
    color?: string | null;
    sound?: number | null;
    display_stats: boolean;
    matched_stat_lines?: number[] | null;
  }

  interface ItemDrop {
    unit_id: number;
    class: number;
    item_code?: string;
    itemCode?: string;
    quality: string;
    name: string;
    base_name: string;
    canonical_name?: string;
    canonicalName?: string;
    stats: string;
    is_ethereal: boolean;
    is_identified: boolean;
    is_runeword?: boolean;
    isRuneword?: boolean;
    mode?: number;
    file_index?: number;
    is_hellforged?: boolean;
    isHellforged?: boolean;
    category?: string | null;
    is_relic?: boolean;
    history_pushed?: boolean;
    source?: string;
    name_source?: string;
    is_new_grail?: boolean;
    gsf_needed_by?: string[];
    seed?: number;
    filter?: NotificationFilter | null;
  }

  interface ItemWithState extends ItemDrop {
    exiting: boolean;
  }

  const overlayLayoutNotificationPreviewItem: ItemWithState = {
    unit_id: -1,
    class: 0,
    quality: 'Unique',
    name: 'Sample Unique Drop',
    base_name: 'Gilded Shield',
    stats: '',
    is_ethereal: false,
    is_identified: true,
    exiting: false,
  };
  const overlayLayoutAchievementPreviewName = 'Complete the Rune Grail Category';

  let items = $state<ItemWithState[]>([]);
  let achievementPopups = $state<Array<AchievementUnlockEntry & { popupId: string; exiting?: boolean }>>([]);

  // Read settings from store (reactive)
  let notificationDuration = $derived(settingsStore.settings.notificationDuration);
  let notificationFontSize = $derived(settingsStore.settings.notificationFontSize);
  let notificationOpacity = $derived(settingsStore.settings.notificationOpacity);
  let notificationStackDirection = $derived(settingsStore.settings.notificationStackDirection);
  let notificationX = $derived(settingsStore.settings.notificationX);
  let notificationY = $derived(settingsStore.settings.notificationY);
  let notificationWidth = $derived(settingsStore.settings.notificationWidth);
  let notificationHeight = $derived(settingsStore.settings.notificationHeight);
  let notificationOverlayEnabled = $derived(settingsStore.settings.notificationOverlayEnabled);
  let notifyUnidentifiedUniqueSetDrops = $derived(settingsStore.settings.notifyUnidentifiedUniqueSetDrops);
  let soundVolume = $derived(settingsStore.settings.soundVolume);
  let achievementSettings = $derived(settingsStore.settings.achievementSettings);
  let achievementPopupOverlayPosition = $derived(settingsStore.settings.achievementPopupOverlayPosition);
  let achievementPopupOverlayWidth = $derived(settingsStore.settings.achievementPopupOverlayWidth);
  let achievementPopupOverlayHeight = $derived(settingsStore.settings.achievementPopupOverlayHeight);

  let editActive = $state(false);
  let notificationLayoutItems = $derived(editActive ? [overlayLayoutNotificationPreviewItem] : items);
  let historyVisible = $state(false);
  let dropsTrackerEnabled = $derived(settingsStore.settings.dropsTrackerEnabled);
  let totalDropsTrackerEnabled = $derived(settingsStore.settings.totalDropsTrackerEnabled);
  let dropsTrackerRunCounterEnabled = $derived(settingsStore.settings.dropsTrackerRunCounterEnabled);
  let dropsTrackerRunTimerEnabled = $derived(settingsStore.settings.dropsTrackerRunTimerEnabled);
  let dropsTrackerSessionTimerEnabled = $derived(settingsStore.settings.dropsTrackerSessionTimerEnabled);
  let dropsTrackerCategories = $derived(settingsStore.settings.dropsTrackerCategories);
  let totalDropsTrackerCategories = $derived(settingsStore.settings.totalDropsTrackerCategories);
  let dropsTrackerCounts = $derived(settingsStore.settings.dropsTrackerCounts);
  let totalDropsTrackerCounts = $derived(settingsStore.settings.totalDropsTrackerCounts);
  let dropsTrackerRunCount = $derived(settingsStore.settings.dropsTrackerRunCount);
  let timerNow = $state(Date.now());
  let dropsTrackerRunElapsedMs = $derived(settingsStore.getDropsTrackerRunDisplayElapsedMs(timerNow));
  let dropsTrackerSessionElapsedMs = $derived(settingsStore.getDropsTrackerSessionDisplayElapsedMs(timerNow));
  let dropsTrackerMulingMode = $derived(settingsStore.settings.dropsTrackerMulingMode);
  let mulingModeHotkey = $derived(settingsStore.settings.mulingModeHotkey);
  let mulingIndicatorOverlayEnabled = $derived(settingsStore.settings.mulingIndicatorOverlayEnabled);
  let mulingIndicatorPosition = $derived(settingsStore.settings.mulingIndicatorOverlayPosition);
  let mulingIndicatorWidth = $derived(settingsStore.settings.mulingIndicatorOverlayWidth);
  let mulingIndicatorHeight = $derived(settingsStore.settings.mulingIndicatorOverlayHeight);
  let trackerOverlaysSeparateWindow = $derived(settingsStore.settings.trackerOverlaysSeparateWindow);
  let renderGameTrackers = $derived(mode === 'game' && !trackerOverlaysSeparateWindow);
  let renderTrackerWindow = $derived(mode === 'tracker');
  let renderTrackerCards = $derived(renderGameTrackers || renderTrackerWindow);
  // Lightweight render nudge for tracker overlays. This does not mirror or
  // replace tracker state; it only gives keyed rows a harmless dependency so
  // the fixed overlay cards can be re-rendered if the WebView ever leaves them
  // visually stale.
  let overlayRefreshCounter = 0;
  let overlayRefreshNonce = $state(0);
  let dropsTrackerRows = $derived(visibleCategoryRows(dropsTrackerCounts, dropsTrackerCategories, overlayRefreshNonce));
  let totalDropsTrackerRows = $derived(visibleCategoryRows(totalDropsTrackerCounts, totalDropsTrackerCategories, overlayRefreshNonce));
  let totalDropsTrackerTotal = $derived(countTotal(totalDropsTrackerCounts, totalDropsTrackerCategories) + overlayRefreshNonce * 0);
  let trackerEditActive = $derived(mode === 'tracker');
  let dropsTrackerOverlayPosition = $derived(settingsStore.settings.dropsTrackerOverlayPosition);
  let dropsTrackerOverlayWidth = $derived(settingsStore.settings.dropsTrackerOverlayWidth);
  let dropsTrackerOverlayHeight = $derived(settingsStore.settings.dropsTrackerOverlayHeight);
  let totalDropsOverlayPosition = $derived(settingsStore.settings.totalDropsOverlayPosition);
  let totalDropsOverlayWidth = $derived(settingsStore.settings.totalDropsOverlayWidth);
  let totalDropsOverlayHeight = $derived(settingsStore.settings.totalDropsOverlayHeight);
  let runeTrackerCounts = $derived(settingsStore.settings.runeTrackerCounts);
  let runeTrackerOverlayEnabled = $derived(settingsStore.settings.runeTrackerOverlayEnabled);
  let runeTrackerOverlayRunes = $derived(settingsStore.settings.runeTrackerOverlayRunes);
  let runeTrackerOverlayPosition = $derived(settingsStore.settings.runeTrackerOverlayPosition);
  let runeTrackerOverlayWidth = $derived(settingsStore.settings.runeTrackerOverlayWidth);
  let runeTrackerOverlayHeight = $derived(settingsStore.settings.runeTrackerOverlayHeight);
  let materialTrackerCounts = $derived(settingsStore.settings.materialTrackerCounts);
  let materialTrackerOverlayEnabled = $derived(settingsStore.settings.materialTrackerOverlayEnabled);
  let materialTrackerOverlayMaterials = $derived(settingsStore.settings.materialTrackerOverlayMaterials);
  let materialTrackerOverlayPosition = $derived(settingsStore.settings.materialTrackerOverlayPosition);
  let materialTrackerOverlayWidth = $derived(settingsStore.settings.materialTrackerOverlayWidth);
  let materialTrackerOverlayHeight = $derived(settingsStore.settings.materialTrackerOverlayHeight);
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
  let fateCardDropCounts = $derived(settingsStore.settings.fateCardDropCounts);
  let fateCardTrackerOverlayEnabled = $derived(settingsStore.settings.fateCardTrackerOverlayEnabled);
  let fateCardTrackerOverlayCards = $derived(settingsStore.settings.fateCardTrackerOverlayCards);
  let fateCardTrackerOverlayTiers = $derived(settingsStore.settings.fateCardTrackerOverlayTiers);
  let fateCardTrackerOverlayPosition = $derived(settingsStore.settings.fateCardTrackerOverlayPosition);
  let fateCardTrackerOverlayWidth = $derived(settingsStore.settings.fateCardTrackerOverlayWidth);
  let fateCardTrackerOverlayHeight = $derived(settingsStore.settings.fateCardTrackerOverlayHeight);
  let fateCardRows = $derived(
    fateCardTrackerRows(fateCardDropCounts, fateCardTrackerOverlayTiers, fateCardTrackerOverlayCards),
  );
  let holyGrailFound = $derived(settingsStore.settings.holyGrailFound);
  let holyGrailOverlayEnabled = $derived(settingsStore.settings.holyGrailOverlayEnabled);
  let holyGrailOverlayShowTotal = $derived(settingsStore.settings.holyGrailOverlayShowTotal);
  let holyGrailOverlayShowLatest = $derived(settingsStore.settings.holyGrailOverlayShowLatest);
  let holyGrailOverlayCategories = $derived(settingsStore.settings.holyGrailOverlayCategories);
  let holyGrailOverlayPosition = $derived(settingsStore.settings.holyGrailOverlayPosition);
  let holyGrailOverlayWidth = $derived(settingsStore.settings.holyGrailOverlayWidth);
  let holyGrailOverlayHeight = $derived(settingsStore.settings.holyGrailOverlayHeight);
  let achievementStats = $derived(settingsStore.settings.achievementStats);
  let achievementProgressOverlayEnabled = $derived(settingsStore.settings.achievementProgressOverlayEnabled);
  let achievementProgressOverlayPosition = $derived(settingsStore.settings.achievementProgressOverlayPosition);
  let achievementProgressOverlayWidth = $derived(settingsStore.settings.achievementProgressOverlayWidth);
  let achievementProgressOverlayHeight = $derived(settingsStore.settings.achievementProgressOverlayHeight);
  let monsterKillsOverlayEnabled = $derived(settingsStore.settings.monsterKillsOverlayEnabled);
  let monsterKillsOverlayPosition = $derived(settingsStore.settings.monsterKillsOverlayPosition);
  let monsterKillsOverlayWidth = $derived(settingsStore.settings.monsterKillsOverlayWidth);
  let monsterKillsOverlayHeight = $derived(settingsStore.settings.monsterKillsOverlayHeight);
  let gsfEnabled = $derived(settingsStore.settings.gsfEnabled);
  let gsfNotificationEnabled = $derived(settingsStore.settings.gsfNotificationEnabled);
  let gsfSoundSlot = $derived(settingsStore.settings.gsfSoundSlot);
  let gsfSoundVolume = $derived(settingsStore.settings.gsfSoundVolume);
  let gsfPlayers = $derived(settingsStore.settings.gsfPlayers);
  let showTrackerWindow = $derived(
    trackerOverlaysSeparateWindow &&
      (
        dropsTrackerEnabled ||
        totalDropsTrackerEnabled ||
        holyGrailOverlayEnabled ||
        materialTrackerOverlayEnabled ||
        runeTrackerOverlayEnabled ||
        fateCardTrackerOverlayEnabled ||
        achievementProgressOverlayEnabled ||
        monsterKillsOverlayEnabled
      ),
  );
  let renderGameTrackerCards = $derived(renderGameTrackers || (mode === 'game' && editActive));
  let holyGrailNewItemNotificationEnabled = $derived(settingsStore.settings.holyGrailNewItemNotificationEnabled);
  let holyGrailNewItemSoundSlot = $derived(settingsStore.settings.holyGrailNewItemSoundSlot);
  let holyGrailNewItemSoundVolume = $derived(settingsStore.settings.holyGrailNewItemSoundVolume);
  let itemSoundRules = $derived(settingsStore.settings.itemSoundRules);
  let holyGrailItems = $derived(buildHolyGrailItems(itemsDictionaryStore.dict));
  let holyGrailTotalProgress = $derived(holyGrailProgress(holyGrailItems, holyGrailFound));
  let holyGrailLatestFind = $derived(mostRecentHolyGrailFind(holyGrailFound));
  let holyGrailOverlayRenderKey = $derived(`${Object.keys(holyGrailFound).length}:${holyGrailLatestFind?.key ?? 'none'}:${overlayRefreshNonce}`);
  let achievementProgress = $derived(evaluateAchievements({
    stats: achievementStats,
    holyGrailFound,
    holyGrailItems,
    runeTrackerCounts,
  }));
  let achievementUnlockedCount = $derived(unlockedAchievementCount(achievementProgress));
  let achievementTotalCount = $derived(achievementProgress.length);
  let achievementPercent = $derived(achievementTotalCount > 0 ? (achievementUnlockedCount / achievementTotalCount) * 100 : 0);
  let monsterKillsTotal = $derived(achievementStats.totalKills ?? 0);
  let draggingOverlay = $state<null | { kind: TrackerOverlayKind; startMouseX: number; startMouseY: number; startX: number; startY: number; width: number; height: number }>(null);
  let trackerWindowScrollEl = $state<HTMLDivElement | null>(null);
  const GAME_ENTRY_TRACKING_SUPPRESSION_MS = 10_000;
  const OVERLAY_LAYOUT_OFFLINE_MESSAGE = 'Start Diablo II to edit overlay layout by dragging.';
  let suppressTrackingUntilMs = $state(0);
  
  // Animation duration placeholder (currently 0 for instant, can be changed later)
  const EXIT_ANIMATION_DURATION = 0;
  
  const removalTimers = new Map<number, number>();

  function emitDropTrackerStateSnapshot(): void {
    const snapshot = {
      dropsTrackerCounts: settingsStore.settings.dropsTrackerCounts,
      totalDropsTrackerCounts: settingsStore.settings.totalDropsTrackerCounts,
      dropsTrackerRecentItems: settingsStore.settings.dropsTrackerRecentItems,
      runeTrackerCounts: settingsStore.settings.runeTrackerCounts,
      materialTrackerCounts: settingsStore.settings.materialTrackerCounts,
      fateCardDropCounts: settingsStore.settings.fateCardDropCounts,
      achievementStats: settingsStore.settings.achievementStats,
    };
    // Emit a plain JSON payload rather than Svelte proxy objects. This keeps
    // the main window's Recently Tracked list in lockstep with the overlay
    // window after scanner-owned tracking updates.
    void emit('drop-tracker-state-updated', JSON.parse(JSON.stringify(snapshot)));
  }

  function suppressGameEntryTracking(): void {
    suppressTrackingUntilMs = Date.now() + GAME_ENTRY_TRACKING_SUPPRESSION_MS;
  }

  function isGameEntryTrackingSuppressed(): boolean {
    return Date.now() < suppressTrackingUntilMs;
  }

  function trackerOverlayWidth(kind: TrackerOverlayKind): number {
    if (kind === 'drops') return dropsTrackerOverlayWidth;
    if (kind === 'total') return totalDropsOverlayWidth;
    if (kind === 'grail') return holyGrailOverlayWidth;
    if (kind === 'materials') return materialTrackerOverlayWidth;
    if (kind === 'runes') return runeTrackerOverlayWidth;
    if (kind === 'fate-cards') return fateCardTrackerOverlayWidth;
    if (kind === 'achievements') return achievementProgressOverlayWidth;
    return monsterKillsOverlayWidth;
  }

  function trackerOverlayHeight(kind: TrackerOverlayKind): number {
    if (kind === 'drops') return dropsTrackerOverlayHeight;
    if (kind === 'total') return totalDropsOverlayHeight;
    if (kind === 'grail') return holyGrailOverlayHeight;
    if (kind === 'materials') return materialTrackerOverlayHeight;
    if (kind === 'runes') return runeTrackerOverlayHeight;
    if (kind === 'fate-cards') return fateCardTrackerOverlayHeight;
    if (kind === 'achievements') return achievementProgressOverlayHeight;
    return monsterKillsOverlayHeight;
  }

  function trackerOverlayStyle(position: OverlayPosition, fallback: TrackerOverlayKind): string {
    const width = trackerOverlayWidth(fallback);
    const height = trackerOverlayHeight(fallback);
    const sizeStyle = `--tracker-card-width: ${width}px; --tracker-card-height: ${height}px;`;
    if (mode === 'tracker') {
      return sizeStyle;
    }

    if (typeof position.x === 'number' && typeof position.y === 'number') {
      return `left: ${position.x}px; top: ${position.y}px; ${sizeStyle}`;
    }

    if (fallback === 'drops') {
      return `left: 12px; bottom: 12px; ${sizeStyle}`;
    }

    if (fallback === 'grail') {
      return `left: 12px; bottom: 220px; ${sizeStyle}`;
    }

    if (fallback === 'materials') {
      return `left: 12px; top: 448px; ${sizeStyle}`;
    }

    if (fallback === 'runes') {
      return `right: 12px; top: 12px; ${sizeStyle}`;
    }

    if (fallback === 'fate-cards') {
      return `left: 260px; top: 612px; ${sizeStyle}`;
    }

    if (fallback === 'achievements') {
      return `left: 12px; top: 608px; ${sizeStyle}`;
    }

    if (fallback === 'kills') {
      return `left: 12px; top: 708px; ${sizeStyle}`;
    }

    return `right: 12px; bottom: 12px; ${sizeStyle}`;
  }

  function achievementPopupStackStyle(): string {
    const sizeStyle = `--achievement-popup-width: ${achievementPopupOverlayWidth}px; --achievement-popup-min-height: ${achievementPopupOverlayHeight}px;`;
    const top = typeof achievementPopupOverlayPosition.y === 'number' ? achievementPopupOverlayPosition.y : 18;
    if (typeof achievementPopupOverlayPosition.x === 'number') {
      return `left:${achievementPopupOverlayPosition.x}px;top:${top}px;transform:none;${sizeStyle}`;
    }
    return `left:50%;top:${top}px;transform:translateX(-50%);${sizeStyle}`;
  }

  function defaultTrackerPosition(kind: TrackerOverlayKind, gameRect: GameWindowRect, width: number, height: number): { x: number; y: number } {
    const right = Math.max(0, gameRect.width - width - 12);
    const bottom = Math.max(0, gameRect.height - height - 12);
    if (kind === 'drops') return { x: 12, y: bottom };
    if (kind === 'total') return { x: right, y: bottom };
    if (kind === 'grail') return { x: 12, y: Math.max(0, gameRect.height - height - 220) };
    if (kind === 'materials') return { x: 12, y: 448 };
    if (kind === 'runes') return { x: right, y: 12 };
    if (kind === 'fate-cards') return { x: 260, y: Math.min(bottom, 612) };
    if (kind === 'achievements') return { x: 12, y: 608 };
    return { x: 12, y: 708 };
  }

  function trackerPositionForEditor(position: OverlayPosition, kind: TrackerOverlayKind, gameRect: GameWindowRect, width: number, height: number): { x: number; y: number } {
    const fallback = defaultTrackerPosition(kind, gameRect, width, height);
    return {
      x: typeof position.x === 'number' ? position.x : fallback.x,
      y: typeof position.y === 'number' ? position.y : fallback.y,
    };
  }

  function safeScaleFactor(scaleFactor: number): number {
    return Number.isFinite(scaleFactor) && scaleFactor > 0 ? scaleFactor : 1;
  }

  function logicalGameRect(gameRect: GameWindowRect, scaleFactor: number): GameWindowRect {
    const scale = safeScaleFactor(scaleFactor);
    return {
      ...gameRect,
      width: Math.max(1, Math.round(gameRect.width / scale)),
      height: Math.max(1, Math.round(gameRect.height / scale)),
    };
  }

  function layoutEntryEnabled(kind: OverlayLayoutKind): boolean {
    if (kind === 'notifications') return notificationOverlayEnabled;
    if (kind === 'drops') return dropsTrackerEnabled;
    if (kind === 'total') return totalDropsTrackerEnabled;
    if (kind === 'grail') return holyGrailOverlayEnabled;
    if (kind === 'materials') return materialTrackerOverlayEnabled;
    if (kind === 'runes') return runeTrackerOverlayEnabled;
    if (kind === 'fate-cards') return fateCardTrackerOverlayEnabled;
    if (kind === 'achievements') return achievementProgressOverlayEnabled;
    if (kind === 'achievement-popup') return achievementSettings.overlayEnabled;
    if (kind === 'kills') return monsterKillsOverlayEnabled;
    return mulingIndicatorOverlayEnabled;
  }

  function buildOverlayLayoutEntry(
    definition: typeof OVERLAY_LAYOUT_WINDOWS[number],
    gameRect: GameWindowRect,
  ): OverlayLayoutEntry {
    if (definition.kind === 'notifications') {
      return {
        ...definition,
        enabled: layoutEntryEnabled(definition.kind),
        x: Math.round((notificationX / 100) * gameRect.width),
        y: Math.round((notificationY / 100) * gameRect.height),
        width: notificationWidth,
        height: notificationHeight,
      };
    }

    if (definition.kind === 'achievement-popup') {
      return {
        ...definition,
        enabled: layoutEntryEnabled(definition.kind),
        x: typeof achievementPopupOverlayPosition.x === 'number'
          ? achievementPopupOverlayPosition.x
          : Math.max(0, Math.round((gameRect.width - achievementPopupOverlayWidth) / 2)),
        y: typeof achievementPopupOverlayPosition.y === 'number' ? achievementPopupOverlayPosition.y : 18,
        width: achievementPopupOverlayWidth,
        height: achievementPopupOverlayHeight,
      };
    }

    if (definition.kind === 'muling') {
      return {
        ...definition,
        enabled: layoutEntryEnabled(definition.kind),
        x: typeof mulingIndicatorPosition.x === 'number' ? mulingIndicatorPosition.x : 12,
        y: typeof mulingIndicatorPosition.y === 'number' ? mulingIndicatorPosition.y : 640,
        width: mulingIndicatorWidth,
        height: mulingIndicatorHeight,
      };
    }

    const position = currentTrackerOverlayPosition(definition.kind);
    const width = trackerOverlayWidth(definition.kind);
    const height = trackerOverlayHeight(definition.kind);
    const editorPosition = trackerPositionForEditor(position, definition.kind, gameRect, width, height);
    return {
      ...definition,
      enabled: layoutEntryEnabled(definition.kind),
      x: editorPosition.x,
      y: editorPosition.y,
      width,
      height,
    };
  }

  function emitOverlayLayoutMessage(message: string): void {
    void emit('overlay-layout-message', { message });
  }

  async function hideOverlayLayoutEditorWindows(): Promise<void> {
    await Promise.all(
      OVERLAY_LAYOUT_WINDOWS.map((definition) =>
        invoke('set_overlay_editor_window_visible', {
          label: definition.windowLabel,
          visible: false,
        }).catch((err) => {
          console.error(`[Overlay] Failed to hide editor window ${definition.windowLabel}:`, err);
        }),
      ),
    );
  }

  async function setOverlayLayoutEditorWindowsVisible(visible: boolean): Promise<boolean> {
    if (!visible) {
      await hideOverlayLayoutEditorWindows();
      editActive = false;
      emitOverlayLayoutMessage('');
      return true;
    }

    let gameRect: GameWindowRect;
    try {
      const [physicalGameRect, scaleFactor] = await Promise.all([
        invoke<GameWindowRect>('get_game_window_rect'),
        overlayWindow.scaleFactor(),
      ]);
      gameRect = logicalGameRect(physicalGameRect, scaleFactor);
    } catch (err) {
      await hideOverlayLayoutEditorWindows();
      editActive = false;
      emitOverlayLayoutMessage(OVERLAY_LAYOUT_OFFLINE_MESSAGE);
      console.warn('[Overlay] Cannot enter overlay layout edit mode:', err);
      void emit('overlay-edit-mode', { active: false });
      return false;
    }

    const entries = OVERLAY_LAYOUT_WINDOWS.map((definition) => buildOverlayLayoutEntry(definition, gameRect));
    try {
      for (const entry of entries) {
        await invoke('set_overlay_editor_window_visible', {
          label: entry.windowLabel,
          visible: true,
          x: entry.x,
          y: entry.y,
          width: entry.width,
          height: entry.height,
        });
      }
      editActive = true;
      emitOverlayLayoutMessage('');
      return true;
    } catch (err) {
      await hideOverlayLayoutEditorWindows();
      editActive = false;
      emitOverlayLayoutMessage(String(err));
      console.error('[Overlay] Failed to show overlay layout editor windows:', err);
      void emit('overlay-edit-mode', { active: false });
      return false;
    }
  }

  function applyOverlayLayoutPositionPreview(payload: OverlayLayoutPositionPreview): void {
    if (payload.kind === 'notifications') {
      const nextX = payload.gameWidth > 0 ? (payload.x / payload.gameWidth) * 100 : 0;
      const nextY = payload.gameHeight > 0 ? (payload.y / payload.gameHeight) * 100 : 0;
      settingsStore.setNotificationPosition(
        Math.max(0, Math.min(100, Math.round(nextX * 10) / 10)),
        Math.max(0, Math.min(100, Math.round(nextY * 10) / 10)),
      );
      if (typeof payload.width === 'number') settingsStore.set('notificationWidth', Math.round(payload.width));
      if (typeof payload.height === 'number') settingsStore.set('notificationHeight', Math.round(payload.height));
      return;
    }

    if (payload.kind === 'achievement-popup') {
      settingsStore.setAchievementPopupOverlayPosition({
        x: Math.round(payload.x),
        y: Math.round(payload.y),
      });
      if (typeof payload.width === 'number') settingsStore.setAchievementPopupOverlayWidth(payload.width);
      if (typeof payload.height === 'number') settingsStore.setAchievementPopupOverlayHeight(payload.height);
      return;
    }

    if (payload.kind === 'muling') {
      settingsStore.setMulingIndicatorOverlayPosition({
        x: Math.round(payload.x),
        y: Math.round(payload.y),
      });
      if (typeof payload.width === 'number') settingsStore.setMulingIndicatorOverlayWidth(payload.width);
      if (typeof payload.height === 'number') settingsStore.setMulingIndicatorOverlayHeight(payload.height);
      return;
    }

    persistTrackerOverlayPosition(payload.kind, payload.x, payload.y);
    if (typeof payload.width === 'number') {
      if (payload.kind === 'drops') settingsStore.setDropsTrackerOverlayWidth(payload.width);
      else if (payload.kind === 'total') settingsStore.setTotalDropsOverlayWidth(payload.width);
      else if (payload.kind === 'grail') settingsStore.setHolyGrailOverlayWidth(payload.width);
      else if (payload.kind === 'materials') settingsStore.setMaterialTrackerOverlayWidth(payload.width);
      else if (payload.kind === 'runes') settingsStore.setRuneTrackerOverlayWidth(payload.width);
      else if (payload.kind === 'fate-cards') settingsStore.setFateCardTrackerOverlayWidth(payload.width);
      else if (payload.kind === 'achievements') settingsStore.setAchievementProgressOverlayWidth(payload.width);
      else settingsStore.setMonsterKillsOverlayWidth(payload.width);
    }
    if (typeof payload.height === 'number') {
      if (payload.kind === 'drops') settingsStore.setDropsTrackerOverlayHeight(payload.height);
      else if (payload.kind === 'total') settingsStore.setTotalDropsOverlayHeight(payload.height);
      else if (payload.kind === 'grail') settingsStore.setHolyGrailOverlayHeight(payload.height);
      else if (payload.kind === 'materials') settingsStore.setMaterialTrackerOverlayHeight(payload.height);
      else if (payload.kind === 'runes') settingsStore.setRuneTrackerOverlayHeight(payload.height);
      else if (payload.kind === 'fate-cards') settingsStore.setFateCardTrackerOverlayHeight(payload.height);
      else if (payload.kind === 'achievements') settingsStore.setAchievementProgressOverlayHeight(payload.height);
      else settingsStore.setMonsterKillsOverlayHeight(payload.height);
    }
    nudgeTrackerOverlayRender();
  }

  function nudgeTrackerOverlayRender(): void {
    overlayRefreshCounter = (overlayRefreshCounter + 1) % 1_000_000;
    overlayRefreshNonce = overlayRefreshCounter;
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

  function resetRunTimer(event?: PointerEvent): void {
    event?.stopPropagation();
    event?.preventDefault();
    settingsStore.resetDropsTrackerRunTimer();
    nudgeTrackerOverlayRender();
  }

  function resetSessionTimer(event?: PointerEvent): void {
    event?.stopPropagation();
    event?.preventDefault();
    settingsStore.resetDropsTrackerSessionTimer();
    nudgeTrackerOverlayRender();
  }

  $effect(() => {
    // Main-window resets and settings syncs already update settingsStore; this
    // effect only nudges Svelte to repaint the overlay rows from that state.
    dropsTrackerCounts;
    totalDropsTrackerCounts;
    dropsTrackerRunCount;
    dropsTrackerRunElapsedMs;
    dropsTrackerSessionElapsedMs;
    dropsTrackerMulingMode;
    dropsTrackerCategories;
    totalDropsTrackerCategories;
    runeTrackerCounts;
    materialTrackerCounts;
    materialTrackerOverlayMaterials;
    materialTrackerOverlayWidth;
    materialTrackerOverlayHeight;
    runeTrackerOverlayRunes;
    runeTrackerOverlayWidth;
    runeTrackerOverlayHeight;
    fateCardDropCounts;
    fateCardTrackerOverlayCards;
    fateCardTrackerOverlayTiers;
    fateCardTrackerOverlayWidth;
    fateCardTrackerOverlayHeight;
    holyGrailFound;
    dropsTrackerOverlayWidth;
    dropsTrackerOverlayHeight;
    totalDropsOverlayWidth;
    totalDropsOverlayHeight;
    holyGrailOverlayWidth;
    holyGrailOverlayHeight;
    achievementStats;
    achievementProgressOverlayWidth;
    achievementProgressOverlayHeight;
    monsterKillsOverlayWidth;
    monsterKillsOverlayHeight;
    nudgeTrackerOverlayRender();
  });

  function persistTrackerOverlayPosition(kind: TrackerOverlayKind, x: number, y: number): void {
    const position = { x: Math.round(x), y: Math.round(y) };
    if (kind === 'drops') {
      settingsStore.setDropsTrackerOverlayPosition(position);
    } else if (kind === 'total') {
      settingsStore.setTotalDropsOverlayPosition(position);
    } else if (kind === 'grail') {
      settingsStore.setHolyGrailOverlayPosition(position);
    } else if (kind === 'materials') {
      settingsStore.setMaterialTrackerOverlayPosition(position);
    } else if (kind === 'runes') {
      settingsStore.setRuneTrackerOverlayPosition(position);
    } else if (kind === 'fate-cards') {
      settingsStore.setFateCardTrackerOverlayPosition(position);
    } else if (kind === 'achievements') {
      settingsStore.setAchievementProgressOverlayPosition(position);
    } else {
      settingsStore.setMonsterKillsOverlayPosition(position);
    }
  }

  function currentTrackerOverlayPosition(kind: TrackerOverlayKind): OverlayPosition {
    if (kind === 'drops') return dropsTrackerOverlayPosition;
    if (kind === 'total') return totalDropsOverlayPosition;
    if (kind === 'grail') return holyGrailOverlayPosition;
    if (kind === 'materials') return materialTrackerOverlayPosition;
    if (kind === 'runes') return runeTrackerOverlayPosition;
    if (kind === 'fate-cards') return fateCardTrackerOverlayPosition;
    if (kind === 'achievements') return achievementProgressOverlayPosition;
    return monsterKillsOverlayPosition;
  }

  function persistTrackerOverlayWindowY(kind: TrackerOverlayKind, y: number): void {
    const current = currentTrackerOverlayPosition(kind);
    const position = {
      x: typeof current.x === 'number' ? current.x : null,
      y: Math.round(Math.max(0, y)),
    };
    if (kind === 'drops') {
      settingsStore.setDropsTrackerOverlayPosition(position);
    } else if (kind === 'total') {
      settingsStore.setTotalDropsOverlayPosition(position);
    } else if (kind === 'grail') {
      settingsStore.setHolyGrailOverlayPosition(position);
    } else if (kind === 'materials') {
      settingsStore.setMaterialTrackerOverlayPosition(position);
    } else if (kind === 'runes') {
      settingsStore.setRuneTrackerOverlayPosition(position);
    } else if (kind === 'fate-cards') {
      settingsStore.setFateCardTrackerOverlayPosition(position);
    } else if (kind === 'achievements') {
      settingsStore.setAchievementProgressOverlayPosition(position);
    } else {
      settingsStore.setMonsterKillsOverlayPosition(position);
    }
  }

  function beginTrackerOverlayDrag(kind: TrackerOverlayKind, event: PointerEvent): void {
    if (mode === 'tracker') return;
    const target = event.currentTarget as HTMLElement | null;
    const wrapper = target?.closest<HTMLElement>('.tracker-overlay-position');
    if (!wrapper) return;

    const rect = wrapper.getBoundingClientRect();
    draggingOverlay = {
      kind,
      startMouseX: event.clientX,
      startMouseY: event.clientY,
      startX: rect.left,
      startY: rect.top,
      width: rect.width,
      height: rect.height,
    };

    // Keep receiving pointermove events even if the cursor moves faster than
    // the small overlay card. This makes dragging reliable on the transparent
    // game overlay window.
    try {
      wrapper.setPointerCapture(event.pointerId);
    } catch {
      // Pointer capture is best-effort; window-level listeners below are the fallback.
    }
    event.stopPropagation();
    event.preventDefault();
  }

  function moveTrackerOverlay(event: PointerEvent): void {
    if (!draggingOverlay) return;
    const nextY = Math.max(0, draggingOverlay.startY + event.clientY - draggingOverlay.startMouseY);
    const nextX = Math.max(0, Math.min(window.innerWidth - draggingOverlay.width, draggingOverlay.startX + event.clientX - draggingOverlay.startMouseX));
    persistTrackerOverlayPosition(draggingOverlay.kind, nextX, Math.min(window.innerHeight - draggingOverlay.height, nextY));
  }

  function endTrackerOverlayDrag(): void {
    draggingOverlay = null;
  }

  function scrollTrackerWindow(event: WheelEvent): void {
    if (mode !== 'tracker' || !trackerWindowScrollEl) return;
    trackerWindowScrollEl.scrollTop += event.deltaY;
    event.preventDefault();
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

  function isPlaceholderItemName(value: unknown): boolean {
    return /^Item #\d+$/i.test(String(value ?? '').trim());
  }

  function normalizeDisplayName(name: string): string {
    // Normalize any filter decoration before display.
    return cleanTrackedItemName(name);
  }

  function trackedItemDisplayName(item: ItemDrop): string {
    if (isUnidentifiedUniqueSetDrop(item)) {
      return `Unidentified ${cleanTrackedItemName(item.base_name || item.name || 'item')}`;
    }
    const codeOnlyGrailItem = holyGrailItemFromDrop(item);
    if (codeOnlyGrailItem && (item.item_code || item.itemCode)) {
      return codeOnlyGrailItem.name;
    }
    const canonicalName = cleanTrackedItemName(item.canonical_name || item.canonicalName || '');
    if (canonicalName) return canonicalTrackedItemName(canonicalName, inferHolyGrailCategory(item));
    if (item.name && !isPlaceholderItemName(item.name)) {
      return canonicalTrackedItemName(item.name, inferHolyGrailCategory(item));
    }
    if (item.base_name) {
      return canonicalTrackedItemName(item.base_name, inferHolyGrailCategory(item));
    }
    return canonicalTrackedItemName(item.name || 'Unknown item', inferHolyGrailCategory(item));
  }

  function isUniqueOrSetDrop(item: ItemDrop): boolean {
    const quality = String(item.quality ?? '').toLowerCase();
    return quality === 'unique' || quality === 'set';
  }

  function isUnidentifiedUniqueSetDrop(item: ItemDrop): boolean {
    return isUniqueOrSetDrop(item) && !item.is_identified;
  }

  function isIdentifyInventoryEvent(item: ItemDrop): boolean {
    return String(item.source ?? '').toLowerCase() === 'identify-inventory';
  }

  function hasTrustedExactUniqueSetName(item: ItemDrop): boolean {
    if (!isUniqueOrSetDrop(item) || !item.is_identified) return true;
    if (holyGrailItemFromDrop(item)) return true;
    const source = String(item.source ?? '').toLowerCase();
    if (source === 'grail-log') return true;
    if (cleanTrackedItemName(item.canonical_name || item.canonicalName || '')) return true;
    if (String(item.name_source ?? '').toLowerCase() === 'live-tooltip') return true;

    // Hellforged uniques are a real SoE grail category. If the scanner delivers
    // a filter-decorated Hellforged name, the cleaned name is enough to match
    // the static grail catalog even when the native name_source label is absent.
    const cleanedName = trackedItemDisplayName(item);
    return /\bhellforged\b/i.test(cleanedName);
  }

  function trackingCategoriesForDrop(item: ItemDrop): DropTrackerCategoryKey[] {
    const grailItem = holyGrailItemFromDrop(item);
    if (grailItem?.category === 'su') return ['unique'];
    if (grailItem?.category === 'ssu') return ['hellforged'];
    if (grailItem?.category === 'sets') return ['sets'];
    if (grailItem?.category === 'fateCards') return ['fateCard'];
    if (grailItem?.category === 'hatredOrbs') return ['hatredOrb'];
    if (grailItem?.category === 'essences') return ['essence'];
    if (grailItem?.category === 'ascendancy') return ['ascendancy'];
    if (!isUnidentifiedUniqueSetDrop(item) && hasTrustedExactUniqueSetName(item)) {
      return categorizeDrop(item);
    }
    return String(item.quality ?? '').toLowerCase() === 'set' ? ['sets'] : ['unique'];
  }

  function recordTrackerDrop(item: ItemDrop, isNewGrailItem = false) {
    if (dropsTrackerMulingMode) return;

    const categories = trackingCategoriesForDrop(item);
    const exactNameTrusted = hasTrustedExactUniqueSetName(item);
    const displayName = exactNameTrusted
      ? trackedItemDisplayName(item)
      : `Unverified ${cleanTrackedItemName(item.canonical_name || item.canonicalName || item.base_name || item.name || item.quality || 'item')}`;
    const materialName =
      materialAchievementNameFromDrop(item.canonical_name || item.canonicalName) ??
      materialAchievementNameFromDrop(item.base_name) ??
      materialAchievementNameFromDrop(displayName);
    const trackerMaterialName = materialTrackerNameFromDrop(item);
    const grailDropItem = holyGrailItemFromDrop(item);
    const fateCardName = grailDropItem?.category === 'fateCards' ? grailDropItem.name : null;
    if (categories.length === 0 && !materialName && !trackerMaterialName && !fateCardName) return;

    const debugSource = [
      item.source ?? 'scanner',
      item.item_code || item.itemCode ? `code=${item.item_code || item.itemCode}` : null,
      item.name_source ? `name=${item.name_source}` : null,
      item.canonical_name || item.canonicalName ? `canonical=${item.canonical_name || item.canonicalName}` : null,
      item.mode != null ? `mode=${item.mode}` : null,
      item.file_index != null ? `idx=${item.file_index}` : null,
      item.seed ? `seed=${item.seed}` : null,
      item.unit_id ? `unit=${item.unit_id}` : null,
    ].filter(Boolean).join(' ');

    if (categories.length > 0) {
      settingsStore.recordDropTrackerItem(displayName, categories, {
        isNewGrail: isNewGrailItem,
        source: debugSource,
      });
    }
    if (materialName) {
      settingsStore.recordAchievementMaterialDrop(materialName);
    }
    if (fateCardName) {
      settingsStore.recordFateCardDrop(fateCardName);
    }
    if (trackerMaterialName) {
      settingsStore.recordMaterialTrackerDrop(trackerMaterialName);
    }
    settingsStore.evaluateAchievementUnlocks({
      holyGrailFound: settingsStore.settings.holyGrailFound,
      holyGrailItems,
      runeTrackerCounts: settingsStore.settings.runeTrackerCounts,
    });
    emitDropTrackerStateSnapshot();
    nudgeTrackerOverlayRender();
  }


  function recordHolyGrailDrop(item: ItemDrop): boolean {
    if (dropsTrackerMulingMode) return false;
    if (isUnidentifiedUniqueSetDrop(item)) return false;
    if (!hasTrustedExactUniqueSetName(item)) return false;
    const isNewGrailItem = settingsStore.recordHolyGrailDrop(item);
    if (isNewGrailItem) {
      nudgeTrackerOverlayRender();
      void emit('holy-grail-found-updated', settingsStore.settings.holyGrailFound);
      emitDropTrackerStateSnapshot();
    }
    return isNewGrailItem;
  }

  function gsfMatchesForDrop(item: ItemDrop): GsfMatch[] {
    if (!gsfEnabled) return [];
    if (dropsTrackerMulingMode) return [];
    if (isUnidentifiedUniqueSetDrop(item)) return [];
    if (!hasTrustedExactUniqueSetName(item)) return [];
    return matchGsfDrop(item, gsfPlayers);
  }

  function removeItem(unit_id: number) {
    items = items.filter(item => item.unit_id !== unit_id);
    removalTimers.delete(unit_id);
  }

  function showAchievementPopup(entry: AchievementUnlockEntry) {
    if (!achievementSettings.overlayEnabled) return;
    if (!entry?.id || !entry?.name) return;
    const popupId = `${entry.id}-${Date.now()}-${Math.random().toString(36).slice(2)}`;
    achievementPopups = [{ ...entry, popupId }, ...achievementPopups].slice(0, 3);
    if (achievementSettings.soundEnabled && achievementSettings.soundSlot != null) {
      void playSound(achievementSettings.soundSlot, soundVolume * achievementSettings.soundVolume);
    }
    const duration = Math.max(1500, Math.min(20000, Number(achievementSettings.overlayDuration) || 6000));
    window.setTimeout(() => {
      achievementPopups = achievementPopups.map((popup) =>
        popup.popupId === popupId ? { ...popup, exiting: true } : popup,
      );
      window.setTimeout(() => {
        achievementPopups = achievementPopups.filter((popup) => popup.popupId !== popupId);
      }, 250);
    }, duration);
  }

  function startExitAnimation(unit_id: number) {
    // Mark item as exiting to trigger animation (placeholder for future use)
    items = items.map(item => 
      item.unit_id === unit_id 
        ? { ...item, exiting: true } 
        : item
    );
    
    // Remove item after animation completes (instant for now)
    if (EXIT_ANIMATION_DURATION > 0) {
      setTimeout(() => {
        removeItem(unit_id);
      }, EXIT_ANIMATION_DURATION);
    } else {
      removeItem(unit_id);
    }
  }

  function addItem(item: ItemDrop, duration: number) {
    // Add item to the stack with exiting = false
    const itemWithState: ItemWithState = { ...item, exiting: false };
    items = [itemWithState, ...items].slice(0, 100);
    
    // Clear existing timer if item already exists (shouldn't happen but just in case)
    const existingTimer = removalTimers.get(item.unit_id);
    if (existingTimer) {
      clearTimeout(existingTimer);
    }
    
    // Set timer to start exit after duration
    const timer = window.setTimeout(() => {
      startExitAnimation(item.unit_id);
    }, duration);
    
    removalTimers.set(item.unit_id, timer);
  }

  $effect(() => {
    if (mode === 'game') {
      invoke('set_overlay_interactive', { active: historyVisible }).catch((err) => {
        console.error('[Overlay] set_overlay_interactive failed:', err);
      });
    }
  });

  $effect(() => {
    if (mode === 'game') {
      invoke('set_tracker_overlay_window_visible', { visible: showTrackerWindow }).catch((err) => {
        console.error('[Overlay] set_tracker_overlay_window_visible failed:', err);
      });
    }
  });

  onMount(() => {
    const unlisteners: Array<() => void> = [];
    let syncTimer: number | null = null;
    let pendingTimerPause: number | null = null;
    if (mode === 'game') {
      suppressGameEntryTracking();
    }
    if (mode === 'tracker') {
      document.documentElement.classList.add('tracker-overlay-mode');
      document.body.classList.add('tracker-overlay-mode');
    }
    const clearPendingTimerPause = () => {
      if (pendingTimerPause == null) return;
      window.clearTimeout(pendingTimerPause);
      pendingTimerPause = null;
    };
    const displayTimer = window.setInterval(() => {
      timerNow = Date.now();
      if (mode === 'game') {
        settingsStore.tickDropsTrackerTimers();
      }
      nudgeTrackerOverlayRender();
    }, 1000);
    document.addEventListener('pointermove', moveTrackerOverlay);
    document.addEventListener('pointerup', endTrackerOverlayDrag);
    document.addEventListener('pointercancel', endTrackerOverlayDrag);

    // Initialize loot history store for scanner drop history.
    void lootHistoryStore.initialize();
    void itemsDictionaryStore.init();

    // Listen for item drops
    listen('toggle-muling-mode', () => {
      const next = !settingsStore.settings.dropsTrackerMulingMode;
      settingsStore.setDropsTrackerMulingMode(next);
      invoke('set_muling_banner_window_visible', { visible: next }).catch(() => {});
    }).then(u => unlisteners.push(u));

    listen<ItemDrop>('item-drop', (event) => {
      if (mode !== 'game') return;
      if (isGameEntryTrackingSuppressed()) return;
      const item = event.payload;
      const identifyInventoryEvent = isIdentifyInventoryEvent(item);
      const unidentifiedUniqueSet = isUnidentifiedUniqueSetDrop(item);
      const allowNotification = !unidentifiedUniqueSet || notifyUnidentifiedUniqueSetDrops;
      const isNewGrailItem = recordHolyGrailDrop(item);
      const gsfMatches = gsfMatchesForDrop(item);
      const gsfNeededBy = gsfNotificationEnabled ? summarizeGsfNeededBy(gsfMatches) : [];
      const shouldShowVisualNotification =
        (holyGrailNewItemNotificationEnabled && isNewGrailItem) ||
        gsfNeededBy.length > 0 ||
        (unidentifiedUniqueSet && notifyUnidentifiedUniqueSetDrops);
      const displayItem = {
        ...item,
        name: trackedItemDisplayName(item),
        is_new_grail: holyGrailNewItemNotificationEnabled && isNewGrailItem,
        gsf_needed_by: gsfNeededBy,
      };
      if (allowNotification && shouldShowVisualNotification) {
        addItem(displayItem, notificationDuration);
      }
      if (!identifyInventoryEvent && item.history_pushed !== false) {
        recordTrackerDrop(item, isNewGrailItem);
      } else if (isNewGrailItem) {
        settingsStore.recordGrailOnlyRecentItem(trackedItemDisplayName(item), categorizeDrop(item), {
          source: 'identify-inventory grail-only',
        });
        emitDropTrackerStateSnapshot();
      }

      const itemSoundRule = findItemSoundRuleForDrop(item, itemSoundRules);
      if (itemSoundRule?.soundSlot != null) {
        playSound(itemSoundRule.soundSlot, soundVolume * itemSoundRule.volume);
      } else if (isNewGrailItem && holyGrailNewItemSoundSlot != null) {
        playSound(holyGrailNewItemSoundSlot, soundVolume * holyGrailNewItemSoundVolume);
      }

      if (gsfMatches.length > 0 && gsfSoundSlot != null) {
        playSound(gsfSoundSlot, soundVolume * gsfSoundVolume);
      }

      const s = event.payload.filter?.sound;
      if (allowNotification && shouldShowVisualNotification && s != null) playSound(s, soundVolume);
    }).then(u => unlisteners.push(u));

    listen<AchievementUnlockEntry>('achievement-unlocked', (event) => {
      if (mode !== 'game') return;
      showAchievementPopup(event.payload);
    }).then(u => unlisteners.push(u));

    const testAchievementPopup = (event: Event) => {
      if (mode !== 'game') return;
      const custom = event as CustomEvent<AchievementUnlockEntry>;
      showAchievementPopup(custom.detail);
    };
    window.addEventListener('achievement-test-popup', testAchievementPopup);

    listen('refresh-tracker-overlays', () => {
      nudgeTrackerOverlayRender();
      requestAnimationFrame(() => nudgeTrackerOverlayRender());
      window.setTimeout(() => nudgeTrackerOverlayRender(), 120);
    }).then(u => unlisteners.push(u));

    listen<typeof settingsStore.settings.holyGrailFound>('holy-grail-found-updated', (event) => {
      if (mode !== 'tracker') return;
      settingsStore.mergeHolyGrailFound(event.payload);
      nudgeTrackerOverlayRender();
      requestAnimationFrame(() => nudgeTrackerOverlayRender());
    }).then(u => unlisteners.push(u));

    listen<OverlayLayoutPositionPreview>('overlay-layout-position-preview', (event) => {
      if (mode !== 'game') return;
      applyOverlayLayoutPositionPreview(event.payload);
    }).then(u => unlisteners.push(u));

    listen<{ active: boolean }>('overlay-edit-mode', async (event) => {
      if (mode !== 'game') return;
      const active = event.payload.active;
      if (active) {
        const entered = await setOverlayLayoutEditorWindowsVisible(true);
        if (!entered) return;
        try {
          await invoke('set_overlay_interactive', { active: historyVisible });
        } catch (err) {
          console.error('[Overlay] set_overlay_interactive (layout edit) failed:', err);
        }
      } else {
        await setOverlayLayoutEditorWindowsVisible(false);
        try {
          await invoke('set_overlay_interactive', { active: historyVisible });
        } catch (err) {
          console.error('[Overlay] set_overlay_interactive(false) failed:', err);
        }
      }
    }).then(u => unlisteners.push(u));


    listen<string>('game-status', (event) => {
      if (mode !== 'game') return;
      const ingame = event.payload === 'ingame';

      if (ingame) {
        suppressGameEntryTracking();
        clearPendingTimerPause();
        const wasInGame = settingsStore.dropsTrackerTimersInGame;
        settingsStore.setDropsTrackerTimersInGame(true);
        if (!wasInGame) {
          settingsStore.incrementDropsTrackerRunCount();
        }
        nudgeTrackerOverlayRender();
        return;
      }

      clearPendingTimerPause();
      pendingTimerPause = window.setTimeout(() => {
        pendingTimerPause = null;
        settingsStore.setDropsTrackerTimersInGame(false);
        nudgeTrackerOverlayRender();
      }, 2500);
    }).then(u => unlisteners.push(u));

    if (mode === 'game') {
      invoke('get_game_status').then((status: unknown) => {
        if (status === 'ingame') {
          suppressGameEntryTracking();
        }
        settingsStore.setDropsTrackerTimersInGame(status === 'ingame');
      }).catch(() => {
        settingsStore.setDropsTrackerTimersInGame(false);
      });
    }

    listen<{ visible?: boolean } | null>('toggle-loot-history', async (event) => {
      if (mode !== 'game') return;
      const next = event.payload?.visible ?? !historyVisible;
      if (next === historyVisible) return;
      historyVisible = next;
      try {
        await invoke('set_overlay_interactive', { active: historyVisible });
      } catch (err) {
        console.error('[Overlay] set_overlay_interactive (history) failed:', err);
      }
    }).then(u => unlisteners.push(u));

    if (mode === 'game') {
      // Periodically sync overlay position with Diablo II window
      syncTimer = window.setInterval(() => {
        invoke('sync_overlay_with_game').catch(() => {
          // Silent: game might not be running or not focused
        });
      }, 250);
    }

    return () => {
      document.removeEventListener('pointermove', moveTrackerOverlay);
      document.removeEventListener('pointerup', endTrackerOverlayDrag);
      document.removeEventListener('pointercancel', endTrackerOverlayDrag);
      if (mode === 'tracker') {
        document.documentElement.classList.remove('tracker-overlay-mode');
        document.body.classList.remove('tracker-overlay-mode');
      }
      window.removeEventListener('achievement-test-popup', testAchievementPopup);
      unlisteners.forEach(u => u());
      itemsDictionaryStore.destroy();
      if (syncTimer !== null) {
        clearInterval(syncTimer);
      }
      clearInterval(displayTimer);
      clearPendingTimerPause();
      // Clear all removal timers
      removalTimers.forEach(timer => clearTimeout(timer));
      removalTimers.clear();
    };
  });
</script>

<main class="overlay" class:unlocked={trackerEditActive} class:layout-editing={editActive} class:tracker-window={mode === 'tracker'}>
  {#if mode === 'game'}
    {#if notificationOverlayEnabled && (!editActive || notificationLayoutItems.length > 0)}
      <NotificationStack
        items={notificationLayoutItems}
        x={notificationX}
        y={notificationY}
        width={notificationWidth}
        height={notificationHeight}
        maxVisible={10}
        fontSize={notificationFontSize}
        opacity={notificationOpacity}
        stackDirection={notificationStackDirection}
      />
    {/if}
    {#if achievementSettings.overlayEnabled && (editActive || achievementPopups.length > 0)}
      <div class="achievement-popup-stack" style={achievementPopupStackStyle()}>
        {#if editActive}
          <div
            class="achievement-popup"
            style={`font-size:${achievementSettings.overlayFontSize}px;opacity:${achievementSettings.overlayOpacity};`}
          >
            <span class="achievement-popup-kicker">Achievement Unlocked!</span>
            <strong>{overlayLayoutAchievementPreviewName}</strong>
          </div>
        {:else}
          {#each achievementPopups as popup (popup.popupId)}
            <div
              class="achievement-popup"
              class:exiting={popup.exiting}
              style={`font-size:${achievementSettings.overlayFontSize}px;opacity:${achievementSettings.overlayOpacity};`}
            >
              <span class="achievement-popup-kicker">Achievement Unlocked!</span>
              <strong>{popup.name}</strong>
            </div>
          {/each}
        {/if}
      </div>
    {/if}
    {#if mulingIndicatorOverlayEnabled}
      {@const _hkLabel = (mulingModeHotkey?.keyCode ?? 0) === 0 && (mulingModeHotkey?.modifiers ?? 0) === 0 ? 'None' : (mulingModeHotkey?.display ?? 'None')}
      <div
        class="muling-indicator"
        class:muling-active={dropsTrackerMulingMode}
        style={typeof mulingIndicatorPosition.x === 'number' && typeof mulingIndicatorPosition.y === 'number'
          ? `left:${mulingIndicatorPosition.x}px;top:${mulingIndicatorPosition.y}px;width:${mulingIndicatorWidth}px;height:${mulingIndicatorHeight}px;`
          : `right:16px;bottom:16px;width:${mulingIndicatorWidth}px;height:${mulingIndicatorHeight}px;`}
      >
        <span class="mi-key">{_hkLabel}</span>
        <div class="mi-mbox">M</div>
      </div>
    {/if}
  {/if}
  {#if historyVisible && mode === 'game'}
    <LootHistoryPanel onClose={() => {
      historyVisible = false;
      invoke('set_overlay_interactive', { active: false }).catch(() => {});
    }} />
  {/if}
  <div class="tracker-window-content" bind:this={trackerWindowScrollEl} onwheel={scrollTrackerWindow}>
  <!-- Drops Tracker overlays -->
  {#if dropsTrackerEnabled && (renderGameTrackerCards || renderTrackerWindow)}
    <div class="tracker-overlay-position drop-tracker-left" class:unlocked={trackerEditActive} style={trackerOverlayStyle(dropsTrackerOverlayPosition, 'drops')}>
      <div class="tracker-card" class:unlocked={trackerEditActive} role="complementary" aria-label="Drops Tracker counter" onpointerdown={(event) => beginTrackerOverlayDrag('drops', event)}>
        <div class="tracker-header">
          <span class="tracker-title">Drops Tracker</span>
          {#if trackerEditActive}<span class="drag-hint">Drag</span>{/if}
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
          {#if trackerEditActive && (dropsTrackerRunTimerEnabled || dropsTrackerSessionTimerEnabled)}
            <div class="tracker-timer-actions">
              {#if dropsTrackerRunTimerEnabled}<button type="button" class="tracker-reset-button" onpointerdown={resetRunTimer}>Reset Run</button>{/if}
              {#if dropsTrackerSessionTimerEnabled}<button type="button" class="tracker-reset-button" onpointerdown={resetSessionTimer}>Reset Session</button>{/if}
            </div>
          {/if}
        </div>
      </div>
    </div>
  {/if}

  {#if totalDropsTrackerEnabled && (renderGameTrackerCards || renderTrackerWindow)}
    <div class="tracker-overlay-position total-drops-overlay" class:unlocked={trackerEditActive} style={trackerOverlayStyle(totalDropsOverlayPosition, 'total')}>
      <div class="tracker-card total-drops-card" class:unlocked={trackerEditActive} role="complementary" aria-label="Total Drops counter" onpointerdown={(event) => beginTrackerOverlayDrag('total', event)}>
        <div class="tracker-header">
          <span class="tracker-title">Total Drops</span>
          {#if trackerEditActive}<span class="drag-hint">Drag</span>{/if}
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
      </div>
    </div>
  {/if}


  {#if holyGrailOverlayEnabled && (renderGameTrackerCards || renderTrackerWindow)}
    {#key holyGrailOverlayRenderKey}
    <div class="tracker-overlay-position grail-progress-overlay" class:unlocked={trackerEditActive} style={trackerOverlayStyle(holyGrailOverlayPosition, 'grail')}>
      <div class="tracker-card grail-progress-card" class:unlocked={trackerEditActive} role="complementary" aria-label="Grail Progress" onpointerdown={(event) => beginTrackerOverlayDrag('grail', event)}>
        <div class="tracker-header">
          <span class="tracker-title">Grail Progress</span>
          {#if trackerEditActive}<span class="drag-hint">Drag</span>{/if}
        </div>
        <div class="tracker-rows">
          {#if holyGrailOverlayShowTotal}
            <div class="tracker-row grail-total-row">
              <span class="tracker-label grail-total">{holyGrailTotalProgress.percent.toFixed(1)}% Complete</span>
              <span class="tracker-count">{holyGrailTotalProgress.found}/{holyGrailTotalProgress.total}</span>
            </div>
          {/if}
          {#each HOLY_GRAIL_CATEGORIES.filter((category) => holyGrailOverlayCategories[category.key]) as category (category.key + '-' + overlayRefreshNonce)}
            {@const categoryProgress = holyGrailCategoryProgress(holyGrailItems, holyGrailFound, category.key)}
            <div class="tracker-row">
              <span class="tracker-label category-{category.key}">{holyGrailCategoryLabel(category.key)}</span>
              <span class="tracker-count">{categoryProgress.found}/{categoryProgress.total}</span>
            </div>
          {/each}
          {#if holyGrailOverlayShowLatest}
            <div class="tracker-row latest-row">
              <span class="tracker-label latest">Latest</span>
              <span class="tracker-count latest-name">{holyGrailLatestFind?.name ?? '—'}</span>
            </div>
          {/if}
        </div>
      </div>
    </div>
    {/key}
  {/if}

  {#if runeTrackerOverlayEnabled && (renderGameTrackerCards || renderTrackerWindow)}
    <div class="tracker-overlay-position rune-tracker-overlay" class:unlocked={trackerEditActive} style={trackerOverlayStyle(runeTrackerOverlayPosition, 'runes')}>
      <div class="tracker-card rune-tracker-card" class:unlocked={trackerEditActive} role="complementary" aria-label="Rune Tracker" onpointerdown={(event) => beginTrackerOverlayDrag('runes', event)}>
        <div class="tracker-header">
          <span class="tracker-title">Rune Tracker</span>
          {#if trackerEditActive}<span class="drag-hint">Drag</span>{/if}
        </div>
        <div class="tracker-rows rune-overlay-rows">
          {#each runeTrackerRows as row (row.key + '-' + overlayRefreshNonce)}
            <div class="tracker-row rune-overlay-row">
              <span class="tracker-label rune-name rune-tier-{row.tier}">{row.label}</span>
              <span class="tracker-count">{row.count}</span>
            </div>
          {/each}
        </div>
      </div>
    </div>
  {/if}

  {#if fateCardTrackerOverlayEnabled && (renderGameTrackerCards || renderTrackerWindow)}
    <div class="tracker-overlay-position fate-card-tracker-overlay" class:unlocked={trackerEditActive} style={trackerOverlayStyle(fateCardTrackerOverlayPosition, 'fate-cards')}>
      <div class="tracker-card fate-card-tracker-card" class:unlocked={trackerEditActive} role="complementary" aria-label="Fate Cards Tracker" onpointerdown={(event) => beginTrackerOverlayDrag('fate-cards', event)}>
        <div class="tracker-header">
          <span class="tracker-title">Fate Cards</span>
          {#if trackerEditActive}<span class="drag-hint">Drag</span>{/if}
        </div>
        <div class="tracker-rows fate-card-overlay-rows">
          {#each fateCardRows as row (row.key + '-' + overlayRefreshNonce)}
            <div class="tracker-row fate-card-overlay-row">
              <span class="tracker-label fate-card-name fate-card-tier-{row.tier}" title={row.label}>{row.label}</span>
              <span class="tracker-count">{row.count}</span>
            </div>
          {/each}
        </div>
      </div>
    </div>
  {/if}

  {#if materialTrackerOverlayEnabled && (renderGameTrackerCards || renderTrackerWindow)}
    <div class="tracker-overlay-position mats-tracker-overlay" class:unlocked={trackerEditActive} style={trackerOverlayStyle(materialTrackerOverlayPosition, 'materials')}>
      <div class="tracker-card mats-tracker-card" class:unlocked={trackerEditActive} role="complementary" aria-label="Mats Tracker" onpointerdown={(event) => beginTrackerOverlayDrag('materials', event)}>
        <div class="tracker-header">
          <span class="tracker-title">Mats Tracker</span>
          {#if trackerEditActive}<span class="drag-hint">Drag</span>{/if}
        </div>
        <div class="tracker-rows material-overlay-rows">
          {#each materialTrackerRows as row (row.key + '-' + overlayRefreshNonce)}
            <div class="tracker-row material-overlay-row">
              <span class="tracker-label material-name">{row.label}</span>
              <span class="tracker-count">{row.count}</span>
            </div>
          {/each}
        </div>
      </div>
    </div>
  {/if}

  {#if achievementProgressOverlayEnabled && (renderGameTrackerCards || renderTrackerWindow)}
    <div class="tracker-overlay-position achievement-progress-overlay" class:unlocked={trackerEditActive} style={trackerOverlayStyle(achievementProgressOverlayPosition, 'achievements')}>
      <div class="tracker-card compact-stat-card" class:unlocked={trackerEditActive} role="complementary" aria-label="Achievement Progress" onpointerdown={(event) => beginTrackerOverlayDrag('achievements', event)}>
        <div class="tracker-header">
          <span class="tracker-title">Achievements</span>
          {#if trackerEditActive}<span class="drag-hint">Drag</span>{/if}
        </div>
        <div class="compact-stat-body">
          <strong>{achievementUnlockedCount}/{achievementTotalCount}</strong>
          <span>{achievementPercent.toFixed(1)}% Complete</span>
        </div>
      </div>
    </div>
  {/if}

  {#if monsterKillsOverlayEnabled && (renderGameTrackerCards || renderTrackerWindow)}
    <div class="tracker-overlay-position monster-kills-overlay" class:unlocked={trackerEditActive} style={trackerOverlayStyle(monsterKillsOverlayPosition, 'kills')}>
      <div class="tracker-card compact-stat-card" class:unlocked={trackerEditActive} role="complementary" aria-label="Total Monster Kills" onpointerdown={(event) => beginTrackerOverlayDrag('kills', event)}>
        <div class="tracker-header">
          <span class="tracker-title">Total Kills</span>
          {#if trackerEditActive}<span class="drag-hint">Drag</span>{/if}
        </div>
        <div class="compact-stat-body kills-stat-body">
          <strong>{monsterKillsTotal.toLocaleString()}</strong>
        </div>
      </div>
    </div>
  {/if}
  </div>

</main>

<style>
  :global(body) {
    background: var(--bg-overlay) !important;
  }

  :global(html.tracker-overlay-mode),
  :global(body.tracker-overlay-mode),
  :global(html.tracker-overlay-mode body) {
    width: 100%;
    height: 100%;
    min-height: 100%;
    margin: 0;
    background: var(--bg-primary) !important;
    overflow: hidden !important;
  }

  :global(html.tracker-overlay-mode #app) {
    width: 100vw !important;
    height: 100vh !important;
    min-height: 100vh !important;
    overflow: hidden !important;
  }
  
  .overlay {
    position: fixed;
    inset: 0;
    background: var(--bg-overlay);
    pointer-events: none;
  }

  .overlay.unlocked {
    pointer-events: auto;
  }

  .overlay.tracker-window {
    position: fixed;
    inset: 0;
    width: 100vw;
    height: 100vh;
    min-height: 0;
    background: var(--bg-primary);
    pointer-events: auto;
    overflow: hidden;
  }

  .tracker-window-content {
    display: contents;
  }

  .tracker-window .tracker-window-content {
    display: grid;
    grid-template-columns: minmax(0, max-content);
    align-items: start;
    gap: 6px;
    width: 100%;
    height: 100vh;
    min-height: 0;
    padding: 10px;
    box-sizing: border-box;
    overflow-x: hidden;
    overflow-y: auto;
    overscroll-behavior: contain;
  }

  /* ── Drops Tracker Counters ────────────────────────────────────── */
  .tracker-overlay-position {
    position: fixed;
    pointer-events: none;
    z-index: 20;
  }

  .tracker-window .tracker-overlay-position {
    position: static !important;
    left: auto !important;
    right: auto !important;
    top: auto !important;
    bottom: auto !important;
    transform: none !important;
    pointer-events: auto;
    z-index: auto;
  }

  .tracker-overlay-position.unlocked {
    pointer-events: auto;
  }

  .tracker-card {
    width: var(--tracker-card-width, 238px);
    min-width: var(--tracker-card-width, 238px);
    max-width: var(--tracker-card-width, 238px);
    height: var(--tracker-card-height, auto);
    max-height: var(--tracker-card-height, none);
    box-sizing: border-box;
    overflow: auto;
    background: var(--overlay-panel-bg, rgba(0, 0, 0, 0.82));
    border: 1px solid var(--overlay-panel-border, rgba(255, 200, 80, 0.35));
    border-radius: 6px;
    padding: 6px 10px 8px;
    font-family: var(--font-mono, monospace);
    font-size: 12px;
    line-height: 1.25;
    color: var(--text-primary, #e8e8e8);
    pointer-events: none;
    text-rendering: geometricPrecision;
    user-select: none;
  }

  .tracker-window .tracker-card {
    width: 100%;
    min-width: 0;
    max-width: none;
    box-sizing: border-box;
    pointer-events: auto;
    cursor: move;
    touch-action: none;
  }

  .tracker-window .tracker-card.unlocked {
    cursor: move;
    border-style: solid;
    box-shadow: none;
    touch-action: none;
  }

  .tracker-card.unlocked {
    pointer-events: auto;
    cursor: move;
    border-style: dashed;
    box-shadow: 0 0 0 1px var(--overlay-panel-border, rgba(255, 200, 80, 0.22)), 0 0 14px var(--accent-primary-muted, rgba(255, 200, 80, 0.18));
    touch-action: none;
  }

  .total-drops-card {
    border-color: var(--overlay-panel-border, rgba(120, 190, 255, 0.35));
  }

  .grail-progress-card {
    width: var(--tracker-card-width, 238px);
    min-width: var(--tracker-card-width, 238px);
    max-width: var(--tracker-card-width, 238px);
    height: var(--tracker-card-height, auto);
    max-height: var(--tracker-card-height, none);
    border-color: var(--overlay-panel-border, rgba(214, 162, 58, 0.45));
  }

  .rune-tracker-card {
    width: var(--tracker-card-width, 278px);
    min-width: var(--tracker-card-width, 278px);
    max-width: var(--tracker-card-width, 278px);
    height: var(--tracker-card-height, 560px);
    max-height: var(--tracker-card-height, 560px);
    overflow: auto;
    border-color: var(--overlay-panel-border, rgba(201, 168, 93, 0.58));
  }

  .mats-tracker-card {
    width: var(--tracker-card-width, 278px);
    min-width: var(--tracker-card-width, 278px);
    max-width: var(--tracker-card-width, 278px);
    height: var(--tracker-card-height, 420px);
    max-height: var(--tracker-card-height, 420px);
    overflow: auto;
    border-color: var(--overlay-panel-border, rgba(201, 168, 93, 0.58));
  }

  .fate-card-tracker-card {
    width: var(--tracker-card-width, 238px);
    min-width: var(--tracker-card-width, 238px);
    max-width: var(--tracker-card-width, 238px);
    height: var(--tracker-card-height, 180px);
    max-height: var(--tracker-card-height, 180px);
    overflow: auto;
    border-color: var(--overlay-panel-border, rgba(215, 168, 255, 0.55));
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

  .total-drops-card .tracker-title {
    color: var(--accent-primary, #f6c33d);
  }

  .grail-progress-card .tracker-title {
    color: var(--accent-primary, #f6c33d);
  }

  .rune-tracker-card .tracker-title {
    color: var(--accent-primary, #f6c33d);
  }

  .mats-tracker-card .tracker-title {
    color: var(--accent-primary, #f6c33d);
  }

  .fate-card-tracker-card .tracker-title {
    color: var(--accent-primary, #f6c33d);
  }

  .compact-stat-card {
    display: flex;
    flex-direction: column;
    justify-content: center;
    overflow: hidden;
    border-color: var(--overlay-panel-border, rgba(201, 168, 93, 0.58));
  }

  .compact-stat-body {
    display: grid;
    gap: 2px;
    place-items: center;
    text-align: center;
  }

  .compact-stat-body strong {
    color: var(--text-primary, #ffffff);
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

  .drag-hint {
    grid-column: 3;
    justify-self: end;
    color: var(--accent-primary, #f6c33d);
    font-size: 10px;
    font-weight: 800;
    opacity: 0.78;
    text-transform: uppercase;
  }

  .tracker-window .drag-hint {
    display: none;
  }

  .tracker-rows {
    display: flex;
    flex-direction: column;
    gap: 3px;
  }

  .tracker-row {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    gap: 12px;
    line-height: 1.25;
  }

  .total-row,
  .run-row {
    margin-top: 3px;
    padding-top: 3px;
    border-top: 1px solid var(--border-primary);
  }

  .timer-row {
    margin-top: 1px;
  }

  .tracker-timer-actions {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 5px;
    margin-top: 6px;
    padding-top: 6px;
    border-top: 1px solid var(--border-primary);
  }

  .tracker-reset-button {
    border: 1px solid var(--overlay-panel-border, rgba(255, 200, 80, 0.35));
    border-radius: 4px;
    background: var(--accent-primary-muted, rgba(255, 200, 80, 0.12));
    color: var(--text-primary, rgba(255, 255, 255, 0.9));
    cursor: pointer;
    font-family: inherit;
    font-size: 9px;
    font-weight: 700;
    padding: 3px 4px;
    text-transform: uppercase;
  }

  .tracker-reset-button:hover {
    background: var(--btn-secondary-hover, rgba(255, 200, 80, 0.22));
  }

  .tracker-label {
    color: var(--text-primary, #f7f7f7);
    font-size: 12px;
    font-weight: 700;
    min-width: 0;
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
  .category-uniqueJewels { color: #ff5cff; }
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
  .tracker-label.total,
  .tracker-label.run,
  .tracker-label.grail-total,
  .tracker-label.latest { color: var(--text-primary, #ffffff); }

  .rune-overlay-rows {
    max-height: calc(min(72vh, 720px) - 28px);
    overflow: hidden;
    gap: 1px;
  }

  .rune-overlay-row {
    gap: 6px;
  }

  .material-overlay-rows {
    max-height: calc(min(72vh, 720px) - 28px);
    overflow: hidden;
    gap: 2px;
  }

  .material-overlay-row {
    gap: 6px;
  }

  .fate-card-overlay-rows {
    max-height: calc(min(72vh, 720px) - 28px);
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

  .latest-row {
    margin-top: 3px;
    padding-top: 3px;
    border-top: 1px solid var(--border-primary);
  }

  .latest-name {
    max-width: 150px;
    overflow: hidden;
    font-size: 12px;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .tracker-count {
    font-size: 12px;
    font-weight: 800;
    color: var(--text-primary, #fff);
    min-width: 24px;
    text-align: right;
  }

  .muling-indicator {
    position: fixed;
    z-index: 30;
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
    pointer-events: none;
    user-select: none;
    font-family: var(--font-mono, monospace);
  }
  .muling-indicator.muling-active {
    border-color: var(--accent-primary, rgba(255, 210, 50, 0.95));
    box-shadow: 0 0 12px var(--accent-primary-muted, rgba(255, 200, 40, 0.5));
  }
  .mi-key {
    display: block;
    max-width: 100%;
    overflow: hidden;
    font-size: 10px;
    font-weight: 800;
    letter-spacing: 0;
    text-transform: uppercase;
    color: var(--accent-primary, #ffd580);
    text-align: center;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .muling-indicator.muling-active .mi-key { color: var(--text-primary, #fff5cc); }
  .mi-mbox {
    width: min(28px, calc(100% - 4px));
    height: min(28px, calc(100% - 20px));
    min-height: 20px;
    display: flex;
    align-items: center;
    justify-content: center;
    border: 2px solid var(--border-secondary, rgba(180, 180, 180, 0.55));
    border-radius: 4px;
    font-size: 16px;
    font-weight: 900;
    color: var(--text-primary, #e0e0e0);
    line-height: 1;
  }
  .muling-indicator.muling-active .mi-mbox {
    border-color: var(--accent-primary, rgba(255, 210, 50, 0.95));
    color: var(--text-primary, #fff5cc);
  }

  .achievement-popup-stack {
    position: fixed;
    width: var(--achievement-popup-width, 300px);
    display: grid;
    gap: 8px;
    z-index: 10020;
    pointer-events: none;
  }

  .achievement-popup {
    width: 100%;
    min-height: var(--achievement-popup-min-height, 88px);
    display: grid;
    align-content: center;
    justify-items: center;
    gap: 2px;
    padding: 9px 13px;
    border: 1px solid var(--accent-primary);
    background:
      linear-gradient(180deg, color-mix(in srgb, var(--bg-secondary) 96%, transparent), color-mix(in srgb, var(--bg-primary) 94%, transparent));
    box-shadow: 0 0 24px color-mix(in srgb, var(--accent-secondary) 32%, transparent);
    color: var(--text-primary);
    font-family: var(--font-display);
    text-align: center;
    animation: achievement-enter 240ms ease-out;
  }

  .achievement-popup.exiting {
    animation: achievement-exit 240ms ease-in forwards;
  }

  .achievement-popup-kicker {
    color: var(--accent-primary);
    font-family: var(--font-display);
    font-size: 0.9em;
    letter-spacing: 0;
    line-height: 1.05;
  }

  .achievement-popup strong {
    max-width: 100%;
    font-size: 1.08em;
    font-weight: 400;
    line-height: 1.12;
    overflow-wrap: anywhere;
  }

  @keyframes achievement-enter {
    from {
      opacity: 0;
      transform: translateY(-12px) scale(0.98);
    }
    to {
      opacity: 1;
      transform: translateY(0) scale(1);
    }
  }

  @keyframes achievement-exit {
    to {
      opacity: 0;
      transform: translateY(-10px) scale(0.98);
    }
  }
</style>
