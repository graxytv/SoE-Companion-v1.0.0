/**
 * Settings store for D2MXLUtils
 *
 * Manages application settings with persistence through Tauri backend.
 * Uses Svelte 5 runes for reactive state management.
 */

import { invoke } from "@tauri-apps/api/core";
import { emit, listen, type UnlistenFn } from "@tauri-apps/api/event";
import {
  defaultDropsTrackerCategories,
  defaultTotalDropsTrackerCategories,
  emptyDropTrackerCounts,
  normalizeCategorySettings,
  normalizeCounts,
  normalizeRuneTrackerCounts,
  normalizeRuneTrackerVisibility,
  defaultRuneTrackerCounts,
  defaultRuneTrackerVisibility,
  incrementCounts,
  decrementCounts,
  runeNameFromDrop,
  RUNE_NAMES,
  type DropTrackerCategoryKey,
  type DropTrackerCategorySettings,
  type DropTrackerCounts,
  type RuneName,
  type RuneTrackerCounts,
  type RuneTrackerVisibility,
} from "../lib/drop-tracker-categories";
import {
  buildHolyGrailItems,
  canonicalHolyGrailItem,
  canonicalTrackedItemName,
  cleanTrackedItemName,
  defaultHolyGrailOverlayCategories,
  holyGrailItemFromDrop,
  normalizeHolyGrailOverlayCategories,
  type HolyGrailCategoryKey,
  type HolyGrailCategorySettings,
  type HolyGrailFoundEntry,
  type HolyGrailItemLike,
} from "../lib/holy-grail";
import {
  normalizeGsfItemName,
  type GsfItemCategory,
  type GsfItemSlot,
} from "../lib/gsf-item-catalog";
import type { GsfPlayer, GsfWantedItem, GsfWantedStatus } from "../lib/gsf-tracker";
import {
  DEFAULT_ACHIEVEMENT_SETTINGS,
  ELITE_UNIQUE_NAMES,
  achievementDetail,
  defaultAchievementStats,
  evaluateAchievements,
  materialAchievementKey,
  materialAchievementNameFromDrop,
  normalizeAchievementSettings,
  normalizeAchievementStats,
  type AchievementContext,
  type AchievementCharacterLevel,
  type AchievementSettings,
  type AchievementStats,
  type AchievementUnlockEntry,
} from "../lib/achievements";
import {
  normalizeItemSoundRules,
  type ItemSoundRule,
  type ItemSoundRules,
} from "../lib/item-sounds";
import {
  MATERIAL_TRACKER_NAMES,
  defaultMaterialTrackerCounts,
  defaultMaterialTrackerVisibility,
  normalizeMaterialTrackerCounts,
  normalizeMaterialTrackerVisibility,
  type MaterialTrackerCounts,
  type MaterialTrackerName,
  type MaterialTrackerVisibility,
} from "../lib/material-tracker";
import {
  SOE_13_FATE_CARD_ITEMS,
  SOE_13_FATE_CARD_TIERS,
  fateCardInfo,
  fateCardTierKey,
} from "../lib/soe-13-items";

/** Hotkey configuration interface */
export interface HotkeyConfig {
  /** Virtual key code (e.g., 0x4B for 'K') */
  keyCode: number;
  /** Modifier flags (Ctrl, Shift, Alt, Win) */
  modifiers: number;
  /** Human-readable representation (e.g., "Ctrl+K") */
  display: string;
}

export interface OverlayPosition {
  /** Left offset in overlay-window pixels. Null means use the default side anchor. */
  x: number | null;
  /** Top offset in overlay-window pixels. Null means use the default bottom anchor. */
  y: number | null;
}

export interface DropTrackerRecentItem {
  id: string;
  timestampMs: number;
  name: string;
  isNewGrail?: boolean;
  is_new_grail?: boolean;
  categories: DropTrackerCategoryKey[];
  dropsTrackerCategories: DropTrackerCategoryKey[];
  totalDropsTrackerCategories: DropTrackerCategoryKey[];
  source?: string;
}

export interface DropTrackerStateSnapshot {
  dropsTrackerCounts?: DropTrackerCounts;
  totalDropsTrackerCounts?: DropTrackerCounts;
  dropsTrackerRecentItems?: DropTrackerRecentItem[];
  runeTrackerCounts?: RuneTrackerCounts;
  materialTrackerCounts?: MaterialTrackerCounts;
  fateCardCounts?: FateCardCounts;
  fateCardDropCounts?: FateCardCounts;
  holyGrailFound?: HolyGrailFoundMap;
  achievementStats?: AchievementStats;
}

export type HolyGrailFoundMap = Record<string, HolyGrailFoundEntry>;
export type FateCardCounts = Partial<Record<string, number>>;
export type FateCardTrackerVisibility = Partial<Record<string, boolean>>;
export type SaveExitAutomationDifficulty = "Normal" | "Nightmare" | "Hell";
export interface HolyGrailBackupStatus {
  backupExists: boolean;
  backupPath: string;
  foundCount: number;
  exportedAt: string | null;
}

export interface FateCardBackupStatus {
  backupExists: boolean;
  backupPath: string;
  cardCount: number;
  exportedAt: string | null;
}

export type { GsfPlayer, GsfWantedItem, GsfWantedStatus, GsfItemCategory, GsfItemSlot };

/** Source of audio for a sound slot. */
export type SoundSource =
  | { kind: "default" }
  | { kind: "custom"; fileName: string }
  | { kind: "empty" };

/** One configurable drop-sound slot. Slot index = position in `sounds` + 1. */
export interface SoundSlot {
  label: string;
  volume: number;
  source: SoundSource;
}

/** Application settings interface */
export type AppTheme = "sanctuary" | "horadric" | "hellfire" | "dark" | "light";

export interface AppSettings {
  /** UI theme. */
  theme: AppTheme;
  /** Master multiplier for drop notification sounds (0.0 - 1.0). Final played gain = `soundVolume * slot.volume`. */
  soundVolume: number;
  /** Active loot filter profile name */
  activeProfile: string | null;
  /** Path to the SoE launcher executable. */
  soeLauncherPath: string | null;
  /** Path to the user's ProjectD2 folder. */
  projectD2Path: string | null;
  /** Selected ProjectD2 shared stash file used by the Runeword Planner. */
  runewordPlannerStashPath: string | null;
  /** Saved main tab order. Unknown/missing tabs are ignored and new tabs append. */
  mainTabOrder: string[];
  /** When true, the user has completed the guided setup wizard at least once. */
  setupWizardCompleted: boolean;
  /** Saved Drops Tracker sub-tab order. */
  dropsTrackerSubTabOrder: string[];
  /** Saved Holy Grail sub-tab order. */
  holyGrailSubTabOrder: string[];
  /** Account-wide achievement progress, manual totals, unlocks, and history. */
  achievementStats: AchievementStats;
  /** Achievement overlay and sound settings. */
  achievementSettings: AchievementSettings;
  /** Notification display duration in milliseconds */
  notificationDuration: number;
  /** Notification stack direction: "up" or "down" */
  notificationStackDirection: string;
  /** Notification font size in pixels */
  notificationFontSize: number;
  /** Notification background opacity (0.0 - 1.0) */
  notificationOpacity: number;
  /** When true, visual item drop notifications are rendered on the overlay. */
  notificationOverlayEnabled: boolean;
  /** When true, unidentified unique/set scanner drops can still show notifications. */
  notifyUnidentifiedUniqueSetDrops: boolean;
  /** Per-item sound overrides keyed by canonical item/rune/material key. */
  itemSoundRules: ItemSoundRules;
  /** Notification position X offset from edge (percentage 0-100) */
  notificationX: number;
  /** Notification position Y offset from edge (percentage 0-100) */
  notificationY: number;
  /** Notification card width in pixels. */
  notificationWidth: number;
  /** Notification stack height in pixels. */
  notificationHeight: number;
  /** When true, drop the unique/set name line for Set/TU/SU/SSU/SSSU items
   *  and show only the base type. Stat-flagged rules ignore this. */
  compactName: boolean;
  /** Hotkey configuration for toggling main window */
  toggleWindowHotkey: HotkeyConfig;
  /** Hotkey held to enter overlay edit mode (drag notification anchor) */
  editOverlayHotkey: HotkeyConfig;
  /** Hotkey held to reveal every item on the ground, bypassing `hide` rules */
  revealHiddenHotkey: HotkeyConfig;
  /** Hotkey to toggle the in-game loot history overlay panel */
  lootHistoryHotkey: HotkeyConfig;
  /** Hotkey to manually reset the Drops Tracker counts/history */
  resetDropsTrackerHotkey: HotkeyConfig;
  /** Hotkey to toggle Muling Mode on/off */
  mulingModeHotkey: HotkeyConfig;
  /** Hotkey that runs the experimental game reset automation. */
  gameResetHotkey: HotkeyConfig;
  /** When true, show the Drops Tracker counter on the overlay */
  dropsTrackerEnabled: boolean;
  /** When true, show the Run Counter row on the Drops Tracker overlay. */
  dropsTrackerRunCounterEnabled: boolean;
  /** When true, show the current run timer row on the Drops Tracker overlay. */
  dropsTrackerRunTimerEnabled: boolean;
  /** When true, show the total session timer row on the Drops Tracker overlay. */
  dropsTrackerSessionTimerEnabled: boolean;
  /** Number of games entered since the run counter was last reset. */
  dropsTrackerRunCount: number;
  /** Timestamp in ms when the current run timer started. */
  dropsTrackerRunStartedAtMs: number;
  /** Timestamp in ms when the current session timer started. Legacy/migration field. */
  dropsTrackerSessionStartedAtMs: number;
  /** Accumulated elapsed time for the current run timer, excluding muling pauses. */
  dropsTrackerRunElapsedMs: number;
  /** Accumulated elapsed time for the total session timer, persisted across app launches and excluding muling pauses. */
  dropsTrackerSessionElapsedMs: number;
  /** Timestamp in ms of the last timer accumulation tick. Reset on app load so closed-app time is not counted. */
  dropsTrackerTimerLastTickAtMs: number;
  /** When true, show the persistent Total Drops overlay. */
  totalDropsTrackerEnabled: boolean;
  /** Per-category tracking/display toggles for the Drops Tracker overlay. */
  dropsTrackerCategories: DropTrackerCategorySettings;
  /** Per-category tracking/display toggles for the Total Drops overlay. */
  totalDropsTrackerCategories: DropTrackerCategorySettings;
  /** Counts for the manually reset Drops Tracker overlay. */
  dropsTrackerCounts: DropTrackerCounts;
  /** Persistent counts for the Total Drops overlay. */
  totalDropsTrackerCounts: DropTrackerCounts;
  /** Most recent items counted by Drops Tracker, newest first. */
  dropsTrackerRecentItems: DropTrackerRecentItem[];
  /** When true, suppress repeated sightings of the same item during a single run. */
  dropsTrackerPreventDuplicates: boolean;
  /** When true, temporarily pause drop/holy-grail tracking while muling items. */
  dropsTrackerMulingMode: boolean;
  /** Timestamp in ms when Muling Mode was enabled, used to pause run/session timers. */
  dropsTrackerMulingStartedAtMs: number | null;
  /** When true, show the compact Muling Mode hotkey/status indicator while Muling Mode is active. */
  mulingIndicatorOverlayEnabled: boolean;
  /** When true, tracker-style overlays render in a separate movable window. */
  trackerOverlaysSeparateWindow: boolean;
  /** When true, game overlays remain visible even when Diablo II is not foreground. */
  alwaysShowOverlays: boolean;
  /** Saved position for the Drops Tracker overlay. */
  dropsTrackerOverlayPosition: OverlayPosition;
  /** Width in pixels for the Drops Tracker overlay. */
  dropsTrackerOverlayWidth: number;
  /** Height in pixels for the Drops Tracker overlay. */
  dropsTrackerOverlayHeight: number;
  /** Saved position for the Total Drops overlay. */
  totalDropsOverlayPosition: OverlayPosition;
  /** Width in pixels for the Total Drops overlay. */
  totalDropsOverlayWidth: number;
  /** Height in pixels for the Total Drops overlay. */
  totalDropsOverlayHeight: number;
  /** Counts for individual rune drops. */
  runeTrackerCounts: RuneTrackerCounts;
  /** When true, show the Rune Tracker overlay. */
  runeTrackerOverlayEnabled: boolean;
  /** Per-rune visibility for the Rune Tracker overlay. */
  runeTrackerOverlayRunes: RuneTrackerVisibility;
  /** Saved position for the Rune Tracker overlay. */
  runeTrackerOverlayPosition: OverlayPosition;
  /** Width in pixels for the Rune Tracker overlay. */
  runeTrackerOverlayWidth: number;
  /** Height in pixels for the Rune Tracker overlay. */
  runeTrackerOverlayHeight: number;
  /** Counts for individual material drops. */
  materialTrackerCounts: MaterialTrackerCounts;
  /** When true, show the Mats Tracker overlay. */
  materialTrackerOverlayEnabled: boolean;
  /** Per-material visibility for the Mats Tracker overlay. */
  materialTrackerOverlayMaterials: MaterialTrackerVisibility;
  /** Saved position for the Mats Tracker overlay. */
  materialTrackerOverlayPosition: OverlayPosition;
  /** Width in pixels for the Mats Tracker overlay. */
  materialTrackerOverlayWidth: number;
  /** Height in pixels for the Mats Tracker overlay. */
  materialTrackerOverlayHeight: number;
  /** Items found in the Holy Grail checklist, keyed by normalized item/category. */
  holyGrailFound: HolyGrailFoundMap;
  /** Known Fate Card counts keyed by canonical card name. */
  fateCardCounts: FateCardCounts;
  /** Live Fate Card drop counts keyed by canonical card name. */
  fateCardDropCounts: FateCardCounts;
  /** When true, show the Fate Cards tracker overlay. */
  fateCardTrackerOverlayEnabled: boolean;
  /** Per-card visibility for the Fate Cards tracker overlay. */
  fateCardTrackerOverlayCards: FateCardTrackerVisibility;
  /** Per-tier visibility for the Fate Cards tracker overlay. */
  fateCardTrackerOverlayTiers: FateCardTrackerVisibility;
  /** Saved position for the Fate Cards tracker overlay. */
  fateCardTrackerOverlayPosition: OverlayPosition;
  /** Width in pixels for the Fate Cards tracker overlay. */
  fateCardTrackerOverlayWidth: number;
  /** Height in pixels for the Fate Cards tracker overlay. */
  fateCardTrackerOverlayHeight: number;
  /** When true, show the compact Grail Progress overlay. */
  holyGrailOverlayEnabled: boolean;
  /** When true, show total progress on the Grail Progress overlay. */
  holyGrailOverlayShowTotal: boolean;
  /** When true, show the latest newly found grail item on the overlay. */
  holyGrailOverlayShowLatest: boolean;
  /** Category progress rows shown on the Grail Progress overlay. */
  holyGrailOverlayCategories: HolyGrailCategorySettings;
  /** Saved position for the Grail Progress overlay. */
  holyGrailOverlayPosition: OverlayPosition;
  /** Width in pixels for the Grail Progress overlay. */
  holyGrailOverlayWidth: number;
  /** Height in pixels for the Grail Progress overlay. */
  holyGrailOverlayHeight: number;
  /** When true, show the compact achievement progress overlay. */
  achievementProgressOverlayEnabled: boolean;
  /** Saved position for the achievement progress overlay. */
  achievementProgressOverlayPosition: OverlayPosition;
  /** Width in pixels for the achievement progress overlay. */
  achievementProgressOverlayWidth: number;
  /** Height in pixels for the achievement progress overlay. */
  achievementProgressOverlayHeight: number;
  /** Saved position for the achievement unlock popup stack. */
  achievementPopupOverlayPosition: OverlayPosition;
  /** Width in pixels for the achievement unlock popup stack. */
  achievementPopupOverlayWidth: number;
  /** Height in pixels for the achievement unlock popup editor preview. */
  achievementPopupOverlayHeight: number;
  /** When true, show the compact total monster kills overlay. */
  monsterKillsOverlayEnabled: boolean;
  /** Saved position for the total monster kills overlay. */
  monsterKillsOverlayPosition: OverlayPosition;
  /** Width in pixels for the total monster kills overlay. */
  monsterKillsOverlayWidth: number;
  /** Height in pixels for the total monster kills overlay. */
  monsterKillsOverlayHeight: number;
  /** Saved position for the small Muling Mode hotkey/status indicator. */
  mulingIndicatorOverlayPosition: OverlayPosition;
  /** Width in pixels for the Muling Mode hotkey/status indicator. */
  mulingIndicatorOverlayWidth: number;
  /** Height in pixels for the Muling Mode hotkey/status indicator. */
  mulingIndicatorOverlayHeight: number;
  /** Difficulty key used by the experimental save-exit reset automation. */
  saveExitAutomationDifficulty: SaveExitAutomationDifficulty;
  /** Client-area X coordinate used to click Single Player on the main menu. */
  saveExitAutomationClickX: number;
  /** Client-area Y coordinate used to click Single Player on the main menu. */
  saveExitAutomationClickY: number;
  /** When true, click coordinates are interpreted as percentages of the game client. */
  saveExitAutomationCoordinateModePercent: boolean;
  /** Delay between automation input steps. */
  saveExitAutomationDelayMs: number;
  /** Wait after Save & Exit before clicking Single Player. */
  saveExitAutomationMainMenuWaitMs: number;
  /** When true, GSF matching and notifications are enabled. */
  gsfEnabled: boolean;
  /** When true, matched GSF drops add Needed by text to loot notifications. */
  gsfNotificationEnabled: boolean;
  /** 1-based sound slot played when a GSF wanted item drops. Null disables the sound. */
  gsfSoundSlot: number | null;
  /** Volume multiplier for the dedicated GSF match sound (0.0 - 1.0). */
  gsfSoundVolume: number;
  /** Group/shared-find players and wanted item entries. */
  gsfPlayers: GsfPlayer[];
  /** When true, show the rainbow New Item header on first-time Holy Grail drops. */
  holyGrailNewItemNotificationEnabled: boolean;
  /** 1-based sound slot played when a new Holy Grail item drops. Null disables the special sound. */
  holyGrailNewItemSoundSlot: number | null;
  /** Volume multiplier for the dedicated Holy Grail new-item sound (0.0 - 1.0). */
  holyGrailNewItemSoundVolume: number;
  /** When true, scanner logs per-item filter decisions (noisy; opt-in debug). */
  verboseFilterLogging: boolean;
  /** Per-slot drop sounds. Slot index = position + 1.
   *  Played gain = `soundVolume * slot.volume`. */
  sounds: SoundSlot[];
  /** 1-based slot index played when a goblin appears nearby. */
  goblinAlertSlot: number | null;
}

