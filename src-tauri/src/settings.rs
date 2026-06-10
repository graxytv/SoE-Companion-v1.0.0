//! Settings persistence module for D2MXLUtils
//!
//! Handles loading and saving application settings using tauri-plugin-store.
//! Settings are stored in a JSON file in the app's data directory.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;
use std::sync::{LazyLock, Mutex};
use std::time::{Duration, Instant};
use tauri::{AppHandle, Emitter, Manager};
use tauri_plugin_store::StoreExt;

use crate::hotkeys::HotkeyConfig;
use crate::logger::{error as log_error, info as log_info};

const SETTINGS_FILE: &str = "settings.json";
const HOLY_GRAIL_BACKUP_FILE: &str = "holy-grail-backup.json";
const HOLY_GRAIL_BACKUP_DIR: &str = "holy-grail-backups";
const FATE_CARD_BACKUP_FILE: &str = "fate-card-counts-backup.json";
const FATE_CARD_BACKUP_DIR: &str = "fate-card-counts-backups";
const ACHIEVEMENT_BACKUP_FILE: &str = "achievements-backup.json";
const ACHIEVEMENT_BACKUP_DIR: &str = "achievement-backups";
static SETTINGS_SAVE_LOCK: LazyLock<Mutex<()>> = LazyLock::new(|| Mutex::new(()));
static LAST_ROUTINE_BACKUP_WRITE: LazyLock<Mutex<Option<Instant>>> =
    LazyLock::new(|| Mutex::new(None));
const ROUTINE_BACKUP_MIN_INTERVAL: Duration = Duration::from_secs(30);

/// One configurable drop-sound slot. Index in `AppSettings.sounds` + 1
/// equals the DSL keyword index (e.g. element 0 -> `sound1`).
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SoundSlot {
    pub label: String,
    pub volume: f32,
    pub source: SoundSource,
}

/// What plays for a given slot.
/// - `Default`: bundled `public/sounds/{N}.mp3` (slots 1..=7 only).
/// - `Custom`: user-imported file in `app_data_dir/sounds/`.
/// - `Empty`: silence; only for slots >= 8 after deletion.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(
    tag = "kind",
    rename_all = "camelCase",
    rename_all_fields = "camelCase"
)]
pub enum SoundSource {
    Default,
    Custom { file_name: String },
    Empty,
}