/** Window state interface */
export interface WindowState {
  x: number;
  y: number;
  width: number;
  height: number;
  maximized: boolean;
}

/** Default hotkey (Ctrl+K) */
const DEFAULT_HOTKEY: HotkeyConfig = {
  keyCode: 0x4b, // 'K' key
  modifiers: 0x0002, // MOD_CONTROL
  display: "Ctrl+K",
};

/** Default edit-overlay hotkey (Ctrl+Alt, modifier-only — keyCode 0) */
const DEFAULT_EDIT_OVERLAY_HOTKEY: HotkeyConfig = {
  keyCode: 0,
  modifiers: 0x0001 | 0x0002, // MOD_ALT | MOD_CONTROL
  display: "Ctrl+Alt",
};

const DEFAULT_REVEAL_HIDDEN_HOTKEY: HotkeyConfig = {
  keyCode: 0x5a,
  modifiers: 0,
  display: "Z",
};

const DEFAULT_LOOT_HISTORY_HOTKEY: HotkeyConfig = {
  keyCode: 0x4e, // 'N'
  modifiers: 0,
  display: "N",
};

const DEFAULT_RESET_DROPS_TRACKER_HOTKEY: HotkeyConfig = {
  keyCode: 0,
  modifiers: 0,
  display: "None",
};

const DEFAULT_MULING_MODE_HOTKEY: HotkeyConfig = {
  keyCode: 0,
  modifiers: 0,
  display: "None",
};

const DEFAULT_GAME_RESET_HOTKEY: HotkeyConfig = {
  keyCode: 0x7b,
  modifiers: 0,
  display: "F12",
};

export const DEFAULT_MAIN_TAB_ORDER = [
  "home",
  "general",
  "drops-tracker",
  "lootfilter",
  "notifications",
  "sounds",
  "achievements",
  "holy-grail",
  "fate-cards",
  "soe-wiki",
];

export const DEFAULT_DROPS_TRACKER_SUB_TAB_ORDER = [
  "overview",
  "tracker-settings",
  "drops-overlay",
  "total-overlay",
  "mats-tracker",
  "rune-tracker",
  "muling-mode",
];

export const DEFAULT_HOLY_GRAIL_SUB_TAB_ORDER = [
  "overview",
  "runeword-planner",
  "import",
  "overlay",
  "notifications",
  "backup",
];

function defaultFateCardTrackerCardVisibility(enabled = false): FateCardTrackerVisibility {
  return Object.fromEntries(SOE_13_FATE_CARD_ITEMS.map((card) => [card, enabled]));
}

function defaultFateCardTrackerTierVisibility(enabled = true): FateCardTrackerVisibility {
  return Object.fromEntries(
    SOE_13_FATE_CARD_TIERS.map((tier) => [fateCardTierKey(tier), enabled]),
  );
}

function normalizeFateCardTrackerCardVisibility(
  value: Partial<Record<string, boolean>> | undefined | null,
): FateCardTrackerVisibility {
  const out = defaultFateCardTrackerCardVisibility(false);
  for (const cardName of SOE_13_FATE_CARD_ITEMS) {
    const card = fateCardInfo(cardName);
    if (!card) continue;
    out[card.name] = value?.[card.name] ?? value?.[cardName] ?? false;
  }
  return out;
}

function normalizeFateCardTrackerTierVisibility(
  value: Partial<Record<string, boolean>> | undefined | null,
): FateCardTrackerVisibility {
  const out = defaultFateCardTrackerTierVisibility(true);
  for (const tier of SOE_13_FATE_CARD_TIERS) {
    const key = fateCardTierKey(tier);
    out[key] = value?.[key] ?? true;
  }
  return out;
}

function defaultSounds(): SoundSlot[] {
  return Array.from({ length: 7 }, (_, i) => ({
    label: `Sound ${i + 1}`,
    volume: 0.8,
    source: { kind: "default" as const },
  }));
}

/** Default settings */
const DEFAULT_SETTINGS: AppSettings = {
  theme: "sanctuary",
  soundVolume: 0.8,
  activeProfile: null,
  soeLauncherPath: null,
  projectD2Path: null,
  runewordPlannerStashPath: null,
  mainTabOrder: [...DEFAULT_MAIN_TAB_ORDER],
  setupWizardCompleted: false,
  dropsTrackerSubTabOrder: [...DEFAULT_DROPS_TRACKER_SUB_TAB_ORDER],
  holyGrailSubTabOrder: [...DEFAULT_HOLY_GRAIL_SUB_TAB_ORDER],
  achievementStats: defaultAchievementStats(),
  achievementSettings: { ...DEFAULT_ACHIEVEMENT_SETTINGS },
  notificationDuration: 5000,
  notificationStackDirection: "up",
  notificationFontSize: 14,
  notificationOpacity: 0.9,
  notificationOverlayEnabled: true,
  notifyUnidentifiedUniqueSetDrops: false,
  itemSoundRules: {},
  notificationX: 1.0,
  notificationY: 1.0,
  notificationWidth: 320,
  notificationHeight: 420,
  compactName: false,
  toggleWindowHotkey: DEFAULT_HOTKEY,
  editOverlayHotkey: DEFAULT_EDIT_OVERLAY_HOTKEY,
  revealHiddenHotkey: DEFAULT_REVEAL_HIDDEN_HOTKEY,
  lootHistoryHotkey: DEFAULT_LOOT_HISTORY_HOTKEY,
  resetDropsTrackerHotkey: DEFAULT_RESET_DROPS_TRACKER_HOTKEY,
  mulingModeHotkey: DEFAULT_MULING_MODE_HOTKEY,
  gameResetHotkey: DEFAULT_GAME_RESET_HOTKEY,
  dropsTrackerEnabled: true,
  dropsTrackerRunCounterEnabled: true,
  dropsTrackerRunTimerEnabled: true,
  dropsTrackerSessionTimerEnabled: true,
  dropsTrackerRunCount: 0,
  dropsTrackerRunStartedAtMs: Date.now(),
  dropsTrackerSessionStartedAtMs: Date.now(),
  dropsTrackerRunElapsedMs: 0,
  dropsTrackerSessionElapsedMs: 0,
  dropsTrackerTimerLastTickAtMs: Date.now(),
  totalDropsTrackerEnabled: true,
  dropsTrackerCategories: defaultDropsTrackerCategories(),
  totalDropsTrackerCategories: defaultTotalDropsTrackerCategories(),
  dropsTrackerCounts: emptyDropTrackerCounts(),
  totalDropsTrackerCounts: emptyDropTrackerCounts(),
  dropsTrackerRecentItems: [],
  dropsTrackerPreventDuplicates: false,
  dropsTrackerMulingMode: false,
  dropsTrackerMulingStartedAtMs: null,
  mulingIndicatorOverlayEnabled: true,
  trackerOverlaysSeparateWindow: false,
  alwaysShowOverlays: false,
  dropsTrackerOverlayPosition: { x: 12, y: 12 },
  dropsTrackerOverlayWidth: 238,
  dropsTrackerOverlayHeight: 260,
  totalDropsOverlayPosition: { x: 12, y: 160 },
  totalDropsOverlayWidth: 238,
  totalDropsOverlayHeight: 190,
  runeTrackerCounts: defaultRuneTrackerCounts(),
  runeTrackerOverlayEnabled: false,
  runeTrackerOverlayRunes: defaultRuneTrackerVisibility(),
  runeTrackerOverlayPosition: { x: 12, y: 612 },
  runeTrackerOverlayWidth: 238,
  runeTrackerOverlayHeight: 560,
  materialTrackerCounts: defaultMaterialTrackerCounts(),
  materialTrackerOverlayEnabled: false,
  materialTrackerOverlayMaterials: defaultMaterialTrackerVisibility(),
  materialTrackerOverlayPosition: { x: 12, y: 448 },
  materialTrackerOverlayWidth: 238,
  materialTrackerOverlayHeight: 420,
  holyGrailFound: {},
  fateCardCounts: {},
  fateCardDropCounts: {},
  fateCardTrackerOverlayEnabled: false,
  fateCardTrackerOverlayCards: defaultFateCardTrackerCardVisibility(false),
  fateCardTrackerOverlayTiers: defaultFateCardTrackerTierVisibility(true),
  fateCardTrackerOverlayPosition: { x: 260, y: 612 },
  fateCardTrackerOverlayWidth: 238,
  fateCardTrackerOverlayHeight: 180,
  holyGrailOverlayEnabled: false,
  holyGrailOverlayShowTotal: true,
  holyGrailOverlayShowLatest: true,
  holyGrailOverlayCategories: defaultHolyGrailOverlayCategories(),
  holyGrailOverlayPosition: { x: 12, y: 304 },
  holyGrailOverlayWidth: 238,
  holyGrailOverlayHeight: 290,
  achievementProgressOverlayEnabled: false,
  achievementProgressOverlayPosition: { x: 12, y: 872 },
  achievementProgressOverlayWidth: 238,
  achievementProgressOverlayHeight: 86,
  achievementPopupOverlayPosition: { x: null, y: null },
  achievementPopupOverlayWidth: 300,
  achievementPopupOverlayHeight: 88,
  monsterKillsOverlayEnabled: false,
  monsterKillsOverlayPosition: { x: 12, y: 972 },
  monsterKillsOverlayWidth: 238,
  monsterKillsOverlayHeight: 74,
  mulingIndicatorOverlayPosition: { x: 12, y: 640 },
  mulingIndicatorOverlayWidth: 58,
  mulingIndicatorOverlayHeight: 58,
  saveExitAutomationDifficulty: "Normal",
  saveExitAutomationClickX: 400,
  saveExitAutomationClickY: 340,
  saveExitAutomationCoordinateModePercent: false,
  saveExitAutomationDelayMs: 300,
  saveExitAutomationMainMenuWaitMs: 10000,
  gsfEnabled: true,
  gsfNotificationEnabled: true,
  gsfSoundSlot: null,
  gsfSoundVolume: 1.0,
  gsfPlayers: [],
  holyGrailNewItemNotificationEnabled: true,
  holyGrailNewItemSoundSlot: null,
  holyGrailNewItemSoundVolume: 1.0,
  verboseFilterLogging: false,
  sounds: defaultSounds(),
  goblinAlertSlot: null,
};

/** Settings store singleton */
class SettingsStore {
  private _settings = $state<AppSettings>({ ...DEFAULT_SETTINGS });
  private _isLoaded = $state(false);
  private _isLoading = $state(false);
  private _saveTimeout: ReturnType<typeof setTimeout> | null = null;
  /** Locally-modified-not-yet-saved keys; merged last so the overlay's drag
   *  doesn't get clobbered by the main window's stale save (and vice versa). */
  private _dirtyKeys = new Set<keyof AppSettings>();
  private _syncUnlisten: UnlistenFn | null = null;
  private _dropsTrackerTimersInGame = $state(false);

  private isPlaceholderItemName(value: unknown): boolean {
    return /^Item #\d+(?:\s+(?:SU|SSU|SSSU|TU))?$/i.test(
      String(value ?? "").trim(),
    );
  }

  private normalizeRecentItems(value: unknown): DropTrackerRecentItem[] {
    if (!Array.isArray(value)) return [];
    return value
      .filter(
        (item): item is Partial<DropTrackerRecentItem> =>
          !!item && typeof item === "object",
      )
      .map((item) => ({
        id:
          typeof item.id === "string"
            ? item.id
            : `${Date.now()}-${Math.random()}`,
        timestampMs:
          typeof item.timestampMs === "number" &&
          Number.isFinite(item.timestampMs)
            ? item.timestampMs
            : Date.now(),
        name:
          typeof item.name === "string" && canonicalTrackedItemName(item.name)
            ? canonicalTrackedItemName(item.name)
            : "Unknown item",
        isNewGrail:
          item.isNewGrail === true ||
          (item as Partial<DropTrackerRecentItem>).is_new_grail === true,
        categories: this.normalizeCategoryList(item.categories),
        dropsTrackerCategories: this.normalizeCategoryList(
          item.dropsTrackerCategories,
        ),
        totalDropsTrackerCategories: this.normalizeCategoryList(
          item.totalDropsTrackerCategories,
        ),
        source: typeof item.source === "string" ? item.source : undefined,
      }))
      .filter((item) => typeof item.source === "string" && item.source.trim().length > 0)
      .filter((item) => !this.isPlaceholderItemName(item.name))
      .slice(0, 20);
  }

  private normalizeCategoryList(value: unknown): DropTrackerCategoryKey[] {
    if (!Array.isArray(value)) return [];
    const valid = new Set<DropTrackerCategoryKey>(
      Object.keys(
        DEFAULT_SETTINGS.dropsTrackerCategories,
      ) as DropTrackerCategoryKey[],
    );
    return Array.from(
      new Set(
        value.filter((key): key is DropTrackerCategoryKey =>
          valid.has(key as DropTrackerCategoryKey),
        ),
      ),
    );
  }

  private normalizeOverlayPosition(
    value: unknown,
    fallback: OverlayPosition,
  ): OverlayPosition {
    if (!value || typeof value !== "object") return { ...fallback };
    const maybe = value as Partial<OverlayPosition>;
    const x =
      typeof maybe.x === "number" && Number.isFinite(maybe.x) ? maybe.x : null;
    const y =
      typeof maybe.y === "number" && Number.isFinite(maybe.y) ? maybe.y : null;
    return { x, y };
  }

  private normalizeOverlayWidth(value: unknown, fallback: number, max = 420, min = 80): number {
    const width = typeof value === "number" && Number.isFinite(value)
      ? Math.round(value)
      : fallback;
    return Math.max(min, Math.min(max, width));
  }

  private normalizeOverlayHeight(value: unknown, fallback: number): number {
    const height = typeof value === "number" && Number.isFinite(value)
      ? Math.round(value)
      : fallback;
    return Math.max(40, Math.min(900, height));
  }

  private normalizeTimerStart(value: unknown): number {
    return typeof value === "number" && Number.isFinite(value) && value > 0
      ? value
      : Date.now();
  }

  private normalizeElapsedMs(value: unknown, fallback = 0): number {
    return typeof value === "number" && Number.isFinite(value) && value >= 0
      ? Math.floor(value)
      : Math.max(0, Math.floor(fallback));
  }

  private normalizeUnitVolume(value: unknown, fallback = 1): number {
    const n = typeof value === "number" && Number.isFinite(value) ? value : fallback;
    return Math.max(0, Math.min(1, n));
  }

  private normalizeDifficulty(value: unknown): SaveExitAutomationDifficulty {
    if (value === "Nightmare" || value === "Hell") return value;
    return "Normal";
  }

  private normalizeTheme(value: unknown): AppTheme {
    const valid: AppTheme[] = ["sanctuary", "horadric", "hellfire", "dark", "light"];
    return valid.includes(value as AppTheme) ? value as AppTheme : DEFAULT_SETTINGS.theme;
  }

  private normalizeTabOrder(value: unknown, defaults: readonly string[]): string[] {
    const seen = new Set<string>();
    const out: string[] = [];
    if (Array.isArray(value)) {
      for (const id of value) {
        if (typeof id !== "string" || !defaults.includes(id) || seen.has(id)) continue;
        seen.add(id);
        out.push(id);
      }
    }
    for (const id of defaults) {
      if (!seen.has(id)) out.push(id);
    }
    return out;
  }

  private normalizeInteger(value: unknown, fallback: number, min: number, max: number): number {
    const n = typeof value === "number" && Number.isFinite(value) ? Math.round(value) : fallback;
    return Math.max(min, Math.min(max, n));
  }

  private createId(prefix: string): string {
    const cryptoId = globalThis.crypto?.randomUUID?.();
    return cryptoId ? `${prefix}-${cryptoId}` : `${prefix}-${Date.now()}-${Math.random()}`;
  }

  private normalizeHolyGrailFound(
    value: unknown,
    keep: "earliest" | "latest" = "earliest",
  ): HolyGrailFoundMap {
    if (!value || typeof value !== "object") return {};
    const out: HolyGrailFoundMap = {};
    const itemsByKey = new Map(buildHolyGrailItems().map((item) => [item.key, item]));
    for (const entry of Object.values(
      value as Record<string, Partial<HolyGrailFoundEntry>>,
    )) {
      if (!entry || typeof entry !== "object") continue;
      if (typeof entry.key !== "string" || typeof entry.name !== "string")
        continue;
      if (this.isPlaceholderItemName(entry.name)) continue;
      const category = entry.category as HolyGrailCategoryKey;
      const validCategories = [
        "su",
        "ssu",
        "sets",
        "runes",
        "runewords",
        "fateCards",
        "hatredOrbs",
        "essences",
        "ascendancy",
      ];
      if (!validCategories.includes(category)) continue;
      const canonicalItem =
        category === "runewords" || category === "runes"
          ? itemsByKey.get(entry.key) ?? canonicalHolyGrailItem(category, entry.name)
          : canonicalHolyGrailItem(category, entry.name);
      if (!canonicalItem) continue;
      const firstFoundAt =
        typeof entry.firstFoundAt === "string" && entry.firstFoundAt
          ? entry.firstFoundAt
          : new Date().toISOString();
      const existing = out[canonicalItem.key];
      if (
        existing &&
        (keep === "earliest"
          ? existing.firstFoundAt <= firstFoundAt
          : existing.firstFoundAt >= firstFoundAt)
      ) {
        continue;
      }
      out[canonicalItem.key] = {
        key: canonicalItem.key,
        name: canonicalItem.name,
        category: canonicalItem.category,
        firstFoundAt,
      };
    }
    return out;
  }

  private normalizeFateCardCounts(
    value: Partial<Record<string, number>> | undefined | null,
  ): FateCardCounts {
    const out: FateCardCounts = {};
    for (const cardName of SOE_13_FATE_CARD_ITEMS) {
      const card = fateCardInfo(cardName);
      if (!card) continue;
      const raw = value?.[card.name] ?? value?.[cardName];
      if (typeof raw === "number" && Number.isFinite(raw) && raw > 0) {
        out[card.name] = Math.floor(raw);
      }
    }
    return out;
  }

  private withCompletedFateCardStacks(
    found: HolyGrailFoundMap,
    counts: FateCardCounts,
  ): HolyGrailFoundMap {
    let next: HolyGrailFoundMap | null = null;
    for (const cardName of SOE_13_FATE_CARD_ITEMS) {
      const card = fateCardInfo(cardName);
      const grailItem = card ? canonicalHolyGrailItem("fateCards", card.name) : null;
      if (!card || !grailItem) continue;
      const count = counts[card.name] ?? 0;
      if (count < card.amountRequired || found[grailItem.key]) continue;
      next = next ?? { ...found };
      next[grailItem.key] = {
        key: grailItem.key,
        name: grailItem.name,
        category: grailItem.category,
        firstFoundAt: new Date().toISOString(),
      };
    }
    return next ?? found;
  }

  private pruneIncompleteFateCardStacks(
    found: HolyGrailFoundMap,
    counts: FateCardCounts,
  ): HolyGrailFoundMap {
    let next: HolyGrailFoundMap | null = null;
    for (const [key, entry] of Object.entries(found)) {
      if (entry.category !== "fateCards") continue;
      const card = fateCardInfo(entry.name);
      if (!card) continue;
      const count = counts[card.name] ?? 0;
      if (count >= card.amountRequired) continue;
      next = next ?? { ...found };
      delete next[key];
    }
    return next ?? found;
  }

  private normalizeGsfCategory(value: unknown): GsfItemCategory {
    const valid: GsfItemCategory[] = ["tu", "su", "ssu", "sssu", "set", "uniqueJewel"];
    return valid.includes(value as GsfItemCategory)
      ? (value as GsfItemCategory)
      : "su";
  }

  private normalizeGsfSlot(value: unknown): GsfItemSlot {
    const valid: GsfItemSlot[] = [
      "any",
      "chest",
      "helm",
      "weapon",
      "offhand",
      "shield",
      "gloves",
      "boots",
      "belt",
      "ring",
      "amulet",
      "jewel",
      "other",
    ];
    return valid.includes(value as GsfItemSlot) ? (value as GsfItemSlot) : "any";
  }

  private normalizeGsfStatus(value: unknown): GsfWantedStatus {
    const valid: GsfWantedStatus[] = ["needed", "found", "skipped"];
    return valid.includes(value as GsfWantedStatus)
      ? (value as GsfWantedStatus)
      : "needed";
  }

  private normalizeGsfClassName(value: unknown): string {
    const valid = [
      "",
      "Amazon",
      "Assassin",
      "Barbarian",
      "Druid",
      "Necromancer",
      "Paladin",
      "Sorceress",
    ];
    return valid.includes(value as string) ? (value as string) : "";
  }

  private normalizeGsfPlayers(value: unknown): GsfPlayer[] {
    if (!Array.isArray(value)) return [];
    return value
      .filter((player): player is Partial<GsfPlayer> => !!player && typeof player === "object")
      .map((player) => {
        const now = new Date().toISOString();
        const id = typeof player.id === "string" && player.id ? player.id : this.createId("gsf-player");
        const name = typeof player.name === "string" ? player.name.trim() : "";
        const wantedItems: unknown[] = Array.isArray(player.wantedItems)
          ? player.wantedItems
          : [];
        return {
          id,
          name,
          className: this.normalizeGsfClassName(player.className),
          buildName: typeof player.buildName === "string" ? player.buildName : "",
          notes: typeof player.notes === "string" ? player.notes : "",
          createdAt:
            typeof player.createdAt === "string" && player.createdAt ? player.createdAt : now,
          updatedAt:
            typeof player.updatedAt === "string" && player.updatedAt ? player.updatedAt : now,
          wantedItems: wantedItems
            .filter((item): item is Record<string, unknown> => !!item && typeof item === "object")
            .map((item) => {
              const itemName = typeof item.itemName === "string" ? item.itemName.trim() : "";
              const status = this.normalizeGsfStatus(item.status);
              return {
                id:
                  typeof item.id === "string" && item.id
                    ? item.id
                    : this.createId("gsf-item"),
                itemName,
                normalizedItemName:
                  typeof item.normalizedItemName === "string" && item.normalizedItemName
                    ? normalizeGsfItemName(item.normalizedItemName)
                    : normalizeGsfItemName(itemName),
                category: this.normalizeGsfCategory(item.category),
                slot: this.normalizeGsfSlot(item.slot),
                status,
                notes: typeof item.notes === "string" ? item.notes : "",
                createdAt:
                  typeof item.createdAt === "string" && item.createdAt ? item.createdAt : now,
                updatedAt:
                  typeof item.updatedAt === "string" && item.updatedAt ? item.updatedAt : now,
                foundAt:
                  status === "found"
                    ? typeof item.foundAt === "string" && item.foundAt
                      ? item.foundAt
                      : now
                    : null,
              };
            }),
        };
      });
  }

  private normalizeSettings(
    settings: AppSettings,
    options: { resetTimerBaseline?: boolean } = {},
  ): AppSettings {
    const resetTimerBaseline = options.resetTimerBaseline ?? false;
    const fateCardCounts = this.normalizeFateCardCounts(
      (settings as Partial<AppSettings>).fateCardCounts,
    );
    const fateCardDropCounts = this.normalizeFateCardCounts(
      (settings as Partial<AppSettings>).fateCardDropCounts,
    );
    const holyGrailFound = this.pruneIncompleteFateCardStacks(
      this.normalizeHolyGrailFound(settings.holyGrailFound),
      fateCardCounts,
    );
    return {
      ...settings,
      theme: this.normalizeTheme(settings.theme),
      soeLauncherPath:
        typeof settings.soeLauncherPath === "string" && settings.soeLauncherPath.trim()
          ? settings.soeLauncherPath
          : DEFAULT_SETTINGS.soeLauncherPath,
      projectD2Path:
        typeof settings.projectD2Path === "string" && settings.projectD2Path.trim()
          ? settings.projectD2Path
          : DEFAULT_SETTINGS.projectD2Path,
      runewordPlannerStashPath:
        typeof settings.runewordPlannerStashPath === "string" && settings.runewordPlannerStashPath.trim()
          ? settings.runewordPlannerStashPath
          : DEFAULT_SETTINGS.runewordPlannerStashPath,
      mainTabOrder: this.normalizeTabOrder(settings.mainTabOrder, DEFAULT_MAIN_TAB_ORDER),
      setupWizardCompleted:
        settings.setupWizardCompleted ??
        DEFAULT_SETTINGS.setupWizardCompleted,
      dropsTrackerSubTabOrder: this.normalizeTabOrder(
        settings.dropsTrackerSubTabOrder,
        DEFAULT_DROPS_TRACKER_SUB_TAB_ORDER,
      ),
      holyGrailSubTabOrder: this.normalizeTabOrder(
        settings.holyGrailSubTabOrder,
        DEFAULT_HOLY_GRAIL_SUB_TAB_ORDER,
      ),
      achievementStats: normalizeAchievementStats(settings.achievementStats),
      achievementSettings: normalizeAchievementSettings(settings.achievementSettings),
      mulingModeHotkey:
        settings.mulingModeHotkey ?? DEFAULT_SETTINGS.mulingModeHotkey,
      gameResetHotkey:
        settings.gameResetHotkey ?? DEFAULT_SETTINGS.gameResetHotkey,
      dropsTrackerCategories: normalizeCategorySettings(
        settings.dropsTrackerCategories,
        defaultDropsTrackerCategories(),
      ),
      totalDropsTrackerCategories: normalizeCategorySettings(
        settings.totalDropsTrackerCategories,
        defaultTotalDropsTrackerCategories(),
      ),
      notificationOverlayEnabled:
        settings.notificationOverlayEnabled ??
        DEFAULT_SETTINGS.notificationOverlayEnabled,
      notificationWidth: this.normalizeOverlayWidth(
        settings.notificationWidth,
        DEFAULT_SETTINGS.notificationWidth,
        560,
      ),
      notificationHeight: this.normalizeOverlayHeight(
        settings.notificationHeight,
        DEFAULT_SETTINGS.notificationHeight,
      ),
      notifyUnidentifiedUniqueSetDrops:
        settings.notifyUnidentifiedUniqueSetDrops ??
        DEFAULT_SETTINGS.notifyUnidentifiedUniqueSetDrops,
      itemSoundRules: normalizeItemSoundRules(settings.itemSoundRules),
      dropsTrackerCounts: normalizeCounts(settings.dropsTrackerCounts),
      totalDropsTrackerCounts: normalizeCounts(
        settings.totalDropsTrackerCounts,
      ),
      dropsTrackerRecentItems: this.normalizeRecentItems(
        settings.dropsTrackerRecentItems,
      ),
      dropsTrackerRunCount: Math.max(
        0,
        Math.floor(settings.dropsTrackerRunCount ?? 0),
      ),
      dropsTrackerRunTimerEnabled:
        settings.dropsTrackerRunTimerEnabled ??
        DEFAULT_SETTINGS.dropsTrackerRunTimerEnabled,
      dropsTrackerSessionTimerEnabled:
        settings.dropsTrackerSessionTimerEnabled ??
        DEFAULT_SETTINGS.dropsTrackerSessionTimerEnabled,
      dropsTrackerRunStartedAtMs: this.normalizeTimerStart(
        settings.dropsTrackerRunStartedAtMs,
      ),
      dropsTrackerSessionStartedAtMs: this.normalizeTimerStart(
        settings.dropsTrackerSessionStartedAtMs,
      ),
      dropsTrackerRunElapsedMs: this.normalizeElapsedMs(
        settings.dropsTrackerRunElapsedMs,
        typeof settings.dropsTrackerRunStartedAtMs === "number"
          ? Date.now() - settings.dropsTrackerRunStartedAtMs
          : 0,
      ),
      dropsTrackerSessionElapsedMs: this.normalizeElapsedMs(
        settings.dropsTrackerSessionElapsedMs,
        typeof settings.dropsTrackerSessionStartedAtMs === "number"
          ? Date.now() - settings.dropsTrackerSessionStartedAtMs
          : 0,
      ),
      dropsTrackerTimerLastTickAtMs: resetTimerBaseline
        ? Date.now()
        : this.normalizeTimerStart(settings.dropsTrackerTimerLastTickAtMs),
      dropsTrackerPreventDuplicates: false,
      dropsTrackerMulingMode:
        settings.dropsTrackerMulingMode ??
        DEFAULT_SETTINGS.dropsTrackerMulingMode,
      dropsTrackerMulingStartedAtMs:
        typeof settings.dropsTrackerMulingStartedAtMs === "number"
          ? settings.dropsTrackerMulingStartedAtMs
          : DEFAULT_SETTINGS.dropsTrackerMulingStartedAtMs,
      mulingIndicatorOverlayEnabled:
        settings.mulingIndicatorOverlayEnabled ??
        DEFAULT_SETTINGS.mulingIndicatorOverlayEnabled,
      trackerOverlaysSeparateWindow:
        settings.trackerOverlaysSeparateWindow ??
        DEFAULT_SETTINGS.trackerOverlaysSeparateWindow,
      alwaysShowOverlays:
        settings.alwaysShowOverlays ??
        DEFAULT_SETTINGS.alwaysShowOverlays,
      dropsTrackerOverlayPosition: this.normalizeOverlayPosition(
        settings.dropsTrackerOverlayPosition,
        DEFAULT_SETTINGS.dropsTrackerOverlayPosition,
      ),
      dropsTrackerOverlayWidth: this.normalizeOverlayWidth(
        settings.dropsTrackerOverlayWidth,
        DEFAULT_SETTINGS.dropsTrackerOverlayWidth,
      ),
      dropsTrackerOverlayHeight: this.normalizeOverlayHeight(
        settings.dropsTrackerOverlayHeight,
        DEFAULT_SETTINGS.dropsTrackerOverlayHeight,
      ),
      totalDropsOverlayPosition: this.normalizeOverlayPosition(
        settings.totalDropsOverlayPosition,
        DEFAULT_SETTINGS.totalDropsOverlayPosition,
      ),
      totalDropsOverlayWidth: this.normalizeOverlayWidth(
        settings.totalDropsOverlayWidth,
        DEFAULT_SETTINGS.totalDropsOverlayWidth,
      ),
      totalDropsOverlayHeight: this.normalizeOverlayHeight(
        settings.totalDropsOverlayHeight,
        DEFAULT_SETTINGS.totalDropsOverlayHeight,
      ),
      runeTrackerCounts: normalizeRuneTrackerCounts(settings.runeTrackerCounts),
      runeTrackerOverlayEnabled:
        settings.runeTrackerOverlayEnabled ??
        DEFAULT_SETTINGS.runeTrackerOverlayEnabled,
      runeTrackerOverlayRunes: normalizeRuneTrackerVisibility(
        settings.runeTrackerOverlayRunes,
      ),
      runeTrackerOverlayPosition: this.normalizeOverlayPosition(
        settings.runeTrackerOverlayPosition,
        DEFAULT_SETTINGS.runeTrackerOverlayPosition,
      ),
      runeTrackerOverlayWidth: this.normalizeOverlayWidth(
        settings.runeTrackerOverlayWidth,
        DEFAULT_SETTINGS.runeTrackerOverlayWidth,
        420,
        96,
      ),
      runeTrackerOverlayHeight: this.normalizeOverlayHeight(
        settings.runeTrackerOverlayHeight,
        DEFAULT_SETTINGS.runeTrackerOverlayHeight,
      ),
      materialTrackerCounts: normalizeMaterialTrackerCounts(settings.materialTrackerCounts),
      materialTrackerOverlayEnabled:
        settings.materialTrackerOverlayEnabled ??
        DEFAULT_SETTINGS.materialTrackerOverlayEnabled,
      materialTrackerOverlayMaterials: normalizeMaterialTrackerVisibility(
        settings.materialTrackerOverlayMaterials,
      ),
      materialTrackerOverlayPosition: this.normalizeOverlayPosition(
        settings.materialTrackerOverlayPosition,
        DEFAULT_SETTINGS.materialTrackerOverlayPosition,
      ),
      materialTrackerOverlayWidth: this.normalizeOverlayWidth(
        settings.materialTrackerOverlayWidth,
        DEFAULT_SETTINGS.materialTrackerOverlayWidth,
      ),
      materialTrackerOverlayHeight: this.normalizeOverlayHeight(
        settings.materialTrackerOverlayHeight,
        DEFAULT_SETTINGS.materialTrackerOverlayHeight,
      ),
      holyGrailFound,
      fateCardCounts,
      fateCardDropCounts,
      fateCardTrackerOverlayEnabled:
        settings.fateCardTrackerOverlayEnabled ??
        DEFAULT_SETTINGS.fateCardTrackerOverlayEnabled,
      fateCardTrackerOverlayCards: normalizeFateCardTrackerCardVisibility(
        settings.fateCardTrackerOverlayCards,
      ),
      fateCardTrackerOverlayTiers: normalizeFateCardTrackerTierVisibility(
        settings.fateCardTrackerOverlayTiers,
      ),
      fateCardTrackerOverlayPosition: this.normalizeOverlayPosition(
        settings.fateCardTrackerOverlayPosition,
        DEFAULT_SETTINGS.fateCardTrackerOverlayPosition,
      ),
      fateCardTrackerOverlayWidth: this.normalizeOverlayWidth(
        settings.fateCardTrackerOverlayWidth,
        DEFAULT_SETTINGS.fateCardTrackerOverlayWidth,
      ),
      fateCardTrackerOverlayHeight: this.normalizeOverlayHeight(
        settings.fateCardTrackerOverlayHeight,
        DEFAULT_SETTINGS.fateCardTrackerOverlayHeight,
      ),
      holyGrailOverlayEnabled:
        settings.holyGrailOverlayEnabled ??
        DEFAULT_SETTINGS.holyGrailOverlayEnabled,
      holyGrailOverlayShowTotal:
        settings.holyGrailOverlayShowTotal ??
        DEFAULT_SETTINGS.holyGrailOverlayShowTotal,
      holyGrailOverlayShowLatest:
        settings.holyGrailOverlayShowLatest ??
        DEFAULT_SETTINGS.holyGrailOverlayShowLatest,
      holyGrailOverlayCategories: normalizeHolyGrailOverlayCategories(
        settings.holyGrailOverlayCategories,
      ),
      holyGrailOverlayPosition: this.normalizeOverlayPosition(
        settings.holyGrailOverlayPosition,
        DEFAULT_SETTINGS.holyGrailOverlayPosition,
      ),
      holyGrailOverlayWidth: this.normalizeOverlayWidth(
        settings.holyGrailOverlayWidth,
        DEFAULT_SETTINGS.holyGrailOverlayWidth,
      ),
      holyGrailOverlayHeight: this.normalizeOverlayHeight(
        settings.holyGrailOverlayHeight,
        DEFAULT_SETTINGS.holyGrailOverlayHeight,
      ),
      achievementProgressOverlayEnabled:
        settings.achievementProgressOverlayEnabled ??
        DEFAULT_SETTINGS.achievementProgressOverlayEnabled,
      achievementProgressOverlayPosition: this.normalizeOverlayPosition(
        settings.achievementProgressOverlayPosition,
        DEFAULT_SETTINGS.achievementProgressOverlayPosition,
      ),
      achievementProgressOverlayWidth: this.normalizeOverlayWidth(
        settings.achievementProgressOverlayWidth,
        DEFAULT_SETTINGS.achievementProgressOverlayWidth,
      ),
      achievementProgressOverlayHeight: this.normalizeOverlayHeight(
        settings.achievementProgressOverlayHeight,
        DEFAULT_SETTINGS.achievementProgressOverlayHeight,
      ),
      achievementPopupOverlayPosition: this.normalizeOverlayPosition(
        settings.achievementPopupOverlayPosition,
        DEFAULT_SETTINGS.achievementPopupOverlayPosition,
      ),
      achievementPopupOverlayWidth: this.normalizeOverlayWidth(
        settings.achievementPopupOverlayWidth,
        DEFAULT_SETTINGS.achievementPopupOverlayWidth,
        420,
        220,
      ),
      achievementPopupOverlayHeight: this.normalizeOverlayHeight(
        settings.achievementPopupOverlayHeight,
        DEFAULT_SETTINGS.achievementPopupOverlayHeight,
      ),
      monsterKillsOverlayEnabled:
        settings.monsterKillsOverlayEnabled ??
        DEFAULT_SETTINGS.monsterKillsOverlayEnabled,
      monsterKillsOverlayPosition: this.normalizeOverlayPosition(
        settings.monsterKillsOverlayPosition,
        DEFAULT_SETTINGS.monsterKillsOverlayPosition,
      ),
      monsterKillsOverlayWidth: this.normalizeOverlayWidth(
        settings.monsterKillsOverlayWidth,
        DEFAULT_SETTINGS.monsterKillsOverlayWidth,
      ),
      monsterKillsOverlayHeight: this.normalizeOverlayHeight(
        settings.monsterKillsOverlayHeight,
        DEFAULT_SETTINGS.monsterKillsOverlayHeight,
      ),
      mulingIndicatorOverlayPosition: this.normalizeOverlayPosition(
        settings.mulingIndicatorOverlayPosition,
        DEFAULT_SETTINGS.mulingIndicatorOverlayPosition,
      ),
      mulingIndicatorOverlayWidth: this.normalizeOverlayWidth(
        settings.mulingIndicatorOverlayWidth,
        DEFAULT_SETTINGS.mulingIndicatorOverlayWidth,
      ),
      mulingIndicatorOverlayHeight: this.normalizeOverlayHeight(
        settings.mulingIndicatorOverlayHeight,
        DEFAULT_SETTINGS.mulingIndicatorOverlayHeight,
      ),
      saveExitAutomationDifficulty: this.normalizeDifficulty(
        settings.saveExitAutomationDifficulty,
      ),
      saveExitAutomationClickX: this.normalizeInteger(
        settings.saveExitAutomationClickX,
        DEFAULT_SETTINGS.saveExitAutomationClickX,
        0,
        10000,
      ),
      saveExitAutomationClickY: this.normalizeInteger(
        settings.saveExitAutomationClickY,
        DEFAULT_SETTINGS.saveExitAutomationClickY,
        0,
        10000,
      ),
      saveExitAutomationCoordinateModePercent:
        settings.saveExitAutomationCoordinateModePercent ??
        DEFAULT_SETTINGS.saveExitAutomationCoordinateModePercent,
      saveExitAutomationDelayMs: this.normalizeInteger(
        settings.saveExitAutomationDelayMs,
        DEFAULT_SETTINGS.saveExitAutomationDelayMs,
        50,
        2000,
      ),
      saveExitAutomationMainMenuWaitMs: this.normalizeInteger(
        settings.saveExitAutomationMainMenuWaitMs,
        DEFAULT_SETTINGS.saveExitAutomationMainMenuWaitMs,
        500,
        30000,
      ),
      gsfEnabled: settings.gsfEnabled ?? DEFAULT_SETTINGS.gsfEnabled,
      gsfNotificationEnabled:
        settings.gsfNotificationEnabled ?? DEFAULT_SETTINGS.gsfNotificationEnabled,
      gsfSoundSlot:
        typeof settings.gsfSoundSlot === "number" && settings.gsfSoundSlot > 0
          ? Math.floor(settings.gsfSoundSlot)
          : DEFAULT_SETTINGS.gsfSoundSlot,
      gsfSoundVolume: this.normalizeUnitVolume(
        settings.gsfSoundVolume,
        DEFAULT_SETTINGS.gsfSoundVolume,
      ),
      gsfPlayers: this.normalizeGsfPlayers(settings.gsfPlayers),
      holyGrailNewItemNotificationEnabled:
        settings.holyGrailNewItemNotificationEnabled ??
        DEFAULT_SETTINGS.holyGrailNewItemNotificationEnabled,
      holyGrailNewItemSoundSlot:
        typeof settings.holyGrailNewItemSoundSlot === "number" &&
        settings.holyGrailNewItemSoundSlot > 0
          ? Math.floor(settings.holyGrailNewItemSoundSlot)
          : DEFAULT_SETTINGS.holyGrailNewItemSoundSlot,
      holyGrailNewItemSoundVolume: this.normalizeUnitVolume(
        settings.holyGrailNewItemSoundVolume,
        DEFAULT_SETTINGS.holyGrailNewItemSoundVolume,
      ),
    };
  }

  private mergeHolyGrailFoundMaps(
    first: HolyGrailFoundMap,
    second: HolyGrailFoundMap,
    keep: "earliest" | "latest" = "earliest",
  ): HolyGrailFoundMap {
    const merged: HolyGrailFoundMap = {};
    const add = (entries: HolyGrailFoundMap) => {
      for (const entry of Object.values(this.normalizeHolyGrailFound(entries, keep))) {
        const existing = merged[entry.key];
        if (!existing) {
          merged[entry.key] = entry;
          continue;
        }
        const shouldReplace =
          keep === "earliest"
            ? entry.firstFoundAt < existing.firstFoundAt
            : entry.firstFoundAt > existing.firstFoundAt;
        if (shouldReplace) merged[entry.key] = entry;
      }
    };
    add(first);
    add(second);
    return merged;
  }

  /** Current settings (reactive) */
  get settings(): AppSettings {
    return this._settings;
  }

  /** Whether settings have been loaded from backend */
  get isLoaded(): boolean {
    return this._isLoaded;
  }

  /** Whether settings are currently loading */
  get isLoading(): boolean {
    return this._isLoading;
  }

  /** Load settings from backend */
  async load(): Promise<void> {
    if (this._isLoading) return;

    this._isLoading = true;

    try {
      const loaded = await invoke<AppSettings>("load_settings");
      this._settings = this.normalizeSettings({
        ...DEFAULT_SETTINGS,
        ...loaded,
      }, { resetTimerBaseline: true });
      this._isLoaded = true;

      // Apply theme immediately
      this.applyTheme(this._settings.theme);
    } catch (error) {
      console.error("[Settings] Failed to load:", error);
      // Use defaults on error
      this._settings = this.normalizeSettings(
        { ...DEFAULT_SETTINGS },
        { resetTimerBaseline: true },
      );
      this._isLoaded = true;
    } finally {
      this._isLoading = false;
    }
  }

  /** Save settings to backend (debounced). Re-reads disk and applies dirty
   *  keys on top so another window's concurrent changes survive. */
  async save(): Promise<void> {
    if (this._saveTimeout) {
      clearTimeout(this._saveTimeout);
    }

    this._saveTimeout = setTimeout(async () => {
      const dirtyKeys = Array.from(this._dirtyKeys);
      if (dirtyKeys.length === 0) return;
      const patch: Partial<AppSettings> = {};
      for (const key of dirtyKeys) {
        (patch as AppSettings)[key] = this._settings[key] as never;
      }

      try {
        const saved = await invoke<AppSettings>("save_settings_patch", {
          patch,
        });
        const localBeforeMerge = this._settings;
        for (const key of dirtyKeys) {
          if (Object.is(localBeforeMerge[key], patch[key])) {
            this._dirtyKeys.delete(key);
          }
        }
        const merged: AppSettings = this.normalizeSettings({
          ...DEFAULT_SETTINGS,
          ...saved,
        });
        for (const key of this._dirtyKeys) {
          (merged as AppSettings)[key] = this._settings[key] as never;
        }
        this._settings = merged;
      } catch (error) {
        console.error("[Settings] Failed to save:", error);
      }
    }, 500);
  }

  /** Update a single setting */
  set<K extends keyof AppSettings>(key: K, value: AppSettings[K]): void {
    this._settings = { ...this._settings, [key]: value };
    this._dirtyKeys.add(key);

    // Special handling for theme changes
    if (key === "theme") {
      this.applyTheme(value as string);
    }

    // Auto-save after change
    this.save();
  }

  /** Update multiple settings at once */
  update(partial: Partial<AppSettings>): void {
    this._settings = { ...this._settings, ...partial };
    for (const key of Object.keys(partial) as Array<keyof AppSettings>) {
      this._dirtyKeys.add(key);
    }

    // Special handling for theme changes
    if ("theme" in partial) {
      this.applyTheme(partial.theme as string);
    }

    // Auto-save after change
    this.save();
  }

  /** Listen for `settings-updated` events from other windows and merge them
   *  in, keeping any locally-dirty keys (pending debounce) intact. */
  async initSync(): Promise<void> {
    if (this._syncUnlisten) return;
    this._syncUnlisten = await listen<AppSettings>(
      "settings-updated",
      (event) => {
        const external = event.payload;
        const merged: AppSettings = this.normalizeSettings({
          ...DEFAULT_SETTINGS,
          ...external,
        });
        for (const key of this._dirtyKeys) {
          (merged as AppSettings)[key] = this._settings[key] as never;
        }
        if (this._settings.theme !== merged.theme) {
          this.applyTheme(merged.theme);
        }
        this._settings = merged;
      },
    );
  }

  /** Tear down the cross-window sync listener. */
  destroySync(): void {
    if (this._syncUnlisten) {
      this._syncUnlisten();
      this._syncUnlisten = null;
    }
  }

  /** Apply theme to the document */
  private applyTheme(theme: string): void {
    document.documentElement.setAttribute("data-theme", theme);
  }

  /** Get current theme */
  get theme(): string {
    return this._settings.theme;
  }

  /** Set theme */
  setTheme(theme: AppTheme): void {
    this.set("theme", theme);
  }

  /** Cycle through available themes */
  toggleTheme(): void {
    const themes: AppTheme[] = ["sanctuary", "hellfire", "horadric", "dark", "light"];
    const idx = themes.indexOf(this._settings.theme);
    this.setTheme(themes[(idx + 1) % themes.length]);
  }

  setSoeLauncherPath(path: string | null): void {
    this.set("soeLauncherPath", path && path.trim() ? path.trim() : null);
  }

  setProjectD2Path(path: string | null): void {
    this.set("projectD2Path", path && path.trim() ? path.trim() : null);
  }

  setRunewordPlannerStashPath(path: string | null): void {
    this.set("runewordPlannerStashPath", path && path.trim() ? path.trim() : null);
  }

  setMainTabOrder(order: string[]): void {
    this.set("mainTabOrder", this.normalizeTabOrder(order, DEFAULT_MAIN_TAB_ORDER));
  }

  setDropsTrackerSubTabOrder(order: string[]): void {
    this.set(
      "dropsTrackerSubTabOrder",
      this.normalizeTabOrder(order, DEFAULT_DROPS_TRACKER_SUB_TAB_ORDER),
    );
  }

  setHolyGrailSubTabOrder(order: string[]): void {
    this.set(
      "holyGrailSubTabOrder",
      this.normalizeTabOrder(order, DEFAULT_HOLY_GRAIL_SUB_TAB_ORDER),
    );
  }

  resetTabLayout(): void {
    this.update({
      mainTabOrder: [...DEFAULT_MAIN_TAB_ORDER],
      dropsTrackerSubTabOrder: [...DEFAULT_DROPS_TRACKER_SUB_TAB_ORDER],
      holyGrailSubTabOrder: [...DEFAULT_HOLY_GRAIL_SUB_TAB_ORDER],
    });
  }

  /** Get master sound volume (0.0 - 1.0) */
  get soundVolume(): number {
    return this._settings.soundVolume;
  }

  /** Set master sound volume (clamped to 0.0 - 1.0) */
  setSoundVolume(volume: number): void {
    const clamped = Math.max(0, Math.min(1, volume));
    this.set("soundVolume", clamped);
  }

  /** Replace the entire sounds array. */
  setSounds(sounds: SoundSlot[]): void {
    this.set("sounds", sounds);
  }

  /** Update a single slot by 1-based index. No-op for out-of-range indices. */
  updateSoundSlot(index1: number, patch: Partial<SoundSlot>): void {
    const idx = index1 - 1;
    const current = this._settings.sounds;
    if (idx < 0 || idx >= current.length) return;
    const next = current.slice();
    next[idx] = { ...next[idx], ...patch };
    this.setSounds(next);
  }

  /** Append a new empty slot. Returns the new 1-based slot index. */
  appendSoundSlot(): number {
    const next = this._settings.sounds.slice();
    const newSlotIndex = next.length + 1;
    next.push({
      label: `Sound ${newSlotIndex}`,
      volume: 0.8,
      source: { kind: "empty" },
    });
    this.setSounds(next);
    return newSlotIndex;
  }

  /** Get toggle window hotkey */
  get toggleWindowHotkey(): HotkeyConfig {
    return this._settings.toggleWindowHotkey;
  }

  /** Set toggle window hotkey */
  async setToggleWindowHotkey(hotkey: HotkeyConfig): Promise<void> {
    this.set("toggleWindowHotkey", hotkey);
    // Also update the backend hotkey listener
    try {
      await invoke("update_hotkey", { hotkey });
    } catch (error) {
      console.error("[Settings] Failed to update hotkey:", error);
    }
  }

  /** Get overlay-edit-mode hotkey */
  get editOverlayHotkey(): HotkeyConfig {
    return this._settings.editOverlayHotkey;
  }

  /** Set overlay-edit-mode hotkey */
  async setEditOverlayHotkey(hotkey: HotkeyConfig): Promise<void> {
    this.set("editOverlayHotkey", hotkey);
    try {
      await invoke("update_edit_mode_hotkey", { hotkey });
    } catch (error) {
      console.error("[Settings] Failed to update edit-mode hotkey:", error);
    }
  }

  get revealHiddenHotkey(): HotkeyConfig {
    return this._settings.revealHiddenHotkey;
  }

  async setRevealHiddenHotkey(hotkey: HotkeyConfig): Promise<void> {
    this.set("revealHiddenHotkey", hotkey);
    try {
      await invoke("update_reveal_hidden_hotkey", { hotkey });
    } catch (error) {
      console.error("[Settings] Failed to update reveal-hidden hotkey:", error);
    }
  }

  get lootHistoryHotkey(): HotkeyConfig {
    return this._settings.lootHistoryHotkey;
  }

  async setLootHistoryHotkey(hotkey: HotkeyConfig): Promise<void> {
    this.set("lootHistoryHotkey", hotkey);
    try {
      await invoke("update_loot_history_hotkey", { hotkey });
    } catch (error) {
      console.error("[Settings] Failed to update loot-history hotkey:", error);
    }
  }

  get resetDropsTrackerHotkey(): HotkeyConfig {
    return this._settings.resetDropsTrackerHotkey;
  }

  async setResetDropsTrackerHotkey(hotkey: HotkeyConfig): Promise<void> {
    this.set("resetDropsTrackerHotkey", hotkey);
    try {
      await invoke("update_reset_drops_tracker_hotkey", { hotkey });
    } catch (error) {
      console.error(
        "[Settings] Failed to update reset-drops-tracker hotkey:",
        error,
      );
    }
  }

  get mulingModeHotkey(): HotkeyConfig {
    return this._settings.mulingModeHotkey;
  }

  async setMulingModeHotkey(hotkey: HotkeyConfig): Promise<void> {
    this.set("mulingModeHotkey", hotkey);
    try {
      await invoke("update_muling_mode_hotkey", { hotkey });
    } catch (error) {
      console.error("[Settings] Failed to update muling-mode hotkey:", error);
    }
  }

  setGameResetHotkey(hotkey: HotkeyConfig): void {
    this.set("gameResetHotkey", hotkey);
  }

  get dropsTrackerEnabled(): boolean {
    return this._settings.dropsTrackerEnabled;
  }

  setDropsTrackerEnabled(enabled: boolean): void {
    this.set("dropsTrackerEnabled", enabled);
  }

  get dropsTrackerRunCounterEnabled(): boolean {
    return this._settings.dropsTrackerRunCounterEnabled;
  }

  setDropsTrackerRunCounterEnabled(enabled: boolean): void {
    this.set("dropsTrackerRunCounterEnabled", enabled);
  }

  setDropsTrackerRunTimerEnabled(enabled: boolean): void {
    this.set("dropsTrackerRunTimerEnabled", enabled);
  }

  setDropsTrackerSessionTimerEnabled(enabled: boolean): void {
    this.set("dropsTrackerSessionTimerEnabled", enabled);
  }

  get dropsTrackerPreventDuplicates(): boolean {
    return this._settings.dropsTrackerPreventDuplicates;
  }

  setDropsTrackerPreventDuplicates(enabled: boolean): void {
    this.set("dropsTrackerPreventDuplicates", enabled);
  }

  get dropsTrackerMulingMode(): boolean {
    return this._settings.dropsTrackerMulingMode;
  }

  setDropsTrackerMulingMode(enabled: boolean): void {
    const now = Date.now();
    const wasEnabled = this._settings.dropsTrackerMulingMode;

    if (enabled === wasEnabled) {
      return;
    }

    if (enabled) {
      // Capture elapsed time up to the moment muling starts, then freeze.
      this.tickDropsTrackerTimers(now);
      this.update({
        dropsTrackerMulingMode: true,
        dropsTrackerMulingStartedAtMs: now,
        dropsTrackerTimerLastTickAtMs: now,
      });
      return;
    }

    // Resume from now so time spent in Muling Mode is not added.
    this.update({
      dropsTrackerMulingMode: false,
      dropsTrackerMulingStartedAtMs: null,
      dropsTrackerTimerLastTickAtMs: now,
    });
  }

  setTrackerOverlaysSeparateWindow(enabled: boolean): void {
    this.set("trackerOverlaysSeparateWindow", enabled);
  }

  setDropsTrackerOverlayPosition(position: OverlayPosition): void {
    this.set(
      "dropsTrackerOverlayPosition",
      this.normalizeOverlayPosition(
        position,
        DEFAULT_SETTINGS.dropsTrackerOverlayPosition,
      ),
    );
  }

  setDropsTrackerOverlayWidth(width: number): void {
    this.set(
      "dropsTrackerOverlayWidth",
      this.normalizeOverlayWidth(width, DEFAULT_SETTINGS.dropsTrackerOverlayWidth),
    );
  }

  setDropsTrackerOverlayHeight(height: number): void {
    this.set(
      "dropsTrackerOverlayHeight",
      this.normalizeOverlayHeight(height, DEFAULT_SETTINGS.dropsTrackerOverlayHeight),
    );
  }

  setTotalDropsOverlayPosition(position: OverlayPosition): void {
    this.set(
      "totalDropsOverlayPosition",
      this.normalizeOverlayPosition(
        position,
        DEFAULT_SETTINGS.totalDropsOverlayPosition,
      ),
    );
  }

  setTotalDropsOverlayWidth(width: number): void {
    this.set(
      "totalDropsOverlayWidth",
      this.normalizeOverlayWidth(width, DEFAULT_SETTINGS.totalDropsOverlayWidth),
    );
  }

  setTotalDropsOverlayHeight(height: number): void {
    this.set(
      "totalDropsOverlayHeight",
      this.normalizeOverlayHeight(height, DEFAULT_SETTINGS.totalDropsOverlayHeight),
    );
  }

  setHolyGrailOverlayPosition(position: OverlayPosition): void {
    this.set(
      "holyGrailOverlayPosition",
      this.normalizeOverlayPosition(
        position,
        DEFAULT_SETTINGS.holyGrailOverlayPosition,
      ),
    );
  }

  setHolyGrailOverlayWidth(width: number): void {
    this.set(
      "holyGrailOverlayWidth",
      this.normalizeOverlayWidth(width, DEFAULT_SETTINGS.holyGrailOverlayWidth),
    );
  }

  setHolyGrailOverlayHeight(height: number): void {
    this.set(
      "holyGrailOverlayHeight",
      this.normalizeOverlayHeight(height, DEFAULT_SETTINGS.holyGrailOverlayHeight),
    );
  }

  setRuneTrackerOverlayPosition(position: OverlayPosition): void {
    this.set(
      "runeTrackerOverlayPosition",
      this.normalizeOverlayPosition(
        position,
        DEFAULT_SETTINGS.runeTrackerOverlayPosition,
      ),
    );
  }

  setRuneTrackerOverlayWidth(width: number): void {
    this.set(
      "runeTrackerOverlayWidth",
      this.normalizeOverlayWidth(width, DEFAULT_SETTINGS.runeTrackerOverlayWidth, 420, 96),
    );
  }

  setRuneTrackerOverlayHeight(height: number): void {
    this.set(
      "runeTrackerOverlayHeight",
      this.normalizeOverlayHeight(height, DEFAULT_SETTINGS.runeTrackerOverlayHeight),
    );
  }

  setMaterialTrackerOverlayPosition(position: OverlayPosition): void {
    this.set(
      "materialTrackerOverlayPosition",
      this.normalizeOverlayPosition(
        position,
        DEFAULT_SETTINGS.materialTrackerOverlayPosition,
      ),
    );
  }

  setMaterialTrackerOverlayWidth(width: number): void {
    this.set(
      "materialTrackerOverlayWidth",
      this.normalizeOverlayWidth(width, DEFAULT_SETTINGS.materialTrackerOverlayWidth),
    );
  }

  setMaterialTrackerOverlayHeight(height: number): void {
    this.set(
      "materialTrackerOverlayHeight",
      this.normalizeOverlayHeight(height, DEFAULT_SETTINGS.materialTrackerOverlayHeight),
    );
  }

  setFateCardTrackerOverlayPosition(position: OverlayPosition): void {
    this.set(
      "fateCardTrackerOverlayPosition",
      this.normalizeOverlayPosition(
        position,
        DEFAULT_SETTINGS.fateCardTrackerOverlayPosition,
      ),
    );
  }

  setFateCardTrackerOverlayWidth(width: number): void {
    this.set(
      "fateCardTrackerOverlayWidth",
      this.normalizeOverlayWidth(width, DEFAULT_SETTINGS.fateCardTrackerOverlayWidth),
    );
  }

  setFateCardTrackerOverlayHeight(height: number): void {
    this.set(
      "fateCardTrackerOverlayHeight",
      this.normalizeOverlayHeight(height, DEFAULT_SETTINGS.fateCardTrackerOverlayHeight),
    );
  }

  setAchievementProgressOverlayPosition(position: OverlayPosition): void {
    this.set(
      "achievementProgressOverlayPosition",
      this.normalizeOverlayPosition(
        position,
        DEFAULT_SETTINGS.achievementProgressOverlayPosition,
      ),
    );
  }

  setAchievementProgressOverlayWidth(width: number): void {
    this.set(
      "achievementProgressOverlayWidth",
      this.normalizeOverlayWidth(width, DEFAULT_SETTINGS.achievementProgressOverlayWidth),
    );
  }

  setAchievementProgressOverlayHeight(height: number): void {
    this.set(
      "achievementProgressOverlayHeight",
      this.normalizeOverlayHeight(height, DEFAULT_SETTINGS.achievementProgressOverlayHeight),
    );
  }

  setAchievementPopupOverlayPosition(position: OverlayPosition): void {
    this.set(
      "achievementPopupOverlayPosition",
      this.normalizeOverlayPosition(
        position,
        DEFAULT_SETTINGS.achievementPopupOverlayPosition,
      ),
    );
  }

  setAchievementPopupOverlayWidth(width: number): void {
    this.set(
      "achievementPopupOverlayWidth",
      this.normalizeOverlayWidth(width, DEFAULT_SETTINGS.achievementPopupOverlayWidth, 420, 220),
    );
  }

  setAchievementPopupOverlayHeight(height: number): void {
    this.set(
      "achievementPopupOverlayHeight",
      this.normalizeOverlayHeight(height, DEFAULT_SETTINGS.achievementPopupOverlayHeight),
    );
  }

  setMonsterKillsOverlayPosition(position: OverlayPosition): void {
    this.set(
      "monsterKillsOverlayPosition",
      this.normalizeOverlayPosition(
        position,
        DEFAULT_SETTINGS.monsterKillsOverlayPosition,
      ),
    );
  }

  setMonsterKillsOverlayWidth(width: number): void {
    this.set(
      "monsterKillsOverlayWidth",
      this.normalizeOverlayWidth(width, DEFAULT_SETTINGS.monsterKillsOverlayWidth),
    );
  }

  setMonsterKillsOverlayHeight(height: number): void {
    this.set(
      "monsterKillsOverlayHeight",
      this.normalizeOverlayHeight(height, DEFAULT_SETTINGS.monsterKillsOverlayHeight),
    );
  }

  setMulingIndicatorOverlayPosition(position: OverlayPosition): void {
    this.set(
      "mulingIndicatorOverlayPosition",
      this.normalizeOverlayPosition(
        position,
        DEFAULT_SETTINGS.mulingIndicatorOverlayPosition,
      ),
    );
  }

  setMulingIndicatorOverlayEnabled(enabled: boolean): void {
    this.set("mulingIndicatorOverlayEnabled", enabled);
  }

  setMulingIndicatorOverlayWidth(width: number): void {
    this.set(
      "mulingIndicatorOverlayWidth",
      this.normalizeOverlayWidth(width, DEFAULT_SETTINGS.mulingIndicatorOverlayWidth),
    );
  }

  setMulingIndicatorOverlayHeight(height: number): void {
    this.set(
      "mulingIndicatorOverlayHeight",
      this.normalizeOverlayHeight(height, DEFAULT_SETTINGS.mulingIndicatorOverlayHeight),
    );
  }

  setHolyGrailOverlayEnabled(enabled: boolean): void {
    this.set("holyGrailOverlayEnabled", enabled);
  }

  setRuneTrackerOverlayEnabled(enabled: boolean): void {
    this.set("runeTrackerOverlayEnabled", enabled);
  }

  setMaterialTrackerOverlayEnabled(enabled: boolean): void {
    this.set("materialTrackerOverlayEnabled", enabled);
  }

  setFateCardTrackerOverlayEnabled(enabled: boolean): void {
    this.set("fateCardTrackerOverlayEnabled", enabled);
  }

  setAchievementProgressOverlayEnabled(enabled: boolean): void {
    this.set("achievementProgressOverlayEnabled", enabled);
  }

  setMonsterKillsOverlayEnabled(enabled: boolean): void {
    this.set("monsterKillsOverlayEnabled", enabled);
  }

  setRuneTrackerOverlayRune(rune: RuneName, enabled: boolean): void {
    this.set("runeTrackerOverlayRunes", {
      ...this._settings.runeTrackerOverlayRunes,
      [rune]: enabled,
    });
  }

  setAllRuneTrackerOverlayRunes(enabled: boolean): void {
    this.set(
      "runeTrackerOverlayRunes",
      Object.fromEntries(RUNE_NAMES.map((rune) => [rune, enabled])) as RuneTrackerVisibility,
    );
  }

  setMaterialTrackerOverlayMaterial(material: MaterialTrackerName, enabled: boolean): void {
    this.set("materialTrackerOverlayMaterials", {
      ...this._settings.materialTrackerOverlayMaterials,
      [material]: enabled,
    });
  }

  setAllMaterialTrackerOverlayMaterials(enabled: boolean): void {
    this.set(
      "materialTrackerOverlayMaterials",
      Object.fromEntries(MATERIAL_TRACKER_NAMES.map((material) => [material, enabled])) as MaterialTrackerVisibility,
    );
  }

  setFateCardTrackerOverlayCard(name: string, enabled: boolean): void {
    const card = fateCardInfo(name);
    if (!card) return;
    this.set("fateCardTrackerOverlayCards", {
      ...this._settings.fateCardTrackerOverlayCards,
      [card.name]: enabled,
    });
  }

  setAllFateCardTrackerOverlayCards(enabled: boolean): void {
    this.set(
      "fateCardTrackerOverlayCards",
      defaultFateCardTrackerCardVisibility(enabled),
    );
  }

  setFateCardTrackerOverlayTier(tier: number, enabled: boolean): void {
    this.set("fateCardTrackerOverlayTiers", {
      ...this._settings.fateCardTrackerOverlayTiers,
      [fateCardTierKey(tier)]: enabled,
    });
  }

  setAllFateCardTrackerOverlayTiers(enabled: boolean): void {
    this.set(
      "fateCardTrackerOverlayTiers",
      defaultFateCardTrackerTierVisibility(enabled),
    );
  }

  setGsfEnabled(enabled: boolean): void {
    this.set("gsfEnabled", enabled);
  }

  setGsfNotificationEnabled(enabled: boolean): void {
    this.set("gsfNotificationEnabled", enabled);
  }

  setGsfSoundSlot(slot: number | null): void {
    this.set(
      "gsfSoundSlot",
      typeof slot === "number" && slot > 0 ? Math.floor(slot) : null,
    );
  }

  setGsfSoundVolume(volume: number): void {
    this.set("gsfSoundVolume", this.normalizeUnitVolume(volume, 1));
  }

  addGsfPlayer(name = "New Player"): string {
    const now = new Date().toISOString();
    const player: GsfPlayer = {
      id: this.createId("gsf-player"),
      name,
      className: "",
      buildName: "",
      notes: "",
      wantedItems: [],
      createdAt: now,
      updatedAt: now,
    };
    this.set("gsfPlayers", [...this._settings.gsfPlayers, player]);
    return player.id;
  }

  updateGsfPlayer(playerId: string, patch: Partial<Pick<GsfPlayer, "name" | "className" | "buildName" | "notes">>): void {
    const now = new Date().toISOString();
    this.set(
      "gsfPlayers",
      this._settings.gsfPlayers.map((player) =>
        player.id === playerId
          ? {
              ...player,
              name: patch.name ?? player.name,
              className:
                patch.className !== undefined
                  ? this.normalizeGsfClassName(patch.className)
                  : player.className,
              buildName: patch.buildName ?? player.buildName,
              notes: patch.notes ?? player.notes,
              updatedAt: now,
            }
          : player,
      ),
    );
  }

  removeGsfPlayer(playerId: string): void {
    this.set(
      "gsfPlayers",
      this._settings.gsfPlayers.filter((player) => player.id !== playerId),
    );
  }

  addGsfWantedItem(
    playerId: string,
    patch: Partial<Pick<GsfWantedItem, "itemName" | "category" | "slot" | "status" | "notes">> = {},
  ): string | null {
    const now = new Date().toISOString();
    const itemName = patch.itemName?.trim() ?? "";
    const item: GsfWantedItem = {
      id: this.createId("gsf-item"),
      itemName,
      normalizedItemName: normalizeGsfItemName(itemName),
      category: this.normalizeGsfCategory(patch.category),
      slot: this.normalizeGsfSlot(patch.slot),
      status: this.normalizeGsfStatus(patch.status),
      notes: patch.notes ?? "",
      createdAt: now,
      updatedAt: now,
      foundAt: patch.status === "found" ? now : null,
    };
    let createdId: string | null = null;
    this.set(
      "gsfPlayers",
      this._settings.gsfPlayers.map((player) => {
        if (player.id !== playerId) return player;
        createdId = item.id;
        return {
          ...player,
          wantedItems: [...player.wantedItems, item],
          updatedAt: now,
        };
      }),
    );
    return createdId;
  }

  updateGsfWantedItem(
    playerId: string,
    itemId: string,
    patch: Partial<Pick<GsfWantedItem, "itemName" | "category" | "slot" | "status" | "notes">>,
  ): void {
    const now = new Date().toISOString();
    this.set(
      "gsfPlayers",
      this._settings.gsfPlayers.map((player) => {
        if (player.id !== playerId) return player;
        return {
          ...player,
          wantedItems: player.wantedItems.map((item) => {
            if (item.id !== itemId) return item;
            const itemName = patch.itemName !== undefined ? patch.itemName.trim() : item.itemName;
            const status = patch.status !== undefined ? this.normalizeGsfStatus(patch.status) : item.status;
            return {
              ...item,
              itemName,
              normalizedItemName: normalizeGsfItemName(itemName),
              category: patch.category !== undefined ? this.normalizeGsfCategory(patch.category) : item.category,
              slot: patch.slot !== undefined ? this.normalizeGsfSlot(patch.slot) : item.slot,
              status,
              notes: patch.notes ?? item.notes,
              updatedAt: now,
              foundAt:
                status === "found"
                  ? item.foundAt ?? now
                  : null,
            };
          }),
          updatedAt: now,
        };
      }),
    );
  }

  removeGsfWantedItem(playerId: string, itemId: string): void {
    const now = new Date().toISOString();
    this.set(
      "gsfPlayers",
      this._settings.gsfPlayers.map((player) =>
        player.id === playerId
          ? {
              ...player,
              wantedItems: player.wantedItems.filter((item) => item.id !== itemId),
              updatedAt: now,
            }
          : player,
      ),
    );
  }

  setHolyGrailOverlayShowTotal(enabled: boolean): void {
    this.set("holyGrailOverlayShowTotal", enabled);
  }

  setHolyGrailOverlayShowLatest(enabled: boolean): void {
    this.set("holyGrailOverlayShowLatest", enabled);
  }

  setHolyGrailOverlayCategory(
    key: HolyGrailCategoryKey,
    enabled: boolean,
  ): void {
    this.set("holyGrailOverlayCategories", {
      ...this._settings.holyGrailOverlayCategories,
      [key]: enabled,
    });
  }

  setHolyGrailNewItemNotificationEnabled(enabled: boolean): void {
    this.set("holyGrailNewItemNotificationEnabled", enabled);
  }

  setHolyGrailNewItemSoundSlot(slot: number | null): void {
    this.set(
      "holyGrailNewItemSoundSlot",
      typeof slot === "number" && slot > 0 ? Math.floor(slot) : null,
    );
  }

  setHolyGrailNewItemSoundVolume(volume: number): void {
    this.set("holyGrailNewItemSoundVolume", this.normalizeUnitVolume(volume, 1));
  }

  setItemSoundRule(key: string, rule: ItemSoundRule): void {
    const normalized = normalizeItemSoundRules({ [key]: rule });
    const nextRule = normalized[key] ?? Object.values(normalized)[0];
    if (!nextRule) return;
    this.set("itemSoundRules", {
      ...this._settings.itemSoundRules,
      [key]: nextRule,
    });
  }

  removeItemSoundRule(key: string): void {
    const next = { ...this._settings.itemSoundRules };
    delete next[key];
    this.set("itemSoundRules", next);
  }

  private async backupHolyGrailFoundNow(
    found: HolyGrailFoundMap = this._settings.holyGrailFound,
  ): Promise<HolyGrailBackupStatus | null> {
    if (Object.keys(found).length === 0) return null;
    try {
      return await invoke<HolyGrailBackupStatus>("backup_holy_grail_found", {
        found,
      });
    } catch (error) {
      console.error("[Settings] Failed to backup Holy Grail data:", error);
      return null;
    }
  }

  async backupHolyGrail(): Promise<HolyGrailBackupStatus | null> {
    try {
      return await invoke<HolyGrailBackupStatus>("backup_holy_grail_found", {
        found: this._settings.holyGrailFound,
      });
    } catch (error) {
      console.error("[Settings] Failed to backup Holy Grail data:", error);
      throw error;
    }
  }

  async restoreHolyGrailBackup(): Promise<number> {
    const restored = await invoke<HolyGrailFoundMap>(
      "restore_holy_grail_backup",
    );
    const merged = this.normalizeHolyGrailFound({
      ...restored,
      ...this._settings.holyGrailFound,
    });
    this.set("holyGrailFound", merged);
    void this.backupHolyGrailFoundNow(merged);
    return Object.keys(merged).length;
  }

  async getHolyGrailBackupStatus(): Promise<HolyGrailBackupStatus> {
    return await invoke<HolyGrailBackupStatus>(
      "get_holy_grail_backup_status",
    );
  }

  async openHolyGrailBackupFolder(): Promise<void> {
    await invoke("open_holy_grail_backup_folder");
  }

  async backupFateCards(): Promise<FateCardBackupStatus> {
    return await invoke<FateCardBackupStatus>("backup_fate_card_counts", {
      counts: this._settings.fateCardCounts,
    });
  }

  async restoreFateCardsBackup(): Promise<number> {
    const restored = await invoke<FateCardCounts>(
      "restore_fate_card_counts_backup",
    );
    const normalized = this.normalizeFateCardCounts(restored);
    const merged: FateCardCounts = { ...this._settings.fateCardCounts };
    for (const [name, count] of Object.entries(normalized)) {
      merged[name] = Math.max(merged[name] ?? 0, count ?? 0);
    }
    const saved = this.normalizeFateCardCounts(merged);
    this.setFateCardCounts(saved);
    return Object.values(saved).reduce<number>((sum, count) => sum + (count ?? 0), 0);
  }

  async getFateCardBackupStatus(): Promise<FateCardBackupStatus> {
    return await invoke<FateCardBackupStatus>(
      "get_fate_card_backup_status",
    );
  }

  async openFateCardBackupFolder(): Promise<void> {
    await invoke("open_fate_card_backup_folder");
  }

  recordHolyGrailDrop(item: HolyGrailItemLike): boolean {
    const grailItem = holyGrailItemFromDrop(item);
    if (!grailItem) return false;
    if (grailItem.category === "fateCards") {
      return false;
    }
    if (this._settings.holyGrailFound[grailItem.key]) return false;
    const next = {
      ...this._settings.holyGrailFound,
      [grailItem.key]: {
        key: grailItem.key,
        name: grailItem.name,
        category: grailItem.category,
        firstFoundAt: new Date().toISOString(),
      },
    };
    this.set("holyGrailFound", next);
    void this.backupHolyGrailFoundNow(next);
    return true;
  }

  mergeHolyGrailFound(found: HolyGrailFoundMap): void {
    const merged = this.mergeHolyGrailFoundMaps(
      this._settings.holyGrailFound,
      found,
      "latest",
    );
    this.set("holyGrailFound", merged);
  }

  setHolyGrailFound(
    key: string,
    name: string,
    category: HolyGrailCategoryKey,
    found: boolean,
  ): void {
    if (category === "fateCards") {
      const card = fateCardInfo(name);
      if (card) {
        this.setFateCardCount(card.name, found ? card.amountRequired : 0, { updateGrail: found });
        if (!found) {
          const nextFound = { ...this._settings.holyGrailFound };
          delete nextFound[key];
          this.set("holyGrailFound", nextFound);
          void this.backupHolyGrailFoundNow(nextFound);
        }
        return;
      }
    }
    const next = { ...this._settings.holyGrailFound };
    if (found) {
      next[key] = next[key] ?? {
        key,
        name,
        category,
        firstFoundAt: new Date().toISOString(),
      };
    } else {
      delete next[key];
    }
    this.set("holyGrailFound", next);
    void this.backupHolyGrailFoundNow(next);
  }

  resetHolyGrail(): void {
    this.update({
      holyGrailFound: {},
      fateCardCounts: {},
    });
  }

  private completedFateCardFoundEntry(name: string): HolyGrailFoundEntry | null {
    const card = fateCardInfo(name);
    const grailItem = card ? canonicalHolyGrailItem("fateCards", card.name) : null;
    if (!card || !grailItem) return null;
    return {
      key: grailItem.key,
      name: grailItem.name,
      category: grailItem.category,
      firstFoundAt: new Date().toISOString(),
    };
  }

  setFateCardCounts(counts: FateCardCounts): void {
    const normalizedCounts = this.normalizeFateCardCounts(counts);
    const nextFound = this.withCompletedFateCardStacks(
      this.pruneIncompleteFateCardStacks(this._settings.holyGrailFound, normalizedCounts),
      normalizedCounts,
    );
    const foundChanged = nextFound !== this._settings.holyGrailFound;
    this.update({
      fateCardCounts: normalizedCounts,
      holyGrailFound: nextFound,
    });
    if (foundChanged) {
      void this.backupHolyGrailFoundNow(nextFound);
    }
  }

  setFateCardCount(
    name: string,
    count: number,
    options: { updateGrail?: boolean } = {},
  ): void {
    const card = fateCardInfo(name);
    if (!card) return;
    const nextCounts = this.normalizeFateCardCounts({
      ...this._settings.fateCardCounts,
      [card.name]: Math.max(0, Math.floor(count || 0)),
    });
    let nextFound = this._settings.holyGrailFound;
    if (options.updateGrail !== false && (nextCounts[card.name] ?? 0) >= card.amountRequired) {
      const entry = this.completedFateCardFoundEntry(card.name);
      if (entry && !nextFound[entry.key]) {
        nextFound = {
          ...nextFound,
          [entry.key]: entry,
        };
      }
    }
    const foundChanged = nextFound !== this._settings.holyGrailFound;
    this.update({
      fateCardCounts: nextCounts,
      holyGrailFound: nextFound,
    });
    if (foundChanged) {
      void this.backupHolyGrailFoundNow(nextFound);
    }
  }

  recordFateCardDrop(name: string): void {
    const card = fateCardInfo(name);
    if (!card) return;
    const nextAchievementStats = normalizeAchievementStats({
      ...this._settings.achievementStats,
      fateCardsFound: (this._settings.achievementStats.fateCardsFound ?? 0) + 1,
      tier0FateCardsFound:
        (this._settings.achievementStats.tier0FateCardsFound ?? 0) + (card.tier === 0 ? 1 : 0),
    });
    const nextDropCounts = this.normalizeFateCardCounts({
      ...this._settings.fateCardDropCounts,
      [card.name]: (this._settings.fateCardDropCounts[card.name] ?? 0) + 1,
    });
    this.update({
      achievementStats: nextAchievementStats,
      fateCardDropCounts: nextDropCounts,
    });
  }

  resetDropTrackerOverlayPositions(): void {
    this.update({
      dropsTrackerOverlayPosition: {
        ...DEFAULT_SETTINGS.dropsTrackerOverlayPosition,
      },
      totalDropsOverlayPosition: {
        ...DEFAULT_SETTINGS.totalDropsOverlayPosition,
      },
      holyGrailOverlayPosition: {
        ...DEFAULT_SETTINGS.holyGrailOverlayPosition,
      },
      runeTrackerOverlayPosition: {
        ...DEFAULT_SETTINGS.runeTrackerOverlayPosition,
      },
      materialTrackerOverlayPosition: {
        ...DEFAULT_SETTINGS.materialTrackerOverlayPosition,
      },
      fateCardTrackerOverlayPosition: {
        ...DEFAULT_SETTINGS.fateCardTrackerOverlayPosition,
      },
      achievementProgressOverlayPosition: {
        ...DEFAULT_SETTINGS.achievementProgressOverlayPosition,
      },
      achievementPopupOverlayPosition: {
        ...DEFAULT_SETTINGS.achievementPopupOverlayPosition,
      },
      monsterKillsOverlayPosition: {
        ...DEFAULT_SETTINGS.monsterKillsOverlayPosition,
      },
      mulingIndicatorOverlayPosition: {
        ...DEFAULT_SETTINGS.mulingIndicatorOverlayPosition,
      },
      dropsTrackerOverlayWidth: DEFAULT_SETTINGS.dropsTrackerOverlayWidth,
      dropsTrackerOverlayHeight: DEFAULT_SETTINGS.dropsTrackerOverlayHeight,
      totalDropsOverlayWidth: DEFAULT_SETTINGS.totalDropsOverlayWidth,
      totalDropsOverlayHeight: DEFAULT_SETTINGS.totalDropsOverlayHeight,
      holyGrailOverlayWidth: DEFAULT_SETTINGS.holyGrailOverlayWidth,
      holyGrailOverlayHeight: DEFAULT_SETTINGS.holyGrailOverlayHeight,
      runeTrackerOverlayWidth: DEFAULT_SETTINGS.runeTrackerOverlayWidth,
      runeTrackerOverlayHeight: DEFAULT_SETTINGS.runeTrackerOverlayHeight,
      materialTrackerOverlayWidth: DEFAULT_SETTINGS.materialTrackerOverlayWidth,
      materialTrackerOverlayHeight: DEFAULT_SETTINGS.materialTrackerOverlayHeight,
      fateCardTrackerOverlayWidth: DEFAULT_SETTINGS.fateCardTrackerOverlayWidth,
      fateCardTrackerOverlayHeight: DEFAULT_SETTINGS.fateCardTrackerOverlayHeight,
      achievementProgressOverlayWidth: DEFAULT_SETTINGS.achievementProgressOverlayWidth,
      achievementProgressOverlayHeight: DEFAULT_SETTINGS.achievementProgressOverlayHeight,
      achievementPopupOverlayWidth: DEFAULT_SETTINGS.achievementPopupOverlayWidth,
      achievementPopupOverlayHeight: DEFAULT_SETTINGS.achievementPopupOverlayHeight,
      monsterKillsOverlayWidth: DEFAULT_SETTINGS.monsterKillsOverlayWidth,
      monsterKillsOverlayHeight: DEFAULT_SETTINGS.monsterKillsOverlayHeight,
      mulingIndicatorOverlayWidth: DEFAULT_SETTINGS.mulingIndicatorOverlayWidth,
      mulingIndicatorOverlayHeight: DEFAULT_SETTINGS.mulingIndicatorOverlayHeight,
    });
  }

  get dropsTrackerTimersInGame(): boolean {
    return this._dropsTrackerTimersInGame;
  }

  private dropsTrackerActiveTimerDeltaMs(now = Date.now()): number {
    if (!this._dropsTrackerTimersInGame || this._settings.dropsTrackerMulingMode) {
      return 0;
    }

    const lastTick = this._settings.dropsTrackerTimerLastTickAtMs;
    const safeLastTick =
      typeof lastTick === "number" && Number.isFinite(lastTick) && lastTick > 0
        ? lastTick
        : now;

    return Math.max(0, now - safeLastTick);
  }

  getDropsTrackerRunDisplayElapsedMs(now = Date.now()): number {
    return (
      this.normalizeElapsedMs(this._settings.dropsTrackerRunElapsedMs) +
      this.dropsTrackerActiveTimerDeltaMs(now)
    );
  }

  getDropsTrackerSessionDisplayElapsedMs(now = Date.now()): number {
    return (
      this.normalizeElapsedMs(this._settings.dropsTrackerSessionElapsedMs) +
      this.dropsTrackerActiveTimerDeltaMs(now)
    );
  }

  setDropsTrackerTimersInGame(active: boolean): void {
    const now = Date.now();
    if (this._dropsTrackerTimersInGame === active) {
      return;
    }

    if (!active) {
      this.tickDropsTrackerTimers(now);
    }

    this._dropsTrackerTimersInGame = active;
    // Reset the tick baseline whenever gameplay pauses/resumes so lobby/menu
    // time is never backfilled into run/session timers.
    this.set("dropsTrackerTimerLastTickAtMs", now);
  }

  setDropsTrackerTimersDisplayActive(active: boolean): void {
    const now = Date.now();
    if (this._dropsTrackerTimersInGame === active) {
      return;
    }

    this._dropsTrackerTimersInGame = active;
    this._settings = {
      ...this._settings,
      dropsTrackerTimerLastTickAtMs: now,
    };
  }

  incrementDropsTrackerRunCount(): void {
    if (this._settings.dropsTrackerMulingMode) {
      return;
    }

    const now = Date.now();
    this.update({
      dropsTrackerRunCount: Math.max(
        0,
        (this._settings.dropsTrackerRunCount ?? 0) + 1,
      ),
      dropsTrackerRunStartedAtMs: now,
      dropsTrackerRunElapsedMs: 0,
      dropsTrackerTimerLastTickAtMs: now,
    });
  }

  resetDropsTrackerRunCount(): void {
    this.set("dropsTrackerRunCount", 0);
  }

  tickDropsTrackerTimers(now = Date.now()): void {
    const lastTick = this._settings.dropsTrackerTimerLastTickAtMs;
    const safeLastTick =
      typeof lastTick === "number" && Number.isFinite(lastTick) && lastTick > 0
        ? lastTick
        : now;

    if (!this._dropsTrackerTimersInGame) {
      // Timers only advance during a live game. The game-status handler resets
      // the tick baseline when entering/leaving gameplay, so menu/lobby time is
      // never backfilled.
      return;
    }

    if (this._settings.dropsTrackerMulingMode) {
      // Muling Mode is paused by setDropsTrackerMulingMode(), which captures
      // elapsed time before enabling and resets the baseline when disabled.
      return;
    }

    const deltaMs = Math.max(0, Math.min(now - safeLastTick, 60_000));
    if (deltaMs <= 0) return;

    this.update({
      dropsTrackerRunElapsedMs: this.normalizeElapsedMs(
        this._settings.dropsTrackerRunElapsedMs,
      ) + deltaMs,
      dropsTrackerSessionElapsedMs: this.normalizeElapsedMs(
        this._settings.dropsTrackerSessionElapsedMs,
      ) + deltaMs,
      dropsTrackerTimerLastTickAtMs: now,
    });
  }

  resetDropsTrackerRunTimer(): void {
    const now = Date.now();
    this.update({
      dropsTrackerRunElapsedMs: 0,
      dropsTrackerRunStartedAtMs: now,
      dropsTrackerTimerLastTickAtMs: now,
    });
  }

  resetDropsTrackerSessionTimer(): void {
    const now = Date.now();
    this.update({
      dropsTrackerSessionElapsedMs: 0,
      dropsTrackerSessionStartedAtMs: now,
      dropsTrackerTimerLastTickAtMs: now,
    });
  }

  resetDropsTrackerTimers(): void {
    const now = Date.now();
    this.update({
      dropsTrackerRunElapsedMs: 0,
      dropsTrackerSessionElapsedMs: 0,
      dropsTrackerRunStartedAtMs: now,
      dropsTrackerSessionStartedAtMs: now,
      dropsTrackerTimerLastTickAtMs: now,
    });
  }

  get totalDropsTrackerEnabled(): boolean {
    return this._settings.totalDropsTrackerEnabled;
  }

  setTotalDropsTrackerEnabled(enabled: boolean): void {
    this.set("totalDropsTrackerEnabled", enabled);
  }

  setDropsTrackerCategory(key: DropTrackerCategoryKey, enabled: boolean): void {
    this.set("dropsTrackerCategories", {
      ...this._settings.dropsTrackerCategories,
      [key]: enabled,
    });
  }

  setTotalDropsTrackerCategory(
    key: DropTrackerCategoryKey,
    enabled: boolean,
  ): void {
    this.set("totalDropsTrackerCategories", {
      ...this._settings.totalDropsTrackerCategories,
      [key]: enabled,
    });
  }

  setNotificationOverlayEnabled(enabled: boolean): void {
    this.set("notificationOverlayEnabled", enabled);
  }

  recordDropTrackerItem(
    name: string,
    categories: DropTrackerCategoryKey[],
    options: { isNewGrail?: boolean; source?: string } = {},
  ): void {
    if (categories.length === 0) return;
    const current = this._settings;
    const dropsCategories = categories.filter(
      (category) => current.dropsTrackerCategories[category],
    );
    const totalCategories = categories.filter(
      (category) => current.totalDropsTrackerCategories[category],
    );

    if (dropsCategories.length === 0 && totalCategories.length === 0) return;
    const runeName = runeNameFromDrop({ name });
    const runeTrackerCounts = runeName
      ? normalizeRuneTrackerCounts({
          ...current.runeTrackerCounts,
          [runeName]: (current.runeTrackerCounts[runeName] ?? 0) + 1,
        })
      : current.runeTrackerCounts;
    const tracksUnique = categories.includes("unique") || categories.includes("hellforged");
    const cleanName = canonicalTrackedItemName(name) || cleanTrackedItemName(name) || "";
    const achievementStats = tracksUnique
      ? normalizeAchievementStats({
          ...current.achievementStats,
          uniqueItemsFound: (current.achievementStats.uniqueItemsFound ?? 0) + 1,
          firstEliteUniqueName:
            current.achievementStats.firstEliteUniqueName ??
            (ELITE_UNIQUE_NAMES.has(cleanName) ? cleanName : null),
        })
      : current.achievementStats;

    const recentItem: DropTrackerRecentItem = {
      id: `${Date.now()}-${Math.random().toString(36).slice(2)}`,
      timestampMs: Date.now(),
      name: cleanName || "Unknown item",
      isNewGrail: options.isNewGrail === true,
      categories: Array.from(new Set(categories)),
      dropsTrackerCategories: Array.from(new Set(dropsCategories)),
      totalDropsTrackerCategories: Array.from(new Set(totalCategories)),
      source: options.source?.trim() || "manual",
    };

    this.update({
      dropsTrackerCounts: normalizeCounts(
        incrementCounts(
          current.dropsTrackerCounts,
          categories,
          current.dropsTrackerCategories,
        ),
      ),
      totalDropsTrackerCounts: normalizeCounts(
        incrementCounts(
          current.totalDropsTrackerCounts,
          categories,
          current.totalDropsTrackerCategories,
        ),
      ),
      runeTrackerCounts,
      achievementStats,
      dropsTrackerRecentItems: [
        recentItem,
        ...current.dropsTrackerRecentItems,
      ].slice(0, 20),
    });
  }

  recordGrailOnlyRecentItem(
    name: string,
    categories: DropTrackerCategoryKey[],
    options: { source?: string } = {},
  ): void {
    const cleanName = canonicalTrackedItemName(name) || cleanTrackedItemName(name) || "";
    const recentItem: DropTrackerRecentItem = {
      id: `${Date.now()}-${Math.random().toString(36).slice(2)}`,
      timestampMs: Date.now(),
      name: cleanName || "Unknown item",
      isNewGrail: true,
      categories: Array.from(new Set(categories)),
      dropsTrackerCategories: [],
      totalDropsTrackerCategories: [],
      source: options.source?.trim() || "identify-inventory grail-only",
    };

    this.update({
      dropsTrackerRecentItems: [
        recentItem,
        ...this._settings.dropsTrackerRecentItems,
      ].slice(0, 20),
    });
  }

  incrementDropTrackerCounts(categories: DropTrackerCategoryKey[]): void {
    this.recordDropTrackerItem("Unknown item", categories, { source: "manual" });
  }

  mergeDropTrackerStateSnapshot(snapshot: DropTrackerStateSnapshot): void {
    const partial: Partial<AppSettings> = {
      ...(snapshot.dropsTrackerCounts
        ? { dropsTrackerCounts: normalizeCounts(snapshot.dropsTrackerCounts) }
        : {}),
      ...(snapshot.totalDropsTrackerCounts
        ? { totalDropsTrackerCounts: normalizeCounts(snapshot.totalDropsTrackerCounts) }
        : {}),
      ...(snapshot.dropsTrackerRecentItems
        ? { dropsTrackerRecentItems: this.normalizeRecentItems(snapshot.dropsTrackerRecentItems) }
        : {}),
      ...(snapshot.runeTrackerCounts
        ? { runeTrackerCounts: normalizeRuneTrackerCounts(snapshot.runeTrackerCounts) }
        : {}),
      ...(snapshot.materialTrackerCounts
        ? { materialTrackerCounts: normalizeMaterialTrackerCounts(snapshot.materialTrackerCounts) }
        : {}),
      ...(snapshot.fateCardCounts
        ? { fateCardCounts: this.normalizeFateCardCounts(snapshot.fateCardCounts) }
        : {}),
      ...(snapshot.fateCardDropCounts
        ? { fateCardDropCounts: this.normalizeFateCardCounts(snapshot.fateCardDropCounts) }
        : {}),
      ...(snapshot.achievementStats
        ? { achievementStats: normalizeAchievementStats(snapshot.achievementStats) }
        : {}),
    };
    if (Object.keys(partial).length === 0) return;
    this.update(partial);
  }

  removeRecentDropTrackerItem(id: string): void {
    const item = this._settings.dropsTrackerRecentItems.find(
      (entry) => entry.id === id,
    );
    if (!item) return;
    this.update({
      dropsTrackerCounts: decrementCounts(
        this._settings.dropsTrackerCounts,
        item.dropsTrackerCategories,
      ),
      totalDropsTrackerCounts: decrementCounts(
        this._settings.totalDropsTrackerCounts,
        item.totalDropsTrackerCategories,
      ),
      dropsTrackerRecentItems: this._settings.dropsTrackerRecentItems.filter(
        (entry) => entry.id !== id,
      ),
    });
  }

  setDropsTrackerCounts(counts: DropTrackerCounts): void {
    this.set("dropsTrackerCounts", normalizeCounts(counts));
  }

  setTotalDropsTrackerCounts(counts: DropTrackerCounts): void {
    this.set("totalDropsTrackerCounts", normalizeCounts(counts));
  }

  resetDropsTrackerCounts(): void {
    this.update({
      dropsTrackerCounts: emptyDropTrackerCounts(),
      dropsTrackerRecentItems: [],
    });
  }

  resetTotalDropsTrackerCounts(): void {
    this.setTotalDropsTrackerCounts(emptyDropTrackerCounts());
  }

  setRuneTrackerCounts(counts: RuneTrackerCounts): void {
    this.set("runeTrackerCounts", normalizeRuneTrackerCounts(counts));
  }

  resetRuneTrackerCounts(): void {
    this.setRuneTrackerCounts(defaultRuneTrackerCounts());
  }

  setMaterialTrackerCounts(counts: MaterialTrackerCounts): void {
    this.set("materialTrackerCounts", normalizeMaterialTrackerCounts(counts));
  }

  resetMaterialTrackerCounts(): void {
    this.setMaterialTrackerCounts(defaultMaterialTrackerCounts());
  }

  recordMaterialTrackerDrop(material: MaterialTrackerName): void {
    this.setMaterialTrackerCounts({
      ...this._settings.materialTrackerCounts,
      [material]: (this._settings.materialTrackerCounts[material] ?? 0) + 1,
    });
  }

  setAchievementStats(stats: AchievementStats): void {
    this.set("achievementStats", normalizeAchievementStats(stats));
  }

  resetAchievementProgress(): void {
    this.update({
      achievementStats: defaultAchievementStats(),
      runeTrackerCounts: defaultRuneTrackerCounts(),
      holyGrailFound: {},
    });
  }

  resetAccountStatsAchievementProgress(): void {
    const stats = normalizeAchievementStats(this._settings.achievementStats);
    const unlocked = Object.fromEntries(
      Object.entries(stats.unlocked).filter(([id]) => !id.startsWith("kills-") && !id.startsWith("boss-")),
    );
    const history = stats.history.filter(
      (entry) => entry.category !== "Kills" && entry.category !== "Bossing",
    );
    this.setAchievementStats({
      ...stats,
      totalKills: 0,
      bossKills: {},
      unlocked,
      history,
    });
  }

  updateAchievementStats(partial: Partial<AchievementStats>): void {
    this.setAchievementStats({
      ...this._settings.achievementStats,
      ...partial,
    });
  }

  setAchievementSettings(settings: Partial<AchievementSettings>): void {
    this.set("achievementSettings", normalizeAchievementSettings({
      ...this._settings.achievementSettings,
      ...settings,
    }));
  }

  setAchievementCharacterLevel(character: AchievementCharacterLevel): void {
    const key = character.name.trim() || `Character ${Date.now()}`;
    this.updateAchievementStats({
      characterLevels: {
        ...this._settings.achievementStats.characterLevels,
        [key]: {
          name: key,
          className: character.className.trim(),
          level: Math.max(1, Math.min(99, Math.floor(character.level || 1))),
        },
      },
    });
  }

  removeAchievementCharacterLevel(name: string): void {
    const next = { ...this._settings.achievementStats.characterLevels };
    delete next[name];
    this.updateAchievementStats({ characterLevels: next });
  }

  setAchievementBossKills(key: string, count: number): void {
    this.updateAchievementStats({
      bossKills: {
        ...this._settings.achievementStats.bossKills,
        [key]: Math.max(0, Math.floor(count || 0)),
      },
    });
  }

  recordAchievementMaterialDrop(name: string): string | null {
    const materialName = materialAchievementNameFromDrop(name);
    if (!materialName) return null;
    const key = materialAchievementKey(materialName);
    this.updateAchievementStats({
      materialFinds: {
        ...this._settings.achievementStats.materialFinds,
        [key]: (this._settings.achievementStats.materialFinds[key] ?? 0) + 1,
      },
    });
    return materialName;
  }

  recordAchievementFateCardDrop(name: string): string | null {
    const card = fateCardInfo(name);
    if (!card) return null;
    this.updateAchievementStats({
      fateCardsFound: (this._settings.achievementStats.fateCardsFound ?? 0) + 1,
      tier0FateCardsFound:
        (this._settings.achievementStats.tier0FateCardsFound ?? 0) + (card.tier === 0 ? 1 : 0),
    });
    return card.name;
  }

  evaluateAchievementUnlocks(ctx: Omit<AchievementContext, "stats">): AchievementUnlockEntry[] {
    const stats = normalizeAchievementStats(this._settings.achievementStats);
    const progress = evaluateAchievements({ ...ctx, stats });
    const now = new Date().toISOString();
    const newUnlocks = progress.filter((achievement) => achievement.complete && !stats.unlocked[achievement.id]);
    if (newUnlocks.length === 0) return [];

    const entries: AchievementUnlockEntry[] = newUnlocks.map((achievement) => ({
      id: achievement.id,
      name: achievement.name,
      category: achievement.category,
      tier: achievement.tier,
      unlockedAt: now,
      detail: achievementDetail(achievement.id, { ...ctx, stats }),
    }));
    const unlocked = { ...stats.unlocked };
    for (const entry of entries) unlocked[entry.id] = entry.unlockedAt;
    const history = [...entries, ...stats.history].slice(0, 200);
    this.setAchievementStats({ ...stats, unlocked, history });
    for (const entry of entries) {
      void emit("achievement-unlocked", entry);
    }
    return entries;
  }

  /** Set notification anchor position (percentages 0-100) */
  setNotificationPosition(x: number, y: number): void {
    this.update({ notificationX: x, notificationY: y });
  }

  /** Enable/disable verbose per-item filter logging. Persists and flips the
   *  scanner-side atomic immediately (saved settings only seed on next
   *  startup, so we also push the change through a dedicated command). */
  async setVerboseFilterLogging(enabled: boolean): Promise<void> {
    this.set("verboseFilterLogging", enabled);
    try {
      await invoke("set_verbose_filter_logging", { enabled });
    } catch (error) {
      console.error(
        "[Settings] Failed to update verbose filter logging:",
        error,
      );
    }
  }

}

/** Global settings store instance */
export const settingsStore = new SettingsStore();

/**
 * Window state management utilities
 */
export const windowState = {
  /** Load window state from backend */
  async load(windowLabel: string): Promise<WindowState | null> {
    try {
      const state = await invoke<WindowState | null>("get_window_state", {
        windowLabel,
      });
      return state;
    } catch (error) {
      console.error(`[WindowState] Failed to load for ${windowLabel}:`, error);
      return null;
    }
  },

  /** Save window state to backend */
  async save(windowLabel: string, state: WindowState): Promise<void> {
    try {
      await invoke("save_window_state", { windowLabel, state });
    } catch (error) {
      console.error(`[WindowState] Failed to save for ${windowLabel}:`, error);
    }
  },
};