fn default_sounds() -> Vec<SoundSlot> {
    (1..=7)
        .map(|n| SoundSlot {
            label: format!("Sound {}", n),
            volume: 0.8,
            source: SoundSource::Default,
        })
        .collect()
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct DropTrackerRecentItem {
    pub id: String,
    pub timestamp_ms: u64,
    pub name: String,
    #[serde(default)]
    pub is_new_grail: bool,
    #[serde(default)]
    pub categories: Vec<String>,
    #[serde(default)]
    pub drops_tracker_categories: Vec<String>,
    #[serde(default)]
    pub total_drops_tracker_categories: Vec<String>,
    #[serde(default)]
    pub source: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct OverlayPosition {
    #[serde(default)]
    pub x: Option<f64>,
    #[serde(default)]
    pub y: Option<f64>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct HolyGrailFoundEntry {
    pub key: String,
    pub name: String,
    pub category: String,
    pub first_found_at: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct HolyGrailBackup {
    pub version: u32,
    pub exported_at: String,
    #[serde(default)]
    pub found: HashMap<String, HolyGrailFoundEntry>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct HolyGrailBackupStatus {
    pub backup_exists: bool,
    pub backup_path: String,
    pub found_count: usize,
    pub exported_at: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct FateCardBackup {
    pub version: u32,
    pub exported_at: String,
    #[serde(default)]
    pub counts: HashMap<String, u32>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct FateCardBackupStatus {
    pub backup_exists: bool,
    pub backup_path: String,
    pub card_count: usize,
    pub exported_at: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct AchievementBackup {
    pub version: u32,
    pub exported_at: String,
    #[serde(default = "default_achievement_stats")]
    pub stats: serde_json::Value,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct AchievementBackupStatus {
    pub backup_exists: bool,
    pub backup_path: String,
    pub exported_at: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct GsfWantedItem {
    pub id: String,
    pub item_name: String,
    pub normalized_item_name: String,
    pub category: String,
    pub slot: String,
    pub status: String,
    pub notes: String,
    pub created_at: String,
    pub updated_at: String,
    pub found_at: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct GsfPlayer {
    pub id: String,
    pub name: String,
    #[serde(default)]
    pub class_name: String,
    #[serde(default)]
    pub build_name: String,
    pub notes: String,
    #[serde(default)]
    pub wanted_items: Vec<GsfWantedItem>,
    pub created_at: String,
    pub updated_at: String,
}

/// Application settings structure
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AppSettings {
    /// UI theme.
    #[serde(default = "default_theme")]
    pub theme: String,

    /// Master multiplier for drop notification sounds (0.0 - 1.0). Final played gain = `sound_volume * slot.volume`.
    #[serde(default = "default_volume")]
    pub sound_volume: f32,

    /// Active loot filter profile name
    #[serde(default)]
    pub active_profile: Option<String>,

    /// Path to the SoE launcher executable.
    #[serde(default)]
    pub soe_launcher_path: Option<String>,

    /// Path to the user's ProjectD2 folder.
    #[serde(default)]
    pub project_d2_path: Option<String>,

    /// Optional user-selected ProjectD2 shared stash file for runeword planning.
    #[serde(default)]
    pub runeword_planner_stash_path: Option<String>,

    /// Saved main tab order.
    #[serde(default = "default_main_tab_order")]
    pub main_tab_order: Vec<String>,

    /// Saved Drops Tracker sub-tab order.
    #[serde(default = "default_drops_tracker_sub_tab_order")]
    pub drops_tracker_sub_tab_order: Vec<String>,

    /// Saved Holy Grail sub-tab order.
    #[serde(default = "default_holy_grail_sub_tab_order")]
    pub holy_grail_sub_tab_order: Vec<String>,

    /// Account-wide achievement progress, unlocks, and history.
    #[serde(default = "default_achievement_stats")]
    pub achievement_stats: serde_json::Value,

    /// Achievement overlay and sound settings.
    #[serde(default = "default_achievement_settings")]
    pub achievement_settings: serde_json::Value,

    /// Notification display duration in milliseconds
    #[serde(default = "default_notification_duration")]
    pub notification_duration: u32,

    /// Notification stack direction: "up" or "down"
    #[serde(default = "default_stack_direction")]
    pub notification_stack_direction: String,

    /// Notification font size in pixels
    #[serde(default = "default_notification_font_size")]
    pub notification_font_size: u32,

    /// Notification background opacity (0.0 - 1.0)
    #[serde(default = "default_notification_opacity")]
    pub notification_opacity: f32,

    /// When true, visual item-drop notifications are rendered on the overlay.
    #[serde(default = "default_true")]
    pub notification_overlay_enabled: bool,

    /// When true, unidentified unique/set scanner drops can still show notifications.
    #[serde(default)]
    pub notify_unidentified_unique_set_drops: bool,

    /// Notification position X offset from edge (percentage 0-100)
    #[serde(default = "default_notification_x")]
    pub notification_x: f32,

    /// Notification position Y offset from edge (percentage 0-100)
    #[serde(default = "default_notification_y")]
    pub notification_y: f32,

    /// Legacy compact notification layout setting.
    #[serde(default)]
    pub compact_name: bool,

    /// Hotkey configuration for toggling main window
    #[serde(default)]
    pub toggle_window_hotkey: HotkeyConfig,

    /// Hotkey held to enter overlay edit mode (drag notification anchor)
    #[serde(default = "default_edit_overlay_hotkey")]
    pub edit_overlay_hotkey: HotkeyConfig,

    /// Hotkey held to reveal every item on the ground, bypassing `hide` rules
    #[serde(default = "default_reveal_hidden_hotkey")]
    pub reveal_hidden_hotkey: HotkeyConfig,

    /// Hotkey to toggle the in-game loot history overlay panel.
    #[serde(default = "default_loot_history_hotkey")]
    pub loot_history_hotkey: HotkeyConfig,

    /// Hotkey to manually reset the Drops Tracker counts/history.
    #[serde(default = "default_reset_drops_tracker_hotkey")]
    pub reset_drops_tracker_hotkey: HotkeyConfig,

    /// Hotkey to toggle Muling Mode on/off.
    #[serde(default = "default_muling_mode_hotkey")]
    pub muling_mode_hotkey: HotkeyConfig,

    /// Hotkey that runs the experimental game reset automation.
    #[serde(default = "default_game_reset_hotkey")]
    pub game_reset_hotkey: HotkeyConfig,

    /// When true, show the Drops Tracker counter on the overlay.
    #[serde(default = "default_drops_tracker_enabled")]
    pub drops_tracker_enabled: bool,

    /// When true, show the run counter row on the Drops Tracker overlay.
    #[serde(default = "default_true")]
    pub drops_tracker_run_counter_enabled: bool,

    /// When true, show the current run timer row on the Drops Tracker overlay.
    #[serde(default = "default_true")]
    pub drops_tracker_run_timer_enabled: bool,

    /// When true, show the total session timer row on the Drops Tracker overlay.
    #[serde(default = "default_true")]
    pub drops_tracker_session_timer_enabled: bool,

    /// Number of games entered since the run counter was last reset.
    #[serde(default)]
    pub drops_tracker_run_count: u32,

    /// Timestamp in ms when the current run timer started.
    #[serde(default)]
    pub drops_tracker_run_started_at_ms: u64,

    /// Timestamp in ms when the current session timer started.
    #[serde(default)]
    pub drops_tracker_session_started_at_ms: u64,

    /// Accumulated elapsed time for the current run timer.
    #[serde(default)]
    pub drops_tracker_run_elapsed_ms: u64,

    /// Accumulated elapsed time for the total session timer.
    #[serde(default)]
    pub drops_tracker_session_elapsed_ms: u64,

    /// Timestamp in ms of the last timer accumulation tick.
    #[serde(default)]
    pub drops_tracker_timer_last_tick_at_ms: u64,

    /// When true, show the persistent Total Drops overlay.
    #[serde(default = "default_drops_tracker_enabled")]
    pub total_drops_tracker_enabled: bool,

    /// Per-category tracking/display toggles for the Drops Tracker overlay.
    #[serde(default = "default_drops_tracker_categories")]
    pub drops_tracker_categories: HashMap<String, bool>,

    /// Per-category tracking/display toggles for the Total Drops overlay.
    #[serde(default = "default_total_drops_tracker_categories")]
    pub total_drops_tracker_categories: HashMap<String, bool>,

    /// Counts for the manually reset Drops Tracker overlay.
    #[serde(default)]
    pub drops_tracker_counts: HashMap<String, u32>,

    /// Persistent counts for the Total Drops overlay.
    #[serde(default)]
    pub total_drops_tracker_counts: HashMap<String, u32>,

    /// Most recent items counted by Drops Tracker, newest first.
    #[serde(default)]
    pub drops_tracker_recent_items: Vec<DropTrackerRecentItem>,

    /// When true, suppress repeated sightings of the same item during a single run.
    #[serde(default = "default_true")]
    pub drops_tracker_prevent_duplicates: bool,

    /// When true, temporarily pause drop and holy grail tracking while muling items.
    #[serde(default)]
    pub drops_tracker_muling_mode: bool,

    /// Timestamp in ms when Muling Mode was enabled.
    #[serde(default)]
    pub drops_tracker_muling_started_at_ms: Option<u64>,

    /// When true, Drops Tracker overlays accept mouse drag events for positioning.
    #[serde(default)]
    pub drops_tracker_overlay_position_unlocked: bool,

    /// When true, tracker-style overlays render in a separate movable window.
    #[serde(default)]
    pub tracker_overlays_separate_window: bool,

    /// Saved position for the Drops Tracker overlay.
    #[serde(default = "default_drops_tracker_overlay_position")]
    pub drops_tracker_overlay_position: OverlayPosition,

    /// Width in pixels for the Drops Tracker overlay.
    #[serde(default = "default_tracker_overlay_width")]
    pub drops_tracker_overlay_width: u32,

    /// Saved position for the Total Drops overlay.
    #[serde(default = "default_total_drops_overlay_position")]
    pub total_drops_overlay_position: OverlayPosition,

    /// Width in pixels for the Total Drops overlay.
    #[serde(default = "default_tracker_overlay_width")]
    pub total_drops_overlay_width: u32,

    /// Counts for individual rune drops.
    #[serde(default)]
    pub rune_tracker_counts: HashMap<String, u32>,

    /// When true, show the Rune Tracker overlay.
    #[serde(default)]
    pub rune_tracker_overlay_enabled: bool,

    /// Per-rune visibility for the Rune Tracker overlay.
    #[serde(default = "default_rune_tracker_overlay_runes")]
    pub rune_tracker_overlay_runes: HashMap<String, bool>,

    /// Saved position for the Rune Tracker overlay.
    #[serde(default = "default_rune_tracker_overlay_position")]
    pub rune_tracker_overlay_position: OverlayPosition,

    /// Width in pixels for the Rune Tracker overlay.
    #[serde(default = "default_tracker_overlay_width")]
    pub rune_tracker_overlay_width: u32,

    /// Items found in the Holy Grail checklist.
    #[serde(default)]
    pub holy_grail_found: HashMap<String, HolyGrailFoundEntry>,

    /// Known Fate Card counts keyed by canonical card name.
    #[serde(default)]
    pub fate_card_counts: HashMap<String, u32>,

    /// Live Fate Card drop counts keyed by canonical card name.
    #[serde(default)]
    pub fate_card_drop_counts: HashMap<String, u32>,

    /// When true, show the compact Grail Progress overlay.
    #[serde(default)]
    pub holy_grail_overlay_enabled: bool,

    /// When true, show total grail progress on the overlay.
    #[serde(default = "default_true")]
    pub holy_grail_overlay_show_total: bool,

    /// When true, show the latest grail find on the overlay.
    #[serde(default = "default_true")]
    pub holy_grail_overlay_show_latest: bool,

    /// Category rows shown on the Grail Progress overlay.
    #[serde(default = "default_holy_grail_overlay_categories")]
    pub holy_grail_overlay_categories: HashMap<String, bool>,

    /// Saved position for the Grail Progress overlay.
    #[serde(default = "default_holy_grail_overlay_position")]
    pub holy_grail_overlay_position: OverlayPosition,

    /// Width in pixels for the Grail Progress overlay.
    #[serde(default = "default_tracker_overlay_width")]
    pub holy_grail_overlay_width: u32,

    /// Saved position for the small Muling Mode hotkey/status indicator.
    #[serde(default = "default_muling_indicator_overlay_position")]
    pub muling_indicator_overlay_position: OverlayPosition,

    /// Difficulty key used by the experimental save-exit reset automation.
    #[serde(default = "default_save_exit_automation_difficulty")]
    pub save_exit_automation_difficulty: String,

    /// Client-area X coordinate used to click Single Player on the main menu.
    #[serde(default = "default_save_exit_automation_click_x")]
    pub save_exit_automation_click_x: i32,

    /// Client-area Y coordinate used to click Single Player on the main menu.
    #[serde(default = "default_save_exit_automation_click_y")]
    pub save_exit_automation_click_y: i32,

    /// When true, click coordinates are interpreted as percentages.
    #[serde(default)]
    pub save_exit_automation_coordinate_mode_percent: bool,

    /// Delay between automation input steps.
    #[serde(default = "default_save_exit_automation_delay_ms")]
    pub save_exit_automation_delay_ms: u32,

    /// Maximum wait for D2Client to report that gameplay has ended.
    #[serde(default = "default_save_exit_automation_main_menu_wait_ms")]
    pub save_exit_automation_main_menu_wait_ms: u32,

    /// When true, a lightweight watcher asks the frontend to sync after
    /// Diablo II loading/zone transitions settle.
    #[serde(default)]
    pub zone_transition_sync_enabled: bool,

    /// When true, GSF matching and notifications are enabled.
    #[serde(default = "default_true")]
    pub gsf_enabled: bool,

    /// When true, matched GSF drops add Needed by text to loot notifications.
    #[serde(default = "default_true")]
    pub gsf_notification_enabled: bool,

    /// 1-based sound-slot index played when a GSF wanted item drops.
    #[serde(default)]
    pub gsf_sound_slot: Option<u32>,

    /// Volume multiplier for the dedicated GSF match sound.
    #[serde(default = "default_unit_volume")]
    pub gsf_sound_volume: f32,

    /// Group/shared-find players and wanted item entries.
    #[serde(default)]
    pub gsf_players: Vec<GsfPlayer>,

    /// When true, show the rainbow **New Item!** header for first-time Holy Grail drops.
    #[serde(default = "default_true")]
    pub holy_grail_new_item_notification_enabled: bool,

    /// 1-based sound-slot index played when a new Holy Grail item is first found.
    #[serde(default)]
    pub holy_grail_new_item_sound_slot: Option<u32>,

    /// Volume multiplier for the dedicated Holy Grail new-item sound.
    #[serde(default = "default_unit_volume")]
    pub holy_grail_new_item_sound_volume: f32,

    /// When true, scanner logs per-item filter decisions (noisy; opt-in for debugging).
    #[serde(default)]
    pub verbose_filter_logging: bool,

    /// Per-slot drop sounds. Slot index = element position + 1.
    /// Final played gain = `sound_volume * slot.volume`.
    #[serde(default = "default_sounds")]
    pub sounds: Vec<SoundSlot>,

    /// 1-based sound-slot index played when a goblin appears in the
    /// scanner's view. `None` disables the feature.
    #[serde(default)]
    pub goblin_alert_slot: Option<u32>,

    /// Preserve newer frontend settings that Rust does not need to inspect.
    #[serde(flatten)]
    pub extra: HashMap<String, serde_json::Value>,
}

/// Window state for persistence
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct WindowState {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
    pub maximized: bool,
}

// Default value functions
fn default_theme() -> String {
    "sanctuary".to_string()
}

fn default_volume() -> f32 {
    0.8
}

fn default_main_tab_order() -> Vec<String> {
    [
        "home",
        "general",
        "drops-tracker",
        "lootfilter",
        "notifications",
        "sounds",
        "achievements",
        "holy-grail",
        "soe-wiki",
    ]
    .iter()
    .map(|s| (*s).to_string())
    .collect()
}

fn default_drops_tracker_sub_tab_order() -> Vec<String> {
    [
        "overview",
        "tracker-settings",
        "drops-overlay",
        "total-overlay",
        "rune-tracker",
        "muling-mode",
    ]
    .iter()
    .map(|s| (*s).to_string())
    .collect()
}

fn default_holy_grail_sub_tab_order() -> Vec<String> {
    ["overview", "import", "overlay", "notifications", "backup"]
        .iter()
        .map(|s| (*s).to_string())
        .collect()
}

fn default_achievement_stats() -> serde_json::Value {
    serde_json::json!({
        "uniqueItemsFound": 0,
        "eliteUniqueItemsFound": 0,
        "firstEliteUniqueName": null,
        "materialFinds": {},
        "fateCardsFound": 0,
        "tier0FateCardsFound": 0,
        "totalKills": 0,
        "bossKills": {},
        "characterLevels": {},
        "corruptions": 0,
        "bricks": 0,
        "currentBrickStreak": 0,
        "bestBrickStreak": 0,
        "unlocked": {},
        "history": []
    })
}

fn default_achievement_settings() -> serde_json::Value {
    serde_json::json!({
        "overlayEnabled": true,
        "overlayDuration": 6000,
        "overlayFontSize": 15,
        "overlayOpacity": 0.94,
        "soundEnabled": true,
        "soundSlot": null,
        "soundVolume": 1.0
    })
}

fn default_notification_duration() -> u32 {
    5000
}

fn default_stack_direction() -> String {
    "up".to_string()
}

fn default_notification_font_size() -> u32 {
    14
}

fn default_notification_opacity() -> f32 {
    0.9
}

fn default_notification_x() -> f32 {
    1.0
}

fn default_notification_y() -> f32 {
    1.0
}

fn default_drops_tracker_overlay_position() -> OverlayPosition {
    OverlayPosition {
        x: Some(12.0),
        y: Some(12.0),
    }
}

fn default_total_drops_overlay_position() -> OverlayPosition {
    OverlayPosition {
        x: Some(12.0),
        y: Some(160.0),
    }
}

fn default_holy_grail_overlay_position() -> OverlayPosition {
    OverlayPosition {
        x: Some(12.0),
        y: Some(304.0),
    }
}

fn default_rune_tracker_overlay_position() -> OverlayPosition {
    OverlayPosition {
        x: Some(12.0),
        y: Some(448.0),
    }
}

fn default_muling_indicator_overlay_position() -> OverlayPosition {
    OverlayPosition {
        x: Some(12.0),
        y: Some(640.0),
    }
}

fn default_save_exit_automation_difficulty() -> String {
    "Normal".to_string()
}

fn default_save_exit_automation_click_x() -> i32 {
    400
}

fn default_save_exit_automation_click_y() -> i32 {
    340
}

fn default_save_exit_automation_delay_ms() -> u32 {
    300
}

fn default_save_exit_automation_main_menu_wait_ms() -> u32 {
    10000
}

fn default_tracker_overlay_width() -> u32 {
    238
}

fn default_edit_overlay_hotkey() -> HotkeyConfig {
    HotkeyConfig {
        key_code: 0,
        modifiers: 0x0001 | 0x0002, // MOD_ALT | MOD_CONTROL
        display: "Ctrl+Alt".to_string(),
    }
}

fn default_reveal_hidden_hotkey() -> HotkeyConfig {
    HotkeyConfig {
        key_code: 0x5A, // 'Z'
        modifiers: 0,
        display: "Z".to_string(),
    }
}

fn default_loot_history_hotkey() -> HotkeyConfig {
    HotkeyConfig {
        key_code: 0x4E, // 'N'
        modifiers: 0,
        display: "N".to_string(),
    }
}

fn default_reset_drops_tracker_hotkey() -> HotkeyConfig {
    HotkeyConfig {
        key_code: 0,
        modifiers: 0,
        display: "None".to_string(),
    }
}

fn default_muling_mode_hotkey() -> HotkeyConfig {
    HotkeyConfig {
        key_code: 0,
        modifiers: 0,
        display: "None".to_string(),
    }
}

fn default_game_reset_hotkey() -> HotkeyConfig {
    HotkeyConfig {
        key_code: 0x7B,
        modifiers: 0,
        display: "F12".to_string(),
    }
}

fn default_drops_tracker_enabled() -> bool {
    true
}

fn default_true() -> bool {
    true
}

fn default_unit_volume() -> f32 {
    1.0
}

const HOLY_GRAIL_CATEGORY_KEYS: &[&str] = &["su", "ssu", "sets", "runewords"];

fn default_holy_grail_overlay_categories() -> HashMap<String, bool> {
    HOLY_GRAIL_CATEGORY_KEYS
        .iter()
        .map(|key| ((*key).to_string(), false))
        .collect()
}

const DROP_TRACKER_CATEGORY_KEYS: &[&str] = &[
    "unique",
    "hellforged",
    "sets",
    "rare",
    "magic",
    "lowRune",
    "midRune",
    "highRune",
    "charm",
    "jewel",
    "perfectGem",
    "token",
];

const RUNE_NAMES: &[&str] = &[
    "El", "Eld", "Tir", "Nef", "Eth", "Ith", "Tal", "Ral", "Ort", "Thul", "Amn", "Sol", "Shael",
    "Dol", "Hel", "Io", "Lum", "Ko", "Fal", "Lem", "Pul", "Um", "Mal", "Ist", "Gul", "Vex", "Ohm",
    "Lo", "Sur", "Ber", "Jah", "Cham", "Zod",
];

fn default_drops_tracker_categories() -> HashMap<String, bool> {
    DROP_TRACKER_CATEGORY_KEYS
        .iter()
        .map(|key| {
            (
                (*key).to_string(),
                matches!(*key, "unique" | "hellforged" | "sets" | "fateCard"),
            )
        })
        .collect()
}

fn default_total_drops_tracker_categories() -> HashMap<String, bool> {
    DROP_TRACKER_CATEGORY_KEYS
        .iter()
        .map(|key| ((*key).to_string(), true))
        .collect()
}

fn default_rune_tracker_overlay_runes() -> HashMap<String, bool> {
    RUNE_NAMES
        .iter()
        .map(|key| ((*key).to_string(), true))
        .collect()
}

impl Default for OverlayPosition {
    fn default() -> Self {
        OverlayPosition { x: None, y: None }
    }
}

impl Default for AppSettings {
    fn default() -> Self {
        Self {
            theme: default_theme(),
            sound_volume: default_volume(),
            active_profile: None,
            soe_launcher_path: None,
            project_d2_path: None,
            runeword_planner_stash_path: None,
            main_tab_order: default_main_tab_order(),
            drops_tracker_sub_tab_order: default_drops_tracker_sub_tab_order(),
            holy_grail_sub_tab_order: default_holy_grail_sub_tab_order(),
            achievement_stats: default_achievement_stats(),
            achievement_settings: default_achievement_settings(),
            notification_duration: default_notification_duration(),
            notification_stack_direction: default_stack_direction(),
            notification_font_size: default_notification_font_size(),
            notification_opacity: default_notification_opacity(),
            notification_overlay_enabled: true,
            notify_unidentified_unique_set_drops: false,
            notification_x: default_notification_x(),
            notification_y: default_notification_y(),
            compact_name: false,
            toggle_window_hotkey: HotkeyConfig::default(),
            edit_overlay_hotkey: default_edit_overlay_hotkey(),
            reveal_hidden_hotkey: default_reveal_hidden_hotkey(),
            loot_history_hotkey: default_loot_history_hotkey(),
            reset_drops_tracker_hotkey: default_reset_drops_tracker_hotkey(),
            muling_mode_hotkey: default_muling_mode_hotkey(),
            game_reset_hotkey: default_game_reset_hotkey(),
            drops_tracker_enabled: default_drops_tracker_enabled(),
            drops_tracker_run_counter_enabled: default_true(),
            drops_tracker_run_timer_enabled: default_true(),
            drops_tracker_session_timer_enabled: default_true(),
            drops_tracker_run_count: 0,
            drops_tracker_run_started_at_ms: 0,
            drops_tracker_session_started_at_ms: 0,
            drops_tracker_run_elapsed_ms: 0,
            drops_tracker_session_elapsed_ms: 0,
            drops_tracker_timer_last_tick_at_ms: 0,
            total_drops_tracker_enabled: default_drops_tracker_enabled(),
            drops_tracker_categories: default_drops_tracker_categories(),
            total_drops_tracker_categories: default_total_drops_tracker_categories(),
            drops_tracker_counts: HashMap::new(),
            total_drops_tracker_counts: HashMap::new(),
            drops_tracker_recent_items: Vec::new(),
            drops_tracker_prevent_duplicates: default_true(),
            drops_tracker_muling_mode: false,
            drops_tracker_muling_started_at_ms: None,
            drops_tracker_overlay_position_unlocked: false,
            tracker_overlays_separate_window: false,
            drops_tracker_overlay_position: default_drops_tracker_overlay_position(),
            drops_tracker_overlay_width: default_tracker_overlay_width(),
            total_drops_overlay_position: default_total_drops_overlay_position(),
            total_drops_overlay_width: default_tracker_overlay_width(),
            rune_tracker_counts: HashMap::new(),
            rune_tracker_overlay_enabled: false,
            rune_tracker_overlay_runes: default_rune_tracker_overlay_runes(),
            rune_tracker_overlay_position: default_rune_tracker_overlay_position(),
            rune_tracker_overlay_width: default_tracker_overlay_width(),
            holy_grail_found: HashMap::new(),
            fate_card_counts: HashMap::new(),
            fate_card_drop_counts: HashMap::new(),
            holy_grail_overlay_enabled: false,
            holy_grail_overlay_show_total: true,
            holy_grail_overlay_show_latest: true,
            holy_grail_overlay_categories: default_holy_grail_overlay_categories(),
            holy_grail_overlay_position: default_holy_grail_overlay_position(),
            holy_grail_overlay_width: default_tracker_overlay_width(),
            muling_indicator_overlay_position: default_muling_indicator_overlay_position(),
            save_exit_automation_difficulty: default_save_exit_automation_difficulty(),
            save_exit_automation_click_x: default_save_exit_automation_click_x(),
            save_exit_automation_click_y: default_save_exit_automation_click_y(),
            save_exit_automation_coordinate_mode_percent: false,
            save_exit_automation_delay_ms: default_save_exit_automation_delay_ms(),
            save_exit_automation_main_menu_wait_ms: default_save_exit_automation_main_menu_wait_ms(
            ),
            zone_transition_sync_enabled: true,
            gsf_enabled: true,
            gsf_notification_enabled: true,
            gsf_sound_slot: None,
            gsf_sound_volume: 1.0,
            gsf_players: Vec::new(),
            holy_grail_new_item_notification_enabled: true,
            holy_grail_new_item_sound_slot: None,
            holy_grail_new_item_sound_volume: 1.0,
            verbose_filter_logging: false,
            sounds: default_sounds(),
            goblin_alert_slot: None,
            extra: HashMap::new(),
        }
    }
}

impl Default for WindowState {
    fn default() -> Self {
        Self {
            x: 100,
            y: 100,
            width: 1024,
            height: 640,
            maximized: false,
        }
    }
}

fn app_data_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = app
        .path()
        .app_data_dir()
        .map_err(|e| format!("Failed to resolve app data dir: {}", e))?;
    fs::create_dir_all(&dir).map_err(|e| format!("Failed to create app data dir: {}", e))?;
    Ok(dir)
}

fn holy_grail_backup_path(app: &AppHandle) -> Result<PathBuf, String> {
    Ok(app_data_dir(app)?.join(HOLY_GRAIL_BACKUP_FILE))
}

fn holy_grail_snapshot_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = app_data_dir(app)?.join(HOLY_GRAIL_BACKUP_DIR);
    fs::create_dir_all(&dir).map_err(|e| format!("Failed to create grail backup dir: {}", e))?;
    Ok(dir)
}

fn fate_card_backup_path(app: &AppHandle) -> Result<PathBuf, String> {
    Ok(app_data_dir(app)?.join(FATE_CARD_BACKUP_FILE))
}

fn fate_card_snapshot_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = app_data_dir(app)?.join(FATE_CARD_BACKUP_DIR);
    fs::create_dir_all(&dir)
        .map_err(|e| format!("Failed to create Fate Card backup dir: {}", e))?;
    Ok(dir)
}

fn achievement_backup_path(app: &AppHandle) -> Result<PathBuf, String> {
    Ok(app_data_dir(app)?.join(ACHIEVEMENT_BACKUP_FILE))
}

fn achievement_snapshot_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = app_data_dir(app)?.join(ACHIEVEMENT_BACKUP_DIR);
    fs::create_dir_all(&dir)
        .map_err(|e| format!("Failed to create achievement backup dir: {}", e))?;
    Ok(dir)
}

fn now_backup_stamp() -> String {
    let millis = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis())
        .unwrap_or(0);
    millis.to_string()
}

fn backup_exported_at() -> String {
    now_backup_stamp()
}

fn normalize_grail_entry(entry: &HolyGrailFoundEntry) -> Option<HolyGrailFoundEntry> {
    if entry.key.trim().is_empty()
        || entry.name.trim().is_empty()
        || entry.category.trim().is_empty()
    {
        return None;
    }
    Some(HolyGrailFoundEntry {
        key: entry.key.clone(),
        name: entry.name.clone(),
        category: entry.category.clone(),
        first_found_at: if entry.first_found_at.trim().is_empty() {
            backup_exported_at()
        } else {
            entry.first_found_at.clone()
        },
    })
}

fn merge_holy_grail_found(
    mut primary: HashMap<String, HolyGrailFoundEntry>,
    backup: HashMap<String, HolyGrailFoundEntry>,
) -> HashMap<String, HolyGrailFoundEntry> {
    for (key, entry) in backup {
        let Some(entry) = normalize_grail_entry(&entry) else {
            continue;
        };
        match primary.get(&key) {
            Some(existing) => {
                // Keep the earliest first-found timestamp when both sources have the same item.
                if !entry.first_found_at.is_empty()
                    && (existing.first_found_at.is_empty()
                        || entry.first_found_at < existing.first_found_at)
                {
                    primary.insert(key, entry);
                }
            }
            None => {
                primary.insert(key, entry);
            }
        }
    }
    primary
}

fn load_holy_grail_backup_from_disk(app: &AppHandle) -> Result<Option<HolyGrailBackup>, String> {
    let path = holy_grail_backup_path(app)?;
    if !path.exists() {
        return Ok(None);
    }
    let text = fs::read_to_string(&path)
        .map_err(|e| format!("Failed to read Holy Grail backup: {}", e))?;
    let backup: HolyGrailBackup = serde_json::from_str(&text)
        .map_err(|e| format!("Failed to parse Holy Grail backup: {}", e))?;
    Ok(Some(backup))
}

fn write_holy_grail_backup_to_disk(
    app: &AppHandle,
    found: &HashMap<String, HolyGrailFoundEntry>,
    write_snapshot: bool,
) -> Result<HolyGrailBackupStatus, String> {
    let path = holy_grail_backup_path(app)?;
    let backup = HolyGrailBackup {
        version: 1,
        exported_at: backup_exported_at(),
        found: found.clone(),
    };
    let json = serde_json::to_string_pretty(&backup)
        .map_err(|e| format!("Failed to serialize Holy Grail backup: {}", e))?;
    fs::write(&path, &json).map_err(|e| format!("Failed to write Holy Grail backup: {}", e))?;

    if write_snapshot && !found.is_empty() {
        let snapshot_dir = holy_grail_snapshot_dir(app)?;
        let snapshot_path =
            snapshot_dir.join(format!("holy-grail-backup-{}.json", now_backup_stamp()));
        let _ = fs::write(&snapshot_path, &json);

        if let Ok(entries) = fs::read_dir(&snapshot_dir) {
            let mut files: Vec<_> = entries
                .flatten()
                .filter(|entry| {
                    entry.path().extension().and_then(|ext| ext.to_str()) == Some("json")
                })
                .collect();
            files.sort_by_key(|entry| entry.metadata().and_then(|m| m.modified()).ok());
            while files.len() > 20 {
                if let Some(entry) = files.first() {
                    let _ = fs::remove_file(entry.path());
                }
                files.remove(0);
            }
        }
    }

    Ok(HolyGrailBackupStatus {
        backup_exists: true,
        backup_path: path.to_string_lossy().to_string(),
        found_count: found.len(),
        exported_at: Some(backup.exported_at),
    })
}

fn holy_grail_backup_status_inner(app: &AppHandle) -> Result<HolyGrailBackupStatus, String> {
    let path = holy_grail_backup_path(app)?;
    if !path.exists() {
        return Ok(HolyGrailBackupStatus {
            backup_exists: false,
            backup_path: path.to_string_lossy().to_string(),
            found_count: 0,
            exported_at: None,
        });
    }
    let backup = load_holy_grail_backup_from_disk(app)?.unwrap_or_default();
    Ok(HolyGrailBackupStatus {
        backup_exists: true,
        backup_path: path.to_string_lossy().to_string(),
        found_count: backup.found.len(),
        exported_at: Some(backup.exported_at),
    })
}

fn load_fate_card_backup_from_disk(app: &AppHandle) -> Result<Option<FateCardBackup>, String> {
    let path = fate_card_backup_path(app)?;
    if !path.exists() {
        return Ok(None);
    }
    let text =
        fs::read_to_string(&path).map_err(|e| format!("Failed to read Fate Card backup: {}", e))?;
    let backup: FateCardBackup = serde_json::from_str(&text)
        .map_err(|e| format!("Failed to parse Fate Card backup: {}", e))?;
    Ok(Some(backup))
}

fn write_fate_card_backup_to_disk(
    app: &AppHandle,
    counts: &HashMap<String, u32>,
    write_snapshot: bool,
) -> Result<FateCardBackupStatus, String> {
    let path = fate_card_backup_path(app)?;
    let backup = FateCardBackup {
        version: 1,
        exported_at: backup_exported_at(),
        counts: counts.clone(),
    };
    let json = serde_json::to_string_pretty(&backup)
        .map_err(|e| format!("Failed to serialize Fate Card backup: {}", e))?;
    fs::write(&path, &json).map_err(|e| format!("Failed to write Fate Card backup: {}", e))?;

    if write_snapshot && !counts.is_empty() {
        let snapshot_dir = fate_card_snapshot_dir(app)?;
        let snapshot_path = snapshot_dir.join(format!(
            "fate-card-counts-backup-{}.json",
            now_backup_stamp()
        ));
        let _ = fs::write(&snapshot_path, &json);

        if let Ok(entries) = fs::read_dir(&snapshot_dir) {
            let mut files: Vec<_> = entries
                .flatten()
                .filter(|entry| {
                    entry.path().extension().and_then(|ext| ext.to_str()) == Some("json")
                })
                .collect();
            files.sort_by_key(|entry| entry.metadata().and_then(|m| m.modified()).ok());
            while files.len() > 20 {
                if let Some(entry) = files.first() {
                    let _ = fs::remove_file(entry.path());
                }
                files.remove(0);
            }
        }
    }

    Ok(FateCardBackupStatus {
        backup_exists: true,
        backup_path: path.to_string_lossy().to_string(),
        card_count: counts.values().filter(|count| **count > 0).count(),
        exported_at: Some(backup.exported_at),
    })
}

fn fate_card_backup_status_inner(app: &AppHandle) -> Result<FateCardBackupStatus, String> {
    let path = fate_card_backup_path(app)?;
    if !path.exists() {
        return Ok(FateCardBackupStatus {
            backup_exists: false,
            backup_path: path.to_string_lossy().to_string(),
            card_count: 0,
            exported_at: None,
        });
    }
    let backup = load_fate_card_backup_from_disk(app)?.unwrap_or_default();
    Ok(FateCardBackupStatus {
        backup_exists: true,
        backup_path: path.to_string_lossy().to_string(),
        card_count: backup.counts.values().filter(|count| **count > 0).count(),
        exported_at: Some(backup.exported_at),
    })
}

fn load_achievement_backup_from_disk(app: &AppHandle) -> Result<Option<AchievementBackup>, String> {
    let path = achievement_backup_path(app)?;
    if !path.exists() {
        return Ok(None);
    }
    let text = fs::read_to_string(&path)
        .map_err(|e| format!("Failed to read achievements backup: {}", e))?;
    let backup: AchievementBackup = serde_json::from_str(&text)
        .map_err(|e| format!("Failed to parse achievements backup: {}", e))?;
    Ok(Some(backup))
}

fn write_achievement_backup_to_disk(
    app: &AppHandle,
    stats: &serde_json::Value,
    write_snapshot: bool,
) -> Result<AchievementBackupStatus, String> {
    let path = achievement_backup_path(app)?;
    let backup = AchievementBackup {
        version: 1,
        exported_at: backup_exported_at(),
        stats: stats.clone(),
    };
    let json = serde_json::to_string_pretty(&backup)
        .map_err(|e| format!("Failed to serialize achievements backup: {}", e))?;
    fs::write(&path, &json).map_err(|e| format!("Failed to write achievements backup: {}", e))?;

    if write_snapshot {
        let snapshot_dir = achievement_snapshot_dir(app)?;
        let snapshot_path =
            snapshot_dir.join(format!("achievements-backup-{}.json", now_backup_stamp()));
        let _ = fs::write(&snapshot_path, &json);

        if let Ok(entries) = fs::read_dir(&snapshot_dir) {
            let mut files: Vec<_> = entries
                .flatten()
                .filter(|entry| {
                    entry.path().extension().and_then(|ext| ext.to_str()) == Some("json")
                })
                .collect();
            files.sort_by_key(|entry| entry.metadata().and_then(|m| m.modified()).ok());
            while files.len() > 20 {
                if let Some(entry) = files.first() {
                    let _ = fs::remove_file(entry.path());
                }
                files.remove(0);
            }
        }
    }

    Ok(AchievementBackupStatus {
        backup_exists: true,
        backup_path: path.to_string_lossy().to_string(),
        exported_at: Some(backup.exported_at),
    })
}

fn safe_backup_label(label: &str) -> String {
    let mut out = String::new();
    for ch in label.chars() {
        if ch.is_ascii_alphanumeric() {
            out.push(ch.to_ascii_lowercase());
        } else if ch.is_whitespace() || ch == '-' || ch == '_' {
            if !out.ends_with('-') {
                out.push('-');
            }
        }
    }
    out.trim_matches('-').to_string()
}

fn achievement_backup_status_inner(app: &AppHandle) -> Result<AchievementBackupStatus, String> {
    let path = achievement_backup_path(app)?;
    if !path.exists() {
        return Ok(AchievementBackupStatus {
            backup_exists: false,
            backup_path: path.to_string_lossy().to_string(),
            exported_at: None,
        });
    }
    let backup = load_achievement_backup_from_disk(app)?.unwrap_or_default();
    Ok(AchievementBackupStatus {
        backup_exists: true,
        backup_path: path.to_string_lossy().to_string(),
        exported_at: Some(backup.exported_at),
    })
}

#[tauri::command]
pub fn backup_holy_grail_found(
    app: AppHandle,
    found: HashMap<String, HolyGrailFoundEntry>,
) -> Result<HolyGrailBackupStatus, String> {
    write_holy_grail_backup_to_disk(&app, &found, true)
}

#[tauri::command]
pub fn restore_holy_grail_backup(
    app: AppHandle,
) -> Result<HashMap<String, HolyGrailFoundEntry>, String> {
    let backup = load_holy_grail_backup_from_disk(&app)?
        .ok_or_else(|| "No Holy Grail backup file exists yet.".to_string())?;
    Ok(backup.found)
}

#[tauri::command]
pub fn get_holy_grail_backup_status(app: AppHandle) -> Result<HolyGrailBackupStatus, String> {
    holy_grail_backup_status_inner(&app)
}

#[tauri::command]
pub fn open_holy_grail_backup_folder(app: AppHandle) -> Result<(), String> {
    let dir = app_data_dir(&app)?;
    std::process::Command::new("explorer")
        .arg(&dir)
        .spawn()
        .map_err(|e| format!("Failed to open Holy Grail backup folder: {}", e))?;
    Ok(())
}

#[tauri::command]
pub fn backup_fate_card_counts(
    app: AppHandle,
    counts: HashMap<String, u32>,
) -> Result<FateCardBackupStatus, String> {
    write_fate_card_backup_to_disk(&app, &counts, true)
}

#[tauri::command]
pub fn restore_fate_card_counts_backup(app: AppHandle) -> Result<HashMap<String, u32>, String> {
    let backup = load_fate_card_backup_from_disk(&app)?
        .ok_or_else(|| "No Fate Card backup file exists yet.".to_string())?;
    Ok(backup.counts)
}

#[tauri::command]
pub fn get_fate_card_backup_status(app: AppHandle) -> Result<FateCardBackupStatus, String> {
    fate_card_backup_status_inner(&app)
}

#[tauri::command]
pub fn open_fate_card_backup_folder(app: AppHandle) -> Result<(), String> {
    let dir = app_data_dir(&app)?;
    std::process::Command::new("explorer")
        .arg(&dir)
        .spawn()
        .map_err(|e| format!("Failed to open Fate Card backup folder: {}", e))?;
    Ok(())
}

#[tauri::command]
pub fn backup_achievement_stats(
    app: AppHandle,
    stats: serde_json::Value,
) -> Result<AchievementBackupStatus, String> {
    write_achievement_backup_to_disk(&app, &stats, true)
}

#[tauri::command]
pub fn backup_achievement_category(
    app: AppHandle,
    category: String,
    stats: serde_json::Value,
) -> Result<AchievementBackupStatus, String> {
    let label = safe_backup_label(&category);
    if label.is_empty() {
        return Err("Achievement category name is required.".to_string());
    }
    let snapshot_dir = achievement_snapshot_dir(&app)?;
    let path = snapshot_dir.join(format!(
        "achievements-{}-{}.json",
        label,
        now_backup_stamp()
    ));
    let backup = serde_json::json!({
        "version": 1,
        "exportedAt": backup_exported_at(),
        "category": category,
        "stats": stats,
    });
    let json = serde_json::to_string_pretty(&backup)
        .map_err(|e| format!("Failed to serialize achievement category backup: {}", e))?;
    fs::write(&path, json)
        .map_err(|e| format!("Failed to write achievement category backup: {}", e))?;
    Ok(AchievementBackupStatus {
        backup_exists: true,
        backup_path: path.to_string_lossy().to_string(),
        exported_at: backup
            .get("exportedAt")
            .and_then(|value| value.as_str())
            .map(|value| value.to_string()),
    })
}

#[tauri::command]
pub fn restore_achievement_backup(app: AppHandle) -> Result<serde_json::Value, String> {
    let backup = load_achievement_backup_from_disk(&app)?
        .ok_or_else(|| "No achievements backup file exists yet.".to_string())?;
    Ok(backup.stats)
}

#[tauri::command]
pub fn get_achievement_backup_status(app: AppHandle) -> Result<AchievementBackupStatus, String> {
    achievement_backup_status_inner(&app)
}

#[tauri::command]
pub fn open_achievement_backup_folder(app: AppHandle) -> Result<(), String> {
    let dir = app_data_dir(&app)?;
    std::process::Command::new("explorer")
        .arg(&dir)
        .spawn()
        .map_err(|e| format!("Failed to open achievements backup folder: {}", e))?;
    Ok(())
}

/// Load application settings from the store
#[tauri::command]
pub fn load_settings(app: AppHandle) -> Result<AppSettings, String> {
    let store = app
        .store(SETTINGS_FILE)
        .map_err(|e| format!("Failed to open settings store: {}", e))?;

    // Try to get settings from store, use defaults if not found
    let settings: AppSettings = match store.get("settings") {
        Some(value) => {
            let migrated = migrate_settings_value(value.clone());
            serde_json::from_value(migrated).unwrap_or_else(|e| {
                log_error(&format!("Failed to parse settings, using defaults: {}", e));
                AppSettings::default()
            })
        }
        None => {
            log_info("No settings found, using defaults");
            AppSettings::default()
        }
    };

    Ok(settings)
}

fn migrate_settings_value(mut value: serde_json::Value) -> serde_json::Value {
    let Some(settings) = value.as_object_mut() else {
        return value;
    };

    let fate_card_tracking_migrated = settings
        .get("fateCardDropsTrackerDefaultMigrated")
        .and_then(|raw| raw.as_bool())
        .unwrap_or(false);

    if !fate_card_tracking_migrated {
        if let Some(categories) = settings
            .get_mut("dropsTrackerCategories")
            .and_then(|raw| raw.as_object_mut())
        {
            categories.insert("fateCard".to_string(), serde_json::Value::Bool(true));
        }
        settings.insert(
            "fateCardDropsTrackerDefaultMigrated".to_string(),
            serde_json::Value::Bool(true),
        );
    }

    let fate_card_stash_counts_paused = settings
        .get("fateCardStashCounterMapPaused")
        .and_then(|raw| raw.as_bool())
        .unwrap_or(false);

    if !fate_card_stash_counts_paused {
        settings.insert("fateCardCounts".to_string(), serde_json::json!({}));
        if let Some(found) = settings
            .get_mut("holyGrailFound")
            .and_then(|raw| raw.as_object_mut())
        {
            found.retain(|_, entry| {
                entry.get("category").and_then(|raw| raw.as_str()) != Some("fateCards")
            });
        }
        settings.insert(
            "fateCardStashCounterMapPaused".to_string(),
            serde_json::Value::Bool(true),
        );
    }

    let fate_card_live_drop_counts_separated = settings
        .get("fateCardLiveDropCountsSeparated")
        .and_then(|raw| raw.as_bool())
        .unwrap_or(false);

    if !fate_card_live_drop_counts_separated {
        settings.insert("fateCardCounts".to_string(), serde_json::json!({}));
        if let Some(found) = settings
            .get_mut("holyGrailFound")
            .and_then(|raw| raw.as_object_mut())
        {
            found.retain(|_, entry| {
                entry.get("category").and_then(|raw| raw.as_str()) != Some("fateCards")
            });
        }
        settings.insert(
            "fateCardLiveDropCountsSeparated".to_string(),
            serde_json::Value::Bool(true),
        );
    }

    let zone_transition_sync_defaulted = settings
        .get("zoneTransitionSyncDefaultedOn")
        .and_then(|raw| raw.as_bool())
        .unwrap_or(false);

    if !zone_transition_sync_defaulted {
        if !settings
            .get("zoneTransitionSyncEnabled")
            .and_then(|raw| raw.as_bool())
            .unwrap_or(true)
        {
            settings.insert(
                "zoneTransitionSyncEnabled".to_string(),
                serde_json::Value::Bool(true),
            );
        }
        settings.insert(
            "zoneTransitionSyncDefaultedOn".to_string(),
            serde_json::Value::Bool(true),
        );
    }

    value
}

fn merge_json_patch(base: &mut serde_json::Value, patch: serde_json::Value) {
    let (Some(base_obj), Some(patch_obj)) = (base.as_object_mut(), patch.as_object()) else {
        return;
    };

    for (key, value) in patch_obj {
        if key == "holyGrailFound" {
            merge_holy_grail_found_json(base_obj, value);
            continue;
        }
        base_obj.insert(key.clone(), value.clone());
    }
}

fn merge_holy_grail_found_json(
    base_obj: &mut serde_json::Map<String, serde_json::Value>,
    patch_value: &serde_json::Value,
) {
    let Some(patch_found) = patch_value.as_object() else {
        base_obj.insert("holyGrailFound".to_string(), patch_value.clone());
        return;
    };

    let base_value = base_obj
        .entry("holyGrailFound".to_string())
        .or_insert_with(|| serde_json::json!({}));
    let Some(base_found) = base_value.as_object_mut() else {
        *base_value = patch_value.clone();
        return;
    };

    for (entry_key, patch_entry) in patch_found {
        match base_found.get(entry_key) {
            Some(existing) if should_keep_existing_grail_entry(existing, patch_entry) => {}
            _ => {
                base_found.insert(entry_key.clone(), patch_entry.clone());
            }
        }
    }
}

fn should_keep_existing_grail_entry(
    existing: &serde_json::Value,
    incoming: &serde_json::Value,
) -> bool {
    let existing_time = existing
        .get("firstFoundAt")
        .and_then(|v| v.as_str())
        .unwrap_or("");
    let incoming_time = incoming
        .get("firstFoundAt")
        .and_then(|v| v.as_str())
        .unwrap_or("");

    !existing_time.is_empty() && (incoming_time.is_empty() || existing_time <= incoming_time)
}

fn should_write_routine_backups_now() -> bool {
    let now = Instant::now();
    let Ok(mut last_write) = LAST_ROUTINE_BACKUP_WRITE.lock() else {
        return false;
    };
    let Some(last) = *last_write else {
        *last_write = Some(now);
        return false;
    };
    let due = now.duration_since(last) >= ROUTINE_BACKUP_MIN_INTERVAL;
    if due {
        *last_write = Some(now);
    }
    due
}

fn persist_settings_with_options(
    app: &AppHandle,
    settings: &AppSettings,
    write_routine_backups: bool,
) -> Result<(), String> {
    let store = app
        .store(SETTINGS_FILE)
        .map_err(|e| format!("Failed to open settings store: {}", e))?;

    let value = serde_json::to_value(settings)
        .map_err(|e| format!("Failed to serialize settings: {}", e))?;

    store.set("settings", value);

    store
        .save()
        .map_err(|e| format!("Failed to save settings to disk: {}", e))?;

    let write_backup_mirrors = write_routine_backups && should_write_routine_backups_now();

    // Never let routine saves of an empty/reset grail overwrite the safety backup.
    // Manual backups can still write the current state explicitly.
    if write_backup_mirrors && !settings.holy_grail_found.is_empty() {
        if let Err(e) = write_holy_grail_backup_to_disk(app, &settings.holy_grail_found, false) {
            log_error(&format!("Failed to auto-backup Holy Grail data: {}", e));
        }
    }

    if write_backup_mirrors {
        if let Err(e) = write_achievement_backup_to_disk(app, &settings.achievement_stats, false) {
            log_error(&format!("Failed to auto-backup achievement data: {}", e));
        }
    }

    if let Err(e) = app.emit("settings-updated", settings) {
        log_error(&format!("Failed to emit settings-updated: {}", e));
    }

    Ok(())
}

fn persist_settings(app: &AppHandle, settings: &AppSettings) -> Result<(), String> {
    persist_settings_with_options(app, settings, true)
}

fn is_timer_only_settings_patch(patch: &serde_json::Value) -> bool {
    let Some(object) = patch.as_object() else {
        return false;
    };
    !object.is_empty()
        && object.keys().all(|key| {
            matches!(
                key.as_str(),
                "dropsTrackerRunElapsedMs"
                    | "dropsTrackerSessionElapsedMs"
                    | "dropsTrackerTimerLastTickAtMs"
            )
        })
}

/// Save application settings to the store
#[tauri::command]
pub fn save_settings(app: AppHandle, settings: AppSettings) -> Result<(), String> {
    let _guard = SETTINGS_SAVE_LOCK
        .lock()
        .map_err(|_| "Settings save lock poisoned".to_string())?;
    persist_settings(&app, &settings)?;
    Ok(())
}

/// Atomically merge a partial frontend settings patch into the latest settings.
///
/// Each webview owns a separate JS settings store. Saving whole snapshots from
/// both windows can race and overwrite unrelated tracker/grail changes. This
/// command serializes saves in Rust and applies only the keys that changed.
#[tauri::command]
pub fn save_settings_patch(
    app: AppHandle,
    patch: serde_json::Value,
) -> Result<AppSettings, String> {
    let _guard = SETTINGS_SAVE_LOCK
        .lock()
        .map_err(|_| "Settings save lock poisoned".to_string())?;
    let timer_only_patch = is_timer_only_settings_patch(&patch);
    let current = load_settings(app.clone())?;
    let mut value = serde_json::to_value(current)
        .map_err(|e| format!("Failed to serialize current settings: {}", e))?;
    merge_json_patch(&mut value, patch);
    let settings: AppSettings = serde_json::from_value(value)
        .map_err(|e| format!("Failed to merge settings patch: {}", e))?;
    persist_settings_with_options(&app, &settings, !timer_only_patch)?;
    Ok(settings)
}

/// Load window state from the store
#[tauri::command]
pub fn get_window_state(
    app: AppHandle,
    window_label: String,
) -> Result<Option<WindowState>, String> {
    log_info(&format!("Loading window state for: {}", window_label));

    let store = app
        .store(SETTINGS_FILE)
        .map_err(|e| format!("Failed to open settings store: {}", e))?;

    let key = format!("window_{}", window_label);

    let state: Option<WindowState> = match store.get(&key) {
        Some(value) => serde_json::from_value(value.clone()).ok(),
        None => None,
    };

    Ok(state)
}

/// Save window state to the store
#[tauri::command]
pub fn save_window_state(
    app: AppHandle,
    window_label: String,
    state: WindowState,
) -> Result<(), String> {
    log_info(&format!(
        "Saving window state for {}: {}x{} at ({}, {})",
        window_label, state.width, state.height, state.x, state.y
    ));

    let store = app
        .store(SETTINGS_FILE)
        .map_err(|e| format!("Failed to open settings store: {}", e))?;

    let key = format!("window_{}", window_label);
    let value = serde_json::to_value(&state)
        .map_err(|e| format!("Failed to serialize window state: {}", e))?;

    store.set(key, value);

    store
        .save()
        .map_err(|e| format!("Failed to save window state to disk: {}", e))?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn grail_entry(
        key: &str,
        name: &str,
        category: &str,
        first_found_at: &str,
    ) -> HolyGrailFoundEntry {
        HolyGrailFoundEntry {
            key: key.into(),
            name: name.into(),
            category: category.into(),
            first_found_at: first_found_at.into(),
        }
    }

    #[test]
    fn legacy_settings_without_sounds_field_seeds_seven_defaults() {
        // settings.json that predates the Sounds tab — no `sounds` field.
        let json = r#"{
            "theme": "dark",
            "soundVolume": 0.5,
            "activeProfile": null,
            "notificationDuration": 5000,
            "notificationStackDirection": "up",
            "notificationFontSize": 14,
            "notificationOpacity": 0.9,
            "notificationX": 1.0,
            "notificationY": 1.0,
            "compactName": false,
            "toggleWindowHotkey": {"keyCode": 0, "modifiers": 0, "display": "None"},
            "editOverlayHotkey": {"keyCode": 0, "modifiers": 3, "display": "Ctrl+Alt"},
            "revealHiddenHotkey": {"keyCode": 90, "modifiers": 0, "display": "Z"},
            "lootHistoryHotkey": {"keyCode": 78, "modifiers": 0, "display": "N"},
            "verboseFilterLogging": false
        }"#;
        let settings: AppSettings = serde_json::from_str(json).expect("valid legacy json");
        assert_eq!(settings.sounds.len(), 7);
        for (i, slot) in settings.sounds.iter().enumerate() {
            assert_eq!(slot.label, format!("Sound {}", i + 1));
            assert_eq!(slot.volume, 0.8);
            assert!(matches!(slot.source, SoundSource::Default));
        }
        assert_eq!(settings.sound_volume, 0.5);
    }

    #[test]
    fn sound_source_round_trips_each_variant() {
        let slots = vec![
            SoundSlot {
                label: "Default".into(),
                volume: 0.8,
                source: SoundSource::Default,
            },
            SoundSlot {
                label: "Custom".into(),
                volume: 0.5,
                source: SoundSource::Custom {
                    file_name: "slot-8.mp3".into(),
                },
            },
            SoundSlot {
                label: "Empty".into(),
                volume: 0.0,
                source: SoundSource::Empty,
            },
        ];
        let json = serde_json::to_string(&slots).unwrap();
        let back: Vec<SoundSlot> = serde_json::from_str(&json).unwrap();
        assert_eq!(back.len(), 3);
        assert!(matches!(back[0].source, SoundSource::Default));
        match &back[1].source {
            SoundSource::Custom { file_name } => assert_eq!(file_name, "slot-8.mp3"),
            other => panic!("expected Custom, got {:?}", other),
        }
        assert!(matches!(back[2].source, SoundSource::Empty));
    }

    #[test]
    fn sound_source_custom_uses_camel_case_on_wire() {
        let slot = SoundSlot {
            label: "Custom".into(),
            volume: 0.5,
            source: SoundSource::Custom {
                file_name: "slot-8.mp3".into(),
            },
        };
        let json = serde_json::to_string(&slot).unwrap();
        // The wire format MUST use camelCase `fileName`, otherwise the JS
        // frontend (which sends `fileName`) cannot round-trip through Tauri.
        assert!(
            json.contains("\"fileName\":\"slot-8.mp3\""),
            "expected camelCase fileName on the wire, got {}",
            json
        );
        assert!(
            !json.contains("file_name"),
            "snake_case file_name should not be on the wire, got {}",
            json
        );
    }

    #[test]
    fn sound_source_deserialises_camel_case_payload_from_frontend() {
        // Exact JSON shape that `SoundsTab.svelte` sends through Tauri's
        // `save_settings` command. If this fails, the Tauri command rejects
        // the args before the Rust handler runs, and the slot's Custom state
        // never makes it to disk.
        let json = r#"{
            "label": "Custom",
            "volume": 0.5,
            "source": { "kind": "custom", "fileName": "slot-8.mp3" }
        }"#;
        let slot: SoundSlot =
            serde_json::from_str(json).expect("frontend payload must deserialise");
        match slot.source {
            SoundSource::Custom { file_name } => assert_eq!(file_name, "slot-8.mp3"),
            other => panic!("expected Custom, got {:?}", other),
        }
    }

    #[test]
    fn holy_grail_merge_restores_backup_only_entries() {
        let mut primary = HashMap::new();
        primary.insert(
            "su:deadfall".into(),
            grail_entry("su:deadfall", "Deadfall", "su", "2026-05-01T10:00:00.000Z"),
        );

        let mut backup = HashMap::new();
        backup.insert(
            "sets:adirahs-tunic".into(),
            grail_entry(
                "sets:adirahs-tunic",
                "Adirah's Tunic",
                "sets",
                "2026-05-02T10:00:00.000Z",
            ),
        );

        let merged = merge_holy_grail_found(primary, backup);

        assert_eq!(merged.len(), 2);
        assert_eq!(merged["su:deadfall"].name, "Deadfall");
        assert_eq!(merged["sets:adirahs-tunic"].name, "Adirah's Tunic");
    }

    #[test]
    fn holy_grail_merge_keeps_earliest_duplicate_first_found_time() {
        let mut primary = HashMap::new();
        primary.insert(
            "su:deadfall".into(),
            grail_entry("su:deadfall", "Deadfall", "su", "2026-05-02T10:00:00.000Z"),
        );

        let mut backup = HashMap::new();
        backup.insert(
            "su:deadfall".into(),
            grail_entry("su:deadfall", "Deadfall", "su", "2026-05-01T10:00:00.000Z"),
        );

        let merged = merge_holy_grail_found(primary, backup);

        assert_eq!(merged.len(), 1);
        assert_eq!(
            merged["su:deadfall"].first_found_at,
            "2026-05-01T10:00:00.000Z",
        );
    }

    #[test]
    fn holy_grail_merge_ignores_invalid_backup_entries() {
        let mut backup = HashMap::new();
        backup.insert(
            "missing-name".into(),
            grail_entry("sets:missing-name", " ", "sets", "2026-05-01T10:00:00.000Z"),
        );
        backup.insert(
            "missing-category".into(),
            grail_entry(
                "sets:missing-category",
                "Adirah's Tunic",
                "",
                "2026-05-01T10:00:00.000Z",
            ),
        );

        let merged = merge_holy_grail_found(HashMap::new(), backup);

        assert!(merged.is_empty());
    }

    #[test]
    fn settings_patch_merges_holy_grail_found_entries() {
        let mut base = serde_json::json!({
            "holyGrailFound": {
                "su:deadfall": {
                    "key": "su:deadfall",
                    "name": "Deadfall",
                    "category": "su",
                    "firstFoundAt": "2026-05-01T10:00:00.000Z"
                }
            },
            "theme": "dark"
        });
        let patch = serde_json::json!({
            "holyGrailFound": {
                "sets:adirahs-tunic": {
                    "key": "sets:adirahs-tunic",
                    "name": "Adirah's Tunic",
                    "category": "sets",
                    "firstFoundAt": "2026-05-02T10:00:00.000Z"
                }
            }
        });

        merge_json_patch(&mut base, patch);

        let found = base["holyGrailFound"].as_object().unwrap();
        assert!(found.contains_key("su:deadfall"));
        assert!(found.contains_key("sets:adirahs-tunic"));
    }

    #[test]
    fn settings_patch_keeps_earliest_duplicate_holy_grail_timestamp() {
        let mut base = serde_json::json!({
            "holyGrailFound": {
                "su:deadfall": {
                    "key": "su:deadfall",
                    "name": "Deadfall",
                    "category": "su",
                    "firstFoundAt": "2026-05-01T10:00:00.000Z"
                }
            }
        });
        let patch = serde_json::json!({
            "holyGrailFound": {
                "su:deadfall": {
                    "key": "su:deadfall",
                    "name": "Deadfall",
                    "category": "su",
                    "firstFoundAt": "2026-05-02T10:00:00.000Z"
                }
            }
        });

        merge_json_patch(&mut base, patch);

        assert_eq!(
            base["holyGrailFound"]["su:deadfall"]["firstFoundAt"],
            "2026-05-01T10:00:00.000Z",
        );
    }
}
