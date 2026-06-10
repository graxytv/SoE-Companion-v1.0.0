#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod character_levels;
mod d2types;
mod drop_hook;
mod grail_log;
mod hook_log;
mod hotkeys;
mod injection;
mod items_cache;
mod kill_counter;
mod logger;
mod loot_filter_hook;
mod loot_history;
mod map_marker;
mod marker_scanner;
mod notifier;
mod offsets;
mod process;
mod rules;
mod runeword_planner;
mod save_exit_automation;
mod scanner_state;
mod settings;
mod soe_filter;
mod sounds;
mod updater;

use std::sync::atomic::{AtomicBool, AtomicU64, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, RwLock};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use tauri::{AppHandle, Emitter, Manager, PhysicalPosition, PhysicalSize, WindowEvent};

use crate::hotkeys::{EditModeState, HotkeyState, MulingModeHotkeyState};
use crate::logger::{error as log_error, info as log_info};
use crate::loot_history::{LootEntry, LootHistory, PickupState};

use notifier::{DropScanner, ItemDropEvent, ItemsDictionary};

// Windows-only imports for process / overlay / privileges
#[cfg(target_os = "windows")]
use std::ffi::OsStr;
#[cfg(target_os = "windows")]
use std::os::windows::ffi::OsStrExt;
#[cfg(target_os = "windows")]
use windows::core::PCWSTR;
#[cfg(target_os = "windows")]
use windows::Win32::Foundation::{CloseHandle, BOOL, COLORREF, HANDLE, HWND, RECT, WAIT_TIMEOUT};
#[cfg(target_os = "windows")]
use windows::Win32::Graphics::Dwm::{DwmSetWindowAttribute, DWMWA_BORDER_COLOR};
#[cfg(target_os = "windows")]
use windows::Win32::Security::{
    AdjustTokenPrivileges, GetTokenInformation, LookupPrivilegeValueW, TokenElevationType,
    TokenLinkedToken, LUID_AND_ATTRIBUTES, SE_DEBUG_NAME, SE_PRIVILEGE_ENABLED,
    TOKEN_ADJUST_PRIVILEGES, TOKEN_ELEVATION_TYPE, TOKEN_LINKED_TOKEN, TOKEN_PRIVILEGES,
    TOKEN_QUERY,
};
#[cfg(target_os = "windows")]
use windows::Win32::System::Com::CoTaskMemFree;
#[cfg(target_os = "windows")]
use windows::Win32::System::Threading::{
    GetCurrentProcess, OpenProcess, OpenProcessToken, WaitForSingleObject,
    PROCESS_ACCESS_RIGHTS, PROCESS_QUERY_LIMITED_INFORMATION,
};
#[cfg(target_os = "windows")]
use windows::Win32::UI::Shell::{FOLDERID_LocalAppData, SHGetKnownFolderPath, KF_FLAG_DEFAULT};
#[cfg(target_os = "windows")]
use windows::Win32::UI::WindowsAndMessaging::{
    FindWindowW, GetForegroundWindow, GetWindowLongW, GetWindowRect, MoveWindow,
    SetLayeredWindowAttributes, SetWindowLongW, SetWindowPos, ShowWindow, GWL_EXSTYLE, GWL_STYLE,
    HWND_TOPMOST, LWA_ALPHA, SWP_FRAMECHANGED, SWP_NOACTIVATE, SWP_NOMOVE, SWP_NOSIZE,
    SWP_NOZORDER, SW_HIDE, SW_SHOWNA, WS_BORDER, WS_CAPTION, WS_DLGFRAME, WS_EX_LAYERED,
    WS_EX_NOACTIVATE, WS_EX_TOOLWINDOW, WS_EX_TRANSPARENT, WS_MAXIMIZEBOX, WS_MINIMIZEBOX,
    WS_POPUP, WS_SYSMENU, WS_THICKFRAME, GetWindowThreadProcessId, IsHungAppWindow,
};

/// Shared state for controlling the scanner
struct AppState {
    is_scanning: Arc<AtomicBool>,
    should_auto_scan: Arc<AtomicBool>,
    /// Filter configuration shared with scanner thread
    filter_config: Arc<RwLock<Option<rules::FilterConfig>>>,
    /// Whether filtering is enabled
    filter_enabled: Arc<AtomicBool>,
    /// When true, scanner logs per-item filter decisions (noisy; opt-in for debugging).
    verbose_filter_logging: Arc<AtomicBool>,
    /// Optional low-impact sync trigger. It reads only tiny gameplay-state
    /// pointers and asks the frontend to run the normal quiet sync once after
    /// a transition settles.
    zone_transition_sync_enabled: Arc<AtomicBool>,
    /// Driven by the reveal-hidden hotkey watcher; mirrored into the hook.
    reveal_hidden_active: Arc<AtomicBool>,
    filter_config_generation: Arc<AtomicU64>,
    // Joined on shutdown so DropScanner::drop → loot_hook.eject runs before exit.
    scanner_thread: Arc<Mutex<Option<JoinHandle<()>>>>,
    game_status: Arc<AtomicU8>,
    items_dictionary: Arc<RwLock<Option<ItemsDictionary>>>,
    /// Session loot history shared with scanner thread.
    loot_history: Arc<RwLock<LootHistory>>,
    /// Quiet in-memory drop queue. The silent collector fills this while
    /// playing; Save & Exit, zone-change sync, or Sync All drains it in one
    /// frontend batch.
    silent_drop_queue: Arc<Mutex<Vec<ItemDropEvent>>>,
    grail_log_watcher: grail_log::GrailLogWatcher,
    account_stats_watcher: kill_counter::AccountStatsWatcher,
}

const GAME_STATUS_UNKNOWN: u8 = 0;
const GAME_STATUS_INGAME: u8 = 1;
const GAME_STATUS_MENU: u8 = 2;
const SILENT_DROP_QUEUE_LIMIT: usize = 2_000;
const SILENT_COLLECTOR_TICK_MS: u64 = 60;
const SILENT_COLLECTOR_ATTACH_RETRY_MS: u64 = 2_000;
#[cfg(target_os = "windows")]
const PROCESS_SYNCHRONIZE: PROCESS_ACCESS_RIGHTS = PROCESS_ACCESS_RIGHTS(0x0010_0000);

#[cfg(target_os = "windows")]
fn is_diablo2_window_alive(hwnd: HWND) -> bool {
    if hwnd.0.is_null() {
        return false;
    }
    if unsafe { IsHungAppWindow(hwnd).as_bool() } {
        return false;
    }

    let mut pid = 0u32;
    unsafe {
        GetWindowThreadProcessId(hwnd, Some(&mut pid as *mut u32));
    }
    if pid == 0 {
        return false;
    }

    let handle = unsafe {
        OpenProcess(
            PROCESS_SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
            false,
            pid,
        )
    };
    let Ok(handle) = handle else {
        return false;
    };
    if handle.is_invalid() {
        return false;
    }

    let wait = unsafe { WaitForSingleObject(handle, 0) };
    unsafe {
        let _ = CloseHandle(handle);
    }
    wait == WAIT_TIMEOUT
}

/// Check if Diablo II window exists
#[cfg(target_os = "windows")]
fn is_diablo2_running() -> bool {
    let class_wide: Vec<u16> = OsStr::new("Diablo II")
        .encode_wide()
        .chain(Some(0))
        .collect();

    let hwnd = unsafe { FindWindowW(PCWSTR(class_wide.as_ptr()), PCWSTR::null()) };
    let Ok(hwnd) = hwnd else {
        return false;
    };
    if hwnd.0.is_null() {
        return false;
    }
    is_diablo2_window_alive(hwnd)
}

#[cfg(not(target_os = "windows"))]
fn is_diablo2_running() -> bool {
    false
}

#[cfg(target_os = "windows")]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct ZoneSignature {
    player_unit: u32,
    automap_layer: u32,
}

#[cfg(target_os = "windows")]
fn read_zone_signature(
    ctx: &crate::process::D2Context,
) -> Result<Option<ZoneSignature>, String> {
    let player_unit = ctx
        .process
        .read_memory::<u32>(ctx.d2_client + crate::offsets::d2client::PLAYER_UNIT)?;
    let automap_layer = ctx
        .process
        .read_memory::<u32>(ctx.d2_client + crate::offsets::d2client::AUTOMAP_LAYER)?;

    if player_unit == 0 || automap_layer == 0 {
        return Ok(None);
    }

    Ok(Some(ZoneSignature {
        player_unit,
        automap_layer,
    }))
}

#[cfg(target_os = "windows")]
#[derive(Clone, serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct ZoneTransitionPayload<'a> {
    reason: &'a str,
    player_unit: u32,
    automap_layer: u32,
}

#[cfg(target_os = "windows")]
fn emit_zone_transition_sync(
    app_handle: &AppHandle,
    reason: &'static str,
    signature: ZoneSignature,
    last_emit: &mut Option<Instant>,
    cooldown: Duration,
) {
    let now = Instant::now();
    if last_emit
        .map(|last| now.duration_since(last) < cooldown)
        .unwrap_or(false)
    {
        return;
    }

    *last_emit = Some(now);
    let payload = ZoneTransitionPayload {
        reason,
        player_unit: signature.player_unit,
        automap_layer: signature.automap_layer,
    };
    if let Err(e) = app_handle.emit("zone-transition-sync-requested", payload) {
        log_error(&format!(
            "Failed to emit zone-transition-sync-requested: {}",
            e
        ));
    }
}

#[cfg(target_os = "windows")]
fn spawn_zone_transition_watcher(
    should_run: Arc<AtomicBool>,
    enabled: Arc<AtomicBool>,
    app_handle: AppHandle,
) {
    thread::Builder::new()
        .name("zone-transition-watcher".into())
        .spawn(move || {
            const POLL_INTERVAL: Duration = Duration::from_millis(750);
            const STABLE_CONFIRM: Duration = Duration::from_millis(1500);
            const EMIT_COOLDOWN: Duration = Duration::from_secs(15);

            let mut ctx: Option<crate::process::D2Context> = None;
            let mut last_stable: Option<ZoneSignature> = None;
            let mut transition_pending = false;
            let mut candidate_changed: Option<(ZoneSignature, Instant)> = None;
            let mut last_emit: Option<Instant> = None;

            while should_run.load(Ordering::SeqCst) {
                thread::sleep(POLL_INTERVAL);

                if !enabled.load(Ordering::SeqCst) {
                    ctx = None;
                    last_stable = None;
                    transition_pending = false;
                    candidate_changed = None;
                    continue;
                }

                if !is_diablo2_running() {
                    ctx = None;
                    last_stable = None;
                    transition_pending = false;
                    candidate_changed = None;
                    continue;
                }

                if ctx.is_none() {
                    match crate::process::D2Context::new() {
                        Ok(next) => ctx = Some(next),
                        Err(_) => continue,
                    }
                }

                let signature = match ctx.as_ref().map(read_zone_signature) {
                    Some(Ok(value)) => value,
                    Some(Err(_)) => {
                        ctx = None;
                        last_stable = None;
                        transition_pending = false;
                        candidate_changed = None;
                        continue;
                    }
                    None => continue,
                };

                let now = Instant::now();
                match signature {
                    None => {
                        if last_stable.is_some() {
                            transition_pending = true;
                        }
                        candidate_changed = None;
                    }
                    Some(current) => {
                        let Some(previous) = last_stable else {
                            last_stable = Some(current);
                            transition_pending = false;
                            candidate_changed = None;
                            continue;
                        };

                        if transition_pending {
                            emit_zone_transition_sync(
                                &app_handle,
                                "loading-stable",
                                current,
                                &mut last_emit,
                                EMIT_COOLDOWN,
                            );
                            last_stable = Some(current);
                            transition_pending = false;
                            candidate_changed = None;
                            continue;
                        }

                        if current == previous {
                            candidate_changed = None;
                            continue;
                        }

                        match candidate_changed {
                            Some((candidate, since))
                                if candidate == current
                                    && now.duration_since(since) >= STABLE_CONFIRM =>
                            {
                                emit_zone_transition_sync(
                                    &app_handle,
                                    "area-signature-changed",
                                    current,
                                    &mut last_emit,
                                    EMIT_COOLDOWN,
                                );
                                last_stable = Some(current);
                                candidate_changed = None;
                            }
                            Some((candidate, _)) if candidate == current => {}
                            _ => {
                                candidate_changed = Some((current, now));
                            }
                        }
                    }
                }
            }
        })
        .map_err(|e| {
            log_error(&format!("Failed to spawn zone-transition watcher: {}", e));
            e
        })
        .ok();
}

#[cfg(not(target_os = "windows"))]
fn spawn_zone_transition_watcher(
    _should_run: Arc<AtomicBool>,
    _enabled: Arc<AtomicBool>,
    _app_handle: AppHandle,
) {
}

/// Spawn the marker-scanner thread. Cancellation is checked at the top of
/// each iteration, so a `stop`-then-join may block until the in-flight BFS
/// finishes (~700 ms worst case in release).
#[cfg(target_os = "windows")]
fn spawn_marker_thread(
    state: Arc<crate::scanner_state::SharedScannerState>,
) -> Option<thread::JoinHandle<()>> {
    thread::Builder::new()
        .name("marker-scanner".into())
        .spawn(move || {
            let mut marker_scanner = crate::marker_scanner::MarkerScanner::new(state.clone());
            loop {
                if state.stop.load(Ordering::Relaxed) {
                    break;
                }
                if state.clear_markers.swap(false, Ordering::Relaxed) {
                    marker_scanner.clear();
                }
                marker_scanner.tick();
                thread::sleep(Duration::from_millis(30));
            }
            marker_scanner.shutdown();
        })
        .map_err(|e| {
            log_error(&format!("Failed to spawn marker-scanner thread: {}", e));
            e
        })
        .ok()
}

fn push_silent_drop_events(
    queue: &Arc<Mutex<Vec<ItemDropEvent>>>,
    events: Vec<ItemDropEvent>,
) {
    let fresh_events: Vec<ItemDropEvent> = events.into_iter().collect();
    if fresh_events.is_empty() {
        return;
    }

    let Ok(mut guard) = queue.lock() else {
        log_error("Failed to acquire silent drop queue lock");
        return;
    };

    let added = fresh_events.len();
    guard.extend(fresh_events);
    if guard.len() > SILENT_DROP_QUEUE_LIMIT {
        let excess = guard.len() - SILENT_DROP_QUEUE_LIMIT;
        guard.drain(0..excess);
    }
    log_info(&format!(
        "Silent drop collector queued {} drop(s); queue size={}",
        added,
        guard.len()
    ));
}

fn spawn_silent_drop_collector(
    should_run: Arc<AtomicBool>,
    is_scanning: Arc<AtomicBool>,
    verbose_filter_logging: Arc<AtomicBool>,
    game_status: Arc<AtomicU8>,
    items_dictionary: Arc<RwLock<Option<ItemsDictionary>>>,
    loot_history: Arc<RwLock<LootHistory>>,
    silent_drop_queue: Arc<Mutex<Vec<ItemDropEvent>>>,
    scanner_thread: Arc<Mutex<Option<JoinHandle<()>>>>,
    app_handle: AppHandle,
) {
    let handle = thread::Builder::new()
        .name("silent-drop-collector".into())
        .spawn(move || {
            while should_run.load(Ordering::SeqCst) {
                if !is_diablo2_running() {
                    game_status.store(GAME_STATUS_UNKNOWN, Ordering::SeqCst);
                    thread::sleep(Duration::from_millis(SILENT_COLLECTOR_ATTACH_RETRY_MS));
                    continue;
                }

                is_scanning.store(true, Ordering::SeqCst);
                if let Err(e) = app_handle.emit("scanner-status", "starting") {
                    log_error(&format!("Failed to emit scanner-status starting: {}", e));
                }

                #[cfg(target_os = "windows")]
                let mut scanner = {
                    let ctx = match crate::process::D2Context::new() {
                        Ok(ctx) => ctx,
                        Err(e) => {
                            log_error(&format!("Silent collector failed to attach: {}", e));
                            is_scanning.store(false, Ordering::SeqCst);
                            if let Err(e) = app_handle.emit("scanner-status", "error") {
                                log_error(&format!("Failed to emit scanner-status error: {}", e));
                            }
                            thread::sleep(Duration::from_millis(SILENT_COLLECTOR_ATTACH_RETRY_MS));
                            continue;
                        }
                    };
                    let injector = match crate::injection::D2Injector::new(
                        &ctx.process,
                        ctx.d2_client,
                        ctx.d2_common,
                        ctx.d2_lang,
                    ) {
                        Ok(injector) => injector,
                        Err(e) => {
                            log_error(&format!("Silent collector injector setup failed: {}", e));
                            is_scanning.store(false, Ordering::SeqCst);
                            if let Err(e) = app_handle.emit("scanner-status", "error") {
                                log_error(&format!("Failed to emit scanner-status error: {}", e));
                            }
                            thread::sleep(Duration::from_millis(SILENT_COLLECTOR_ATTACH_RETRY_MS));
                            continue;
                        }
                    };
                    let shared_state =
                        Arc::new(crate::scanner_state::SharedScannerState::new(ctx, injector));
                    match DropScanner::new(shared_state, loot_history.clone()) {
                        Ok(scanner) => scanner,
                        Err(e) => {
                            log_error(&format!("Silent collector scanner setup failed: {}", e));
                            is_scanning.store(false, Ordering::SeqCst);
                            if let Err(e) = app_handle.emit("scanner-status", "error") {
                                log_error(&format!("Failed to emit scanner-status error: {}", e));
                            }
                            thread::sleep(Duration::from_millis(SILENT_COLLECTOR_ATTACH_RETRY_MS));
                            continue;
                        }
                    }
                };

                #[cfg(not(target_os = "windows"))]
                let mut scanner = match DropScanner::new(loot_history.clone()) {
                    Ok(scanner) => scanner,
                    Err(e) => {
                        log_error(&format!("Silent collector scanner setup failed: {}", e));
                        is_scanning.store(false, Ordering::SeqCst);
                        thread::sleep(Duration::from_millis(SILENT_COLLECTOR_ATTACH_RETRY_MS));
                        continue;
                    }
                };

                scanner.set_filter_enabled(false);
                scanner.set_verbose_filter_logging(verbose_filter_logging.load(Ordering::SeqCst));
                if let Err(e) = app_handle.emit("scanner-status", "running") {
                    log_error(&format!("Failed to emit scanner-status running: {}", e));
                }

                let mut was_ingame = false;
                let mut dict_published = false;

                while should_run.load(Ordering::SeqCst) && is_diablo2_running() {
                    scanner.set_verbose_filter_logging(verbose_filter_logging.load(Ordering::SeqCst));
                    let ingame = scanner.is_ingame();
                    game_status.store(
                        if ingame {
                            GAME_STATUS_INGAME
                        } else {
                            GAME_STATUS_MENU
                        },
                        Ordering::SeqCst,
                    );

                    if ingame && !was_ingame {
                        log_info("Silent collector entered game");
                        scanner.clear_cache();
                        if let Ok(mut h) = loot_history.write() {
                            h.clear();
                        }
                        scanner.suppress_item_events_for(Duration::from_secs(2));
                        if let Err(e) = app_handle.emit("game-status", "ingame") {
                            log_error(&format!("Failed to emit game-status ingame: {}", e));
                        }
                    } else if !ingame && was_ingame {
                        if let Ok(mut h) = loot_history.write() {
                            let _ = h.mark_all_pending_lost();
                        }
                        if let Err(e) = app_handle.emit("game-status", "menu") {
                            log_error(&format!("Failed to emit game-status menu: {}", e));
                        }
                    }
                    was_ingame = ingame;

                    if ingame {
                        let events = scanner.tick_items();
                        push_silent_drop_events(&silent_drop_queue, events);
                        let _ = scanner.drain_pickup_updates();
                        let _ = scanner.drain_goblin_events();

                        if !dict_published {
                            if let Some(dict) = scanner.items_dictionary_snapshot() {
                                if let Ok(mut guard) = items_dictionary.write() {
                                    *guard = Some(dict.clone());
                                }
                                items_cache::save_items_cache(&app_handle, &dict);
                                dict_published = true;
                            }
                        }
                    }

                    thread::sleep(Duration::from_millis(SILENT_COLLECTOR_TICK_MS));
                }

                is_scanning.store(false, Ordering::SeqCst);
                game_status.store(GAME_STATUS_UNKNOWN, Ordering::SeqCst);
                if let Err(e) = app_handle.emit("scanner-status", "stopped") {
                    log_error(&format!("Failed to emit scanner-status stopped: {}", e));
                }
                if let Err(e) = app_handle.emit("game-status", "unknown") {
                    log_error(&format!("Failed to emit game-status unknown: {}", e));
                }
                thread::sleep(Duration::from_millis(SILENT_COLLECTOR_ATTACH_RETRY_MS));
            }
        })
        .expect("failed to spawn silent-drop-collector thread");
    *scanner_thread.lock().unwrap() = Some(handle);
}

/// Start the scanner (internal function used by auto-start and manual start)
fn start_scanner_internal(
    is_scanning: Arc<AtomicBool>,
    filter_config: Arc<RwLock<Option<rules::FilterConfig>>>,
    filter_enabled: Arc<AtomicBool>,
    verbose_filter_logging: Arc<AtomicBool>,
    reveal_hidden_active: Arc<AtomicBool>,
    filter_config_generation: Arc<AtomicU64>,
    scanner_thread: Arc<Mutex<Option<JoinHandle<()>>>>,
    game_status: Arc<AtomicU8>,
    items_dictionary: Arc<RwLock<Option<ItemsDictionary>>>,
    loot_history: Arc<RwLock<LootHistory>>,
    silent_drop_queue: Arc<Mutex<Vec<ItemDropEvent>>>,
    app_handle: AppHandle,
) {
    // Check if already running
    if is_scanning.load(Ordering::SeqCst) {
        return;
    }

    if let Some(prev) = scanner_thread.lock().unwrap().take() {
        let _ = prev.join();
    }

    // Set scanning flag
    is_scanning.store(true, Ordering::SeqCst);

    // Emit status to frontend
    if let Err(e) = app_handle.emit("scanner-status", "starting") {
        log_error(&format!("Failed to emit event (starting): {}", e));
    }

    // Spawn background scanning thread
    let handle = thread::Builder::new()
        .name("drop-scanner".into())
        .spawn(move || {
            #[cfg(target_os = "windows")]
            let (shared_state, mut scanner) = {
                let ctx = match crate::process::D2Context::new() {
                    Ok(c) => c,
                    Err(e) => {
                        log_error(&format!("Failed to attach to Diablo II: {}", e));
                        if let Err(e) = app_handle.emit("scanner-status", "error") {
                            log_error(&format!("Failed to emit event (error): {}", e));
                        }
                        if let Some(overlay) = app_handle.get_webview_window("overlay") {
                            if let Err(e) = overlay.hide() {
                                log_error(&format!(
                                    "Failed to hide overlay window after scanner attach error: {}",
                                    e
                                ));
                            }
                        }
                        is_scanning.store(false, Ordering::SeqCst);
                        return;
                    }
                };
                let injector = match crate::injection::D2Injector::new(
                    &ctx.process,
                    ctx.d2_client,
                    ctx.d2_common,
                    ctx.d2_lang,
                ) {
                    Ok(i) => i,
                    Err(e) => {
                        log_error(&format!("Failed to create D2Injector: {}", e));
                        if let Err(e) = app_handle.emit("scanner-status", "error") {
                            log_error(&format!("Failed to emit event (error): {}", e));
                        }
                        if let Some(overlay) = app_handle.get_webview_window("overlay") {
                            if let Err(e) = overlay.hide() {
                                log_error(&format!(
                                    "Failed to hide overlay window after scanner attach error: {}",
                                    e
                                ));
                            }
                        }
                        is_scanning.store(false, Ordering::SeqCst);
                        return;
                    }
                };
                let shared_state =
                    Arc::new(crate::scanner_state::SharedScannerState::new(ctx, injector));
                let scanner = match DropScanner::new(shared_state.clone(), loot_history.clone()) {
                    Ok(s) => {
                        log_info("Scanner attached to Diablo II");
                        if let Err(e) = app_handle.emit("scanner-status", "running") {
                            log_error(&format!("Failed to emit event (running): {}", e));
                        }
                        s
                    }
                    Err(e) => {
                        log_error(&format!("Failed to attach to Diablo II: {}", e));
                        if let Err(e) = app_handle.emit("scanner-status", "error") {
                            log_error(&format!("Failed to emit event (error): {}", e));
                        }
                        if let Some(overlay) = app_handle.get_webview_window("overlay") {
                            if let Err(e) = overlay.hide() {
                                log_error(&format!(
                                    "Failed to hide overlay window after scanner attach error: {}",
                                    e
                                ));
                            }
                        }
                        is_scanning.store(false, Ordering::SeqCst);
                        return;
                    }
                };
                (shared_state, scanner)
            };

            // Disabled for safe scanner mode. The legacy marker scanner writes
            // AutomapCell chains into the game process; if SoE moves those
            // layouts, it can cause visual corruption. Drop/grail scanning does
            // not depend on automap markers.
            #[cfg(target_os = "windows")]
            let marker_handle: Option<thread::JoinHandle<()>> = None;

            #[cfg(not(target_os = "windows"))]
            let mut scanner = match DropScanner::new(loot_history.clone()) {
                Ok(s) => {
                    log_info("Scanner attached to Diablo II");
                    if let Err(e) = app_handle.emit("scanner-status", "running") {
                        log_error(&format!("Failed to emit event (running): {}", e));
                    }
                    s
                }
                Err(e) => {
                    log_error(&format!("Failed to attach to Diablo II: {}", e));
                    if let Err(e) = app_handle.emit("scanner-status", "error") {
                        log_error(&format!("Failed to emit event (error): {}", e));
                    }
                    // Ensure overlay is hidden if attachment failed
                    if let Some(overlay) = app_handle.get_webview_window("overlay") {
                        if let Err(e) = overlay.hide() {
                            log_error(&format!(
                                "Failed to hide overlay window after scanner attach error: {}",
                                e
                            ));
                        }
                    }
                    is_scanning.store(false, Ordering::SeqCst);
                    return;
                }
            };

            // Configure filter if available
            let mut last_config_gen = filter_config_generation.load(Ordering::SeqCst);
            if let Ok(guard) = filter_config.read() {
                if let Some(ref config) = *guard {
                    scanner.set_filter_config(Arc::new(RwLock::new(config.clone())));
                    scanner.on_filter_config_changed();
                }
            }
            scanner.set_filter_enabled(filter_enabled.load(Ordering::SeqCst));
            scanner.set_verbose_filter_logging(verbose_filter_logging.load(Ordering::SeqCst));

            // Seed the hook with the current flag so a key already held on
            // attach (e.g. user reopened the game) works on frame one.
            let mut last_reveal = reveal_hidden_active.load(Ordering::SeqCst);
            if let Err(e) = scanner.set_force_show_all(last_reveal) {
                log_error(&format!("Initial set_force_show_all failed: {}", e));
            }

            let mut was_ingame = false;
            let mut dict_published = false;
            // Main scanning loop
            while is_scanning.load(Ordering::SeqCst) {
                // Check if D2 is still running
                if !is_diablo2_running() {
                    log_info("Diablo II closed, stopping scanner");
                    break;
                }

                let ingame = scanner.is_ingame();

                game_status.store(
                    if ingame {
                        GAME_STATUS_INGAME
                    } else {
                        GAME_STATUS_MENU
                    },
                    Ordering::SeqCst,
                );

                // Detect entering a new game
                if ingame && !was_ingame {
                    log_info("Entered game");
                    scanner.clear_cache();
                    if let Ok(mut h) = loot_history.write() {
                        h.clear();
                    }
                    if let Err(e) = app_handle.emit("loot-history-cleared", ()) {
                        log_error(&format!("Failed to emit loot-history-cleared: {}", e));
                    }
                    scanner.suppress_item_events_for(Duration::from_secs(2));
                    #[cfg(target_os = "windows")]
                    shared_state
                        .clear_markers
                        .store(true, Ordering::Relaxed);
                    if let Err(e) = app_handle.emit("game-status", "ingame") {
                        log_error(&format!("Failed to emit event (ingame): {}", e));
                    }
                } else if !ingame && was_ingame {
                    // Exiting to menu: every still-Pending entry is
                    // effectively lost from this session — broadcast each
                    // as a `loot-history-update` so the panel ticks them
                    // over to ⊘. The history is cleared on the next
                    // menu→ingame transition.
                    let pending_to_lost = loot_history
                        .write()
                        .map(|mut h| h.mark_all_pending_lost())
                        .unwrap_or_default();
                    for (unit_id, seed, pickup) in pending_to_lost {
                        #[derive(serde::Serialize)]
                        struct LootHistoryUpdatePayload {
                            unit_id: u32,
                            seed: u32,
                            pickup: PickupState,
                        }
                        let payload = LootHistoryUpdatePayload { unit_id, seed, pickup };
                        if let Err(e) = app_handle.emit("loot-history-update", &payload) {
                            log_error(&format!(
                                "Failed to emit loot-history-update (menu sweep): {}",
                                e
                            ));
                        }
                    }
                    if let Err(e) = app_handle.emit("game-status", "menu") {
                        log_error(&format!("Failed to emit event (menu): {}", e));
                    }
                }
                was_ingame = ingame;

                // Only re-sync config when generation changed (user saved or toggled mode).
                // This avoids reallocating Arcs every tick and also lets us trigger a
                // full re-evaluation of ground items + hide-mask reset on change.
                let current_gen = filter_config_generation.load(Ordering::SeqCst);
                if current_gen != last_config_gen {
                    if let Ok(guard) = filter_config.read() {
                        if let Some(ref config) = *guard {
                            scanner.set_filter_config(Arc::new(RwLock::new(config.clone())));
                            scanner.on_filter_config_changed();
                        }
                    }
                    last_config_gen = current_gen;
                }

                // Sync filter_enabled state from AppState
                let current_filter_enabled = filter_enabled.load(Ordering::SeqCst);
                scanner.set_filter_enabled(current_filter_enabled);
                scanner.set_verbose_filter_logging(
                    verbose_filter_logging.load(Ordering::SeqCst),
                );

                let current_reveal = reveal_hidden_active.load(Ordering::SeqCst);
                if current_reveal != last_reveal {
                    if let Err(e) = scanner.set_force_show_all(current_reveal) {
                        log_error(&format!("set_force_show_all failed: {}", e));
                    }
                    last_reveal = current_reveal;
                }

                // Scan for items
                if ingame {
                    // Split pass: emit notifications first, then run the
                    // (potentially expensive) map-marker BFS. Otherwise
                    // `item-drop` events would wait on the marker pass and
                    // appear with noticeable lag on crowded maps.
                    let items = scanner.tick_items();
                    push_silent_drop_events(&silent_drop_queue, items);
                    for item in Vec::<ItemDropEvent>::new() {
                        // Only emit loot-history-entry when the scanner
                        // actually inserted a new row (false when a
                        // dedup-merge happened — same physical item seen
                        // again after area reload).
                        if item.history_pushed {
                            #[derive(serde::Serialize, Clone)]
                            struct LootHistoryEntryPayload<'a> {
                                unit_id: u32,
                                seed: u32,
                                timestamp_ms: u64,
                                name: &'a str,
                                quality: &'a str,
                                color: Option<&'a str>,
                                pickup: PickupState,
                                unique_kind: Option<String>,
                                is_relic: bool,
                            }
                            // Read history once to get the timestamp+color+unique_kind
                            // the scanner just stamped the entry with.
                            let stamped = loot_history.read().ok().and_then(|h| {
                                h.snapshot()
                                    .iter()
                                    .find(|e| e.unit_id == item.unit_id)
                                    .map(|e| (e.timestamp_ms, e.color.clone(), e.unique_kind.clone(), e.is_relic))
                            });
                            let (timestamp_ms, color_string, unique_kind, is_relic) =
                                stamped.unwrap_or((0, None, None, false));
                            let payload = LootHistoryEntryPayload {
                                unit_id: item.unit_id,
                                seed: item.seed,
                                timestamp_ms,
                                name: &item.name,
                                quality: &item.quality,
                                color: color_string.as_deref(),
                                pickup: PickupState::Pending,
                                unique_kind,
                                is_relic,
                            };
                            if let Err(e) = app_handle.emit("loot-history-entry", &payload) {
                                log_error(&format!(
                                    "Failed to emit loot-history-entry: {}",
                                    e
                                ));
                            }
                        }
                        if let Err(e) = app_handle.emit("item-drop", &item) {
                            log_error(&format!("Failed to emit item-drop event: {}", e));
                        }
                    }

                    // Drain pickup-state transitions and broadcast them.
                    let _ = scanner.drain_pickup_updates();
                    for (unit_id, seed, pickup) in Vec::<(u32, u32, PickupState)>::new() {
                        #[derive(serde::Serialize)]
                        struct LootHistoryUpdatePayload {
                            unit_id: u32,
                            seed: u32,
                            pickup: PickupState,
                        }
                        let payload = LootHistoryUpdatePayload { unit_id, seed, pickup };
                        if let Err(e) = app_handle.emit("loot-history-update", &payload) {
                            log_error(&format!(
                                "Failed to emit loot-history-update: {}",
                                e
                            ));
                        }
                    }

                    let _ = scanner.drain_goblin_events();

                    if !dict_published {
                        if let Some(dict) = scanner.items_dictionary_snapshot() {
                            if let Ok(mut guard) = items_dictionary.write() {
                                *guard = Some(dict.clone());
                            }
                            if let Err(e) = items_cache::save_items_cache(&app_handle, &dict) {
                                log_error(&format!("Failed to save items cache: {}", e));
                            }
                            if let Err(e) = app_handle.emit("items-dictionary-updated", &dict) {
                                log_error(&format!(
                                    "Failed to emit items-dictionary-updated: {}",
                                    e
                                ));
                            }
                            log_info(&format!(
                                "Published items dictionary ({} base, {} TU, {} SU, {} SSU, {} SSSU, {} set items)",
                                dict.base_types.len(),
                                dict.uniques_tu.len(),
                                dict.uniques_su.len(),
                                dict.uniques_ssu.len(),
                                dict.uniques_sssu.len(),
                                dict.set_items.len()
                            ));
                            dict_published = true;
                        }
                    }

                }

                thread::sleep(Duration::from_millis(30));
            }

            // Signal the marker thread, then emit user-visible status before
            // joining — the join can block for one BFS tick.
            #[cfg(target_os = "windows")]
            shared_state.stop.store(true, Ordering::Relaxed);

            is_scanning.store(false, Ordering::SeqCst);
            game_status.store(GAME_STATUS_UNKNOWN, Ordering::SeqCst);
            if let Err(e) = app_handle.emit("scanner-status", "stopped") {
                log_error(&format!("Failed to emit event (stopped): {}", e));
            }
            if let Err(e) = app_handle.emit("game-status", "unknown") {
                log_error(&format!("Failed to emit event (unknown): {}", e));
            }
            if let Some(overlay) = app_handle.get_webview_window("overlay") {
                if let Err(e) = overlay.hide() {
                    log_error(&format!(
                        "Failed to hide overlay window when scanner stopped: {}",
                        e
                    ));
                }
            }

            #[cfg(target_os = "windows")]
            if let Some(h) = marker_handle {
                if h.join().is_err() {
                    log_error("marker-scanner thread panicked");
                }
            }
        })
        .expect("failed to spawn drop-scanner thread");
    *scanner_thread.lock().unwrap() = Some(handle);
}

/// Spawn background thread that monitors for Diablo II and auto-starts scanner
fn spawn_auto_scanner(
    is_scanning: Arc<AtomicBool>,
    should_auto_scan: Arc<AtomicBool>,
    filter_config: Arc<RwLock<Option<rules::FilterConfig>>>,
    filter_enabled: Arc<AtomicBool>,
    verbose_filter_logging: Arc<AtomicBool>,
    reveal_hidden_active: Arc<AtomicBool>,
    filter_config_generation: Arc<AtomicU64>,
    scanner_thread: Arc<Mutex<Option<JoinHandle<()>>>>,
    game_status: Arc<AtomicU8>,
    items_dictionary: Arc<RwLock<Option<ItemsDictionary>>>,
    loot_history: Arc<RwLock<LootHistory>>,
    silent_drop_queue: Arc<Mutex<Vec<ItemDropEvent>>>,
    app_handle: AppHandle,
) {
    thread::spawn(move || {
        while should_auto_scan.load(Ordering::SeqCst) {
            // If not currently scanning, check if D2 is running
            if !is_scanning.load(Ordering::SeqCst) && is_diablo2_running() {
                start_scanner_internal(
                    is_scanning.clone(),
                    filter_config.clone(),
                    filter_enabled.clone(),
                    verbose_filter_logging.clone(),
                    reveal_hidden_active.clone(),
                    filter_config_generation.clone(),
                    scanner_thread.clone(),
                    game_status.clone(),
                    items_dictionary.clone(),
                    loot_history.clone(),
                    silent_drop_queue.clone(),
                    app_handle.clone(),
                );
            }

            // Check every 2 seconds
            thread::sleep(Duration::from_secs(2));
        }
    });
}

#[tauri::command]
fn get_game_status(state: tauri::State<AppState>) -> &'static str {
    match state.game_status.load(Ordering::SeqCst) {
        GAME_STATUS_INGAME => "ingame",
        GAME_STATUS_MENU => "menu",
        _ => "unknown",
    }
}

#[tauri::command]
fn get_scanner_status(state: tauri::State<AppState>) -> bool {
    state.is_scanning.load(Ordering::SeqCst)
}

#[tauri::command]
fn get_items_dictionary(state: tauri::State<AppState>) -> ItemsDictionary {
    state
        .items_dictionary
        .read()
        .ok()
        .and_then(|guard| guard.clone())
        .unwrap_or_default()
}

#[tauri::command]
fn get_loot_history(state: tauri::State<AppState>) -> Vec<LootEntry> {
    state
        .loot_history
        .read()
        .map(|h| h.snapshot())
        .unwrap_or_default()
}

#[tauri::command]
fn clear_loot_history(state: tauri::State<AppState>, app_handle: AppHandle) -> Result<(), String> {
    if let Ok(mut h) = state.loot_history.write() {
        h.clear();
    }
    app_handle
        .emit("loot-history-cleared", ())
        .map_err(|e| format!("Failed to emit loot-history-cleared: {}", e))
}

#[tauri::command]
fn drain_collected_drops(state: tauri::State<AppState>) -> Vec<ItemDropEvent> {
    let Ok(mut queue) = state.silent_drop_queue.lock() else {
        log_error("Failed to acquire silent drop queue lock for drain");
        return Vec::new();
    };
    let drained = std::mem::take(&mut *queue);
    log_info(&format!("Drained {} silent collected drop(s)", drained.len()));
    drained
}

// ===== Filter Configuration Commands =====

/// Set the filter configuration for the scanner
#[tauri::command]
fn set_filter_config(
    config: rules::FilterConfig,
    state: tauri::State<AppState>,
) -> Result<(), String> {
    {
        let mut guard = state
            .filter_config
            .write()
            .map_err(|e| format!("Failed to acquire lock: {}", e))?;
        *guard = Some(config);
    }
    // Bump generation so the scanner thread re-evaluates all ground items
    // (clears hide mask, re-runs rule matching) on the next tick.
    state
        .filter_config_generation
        .fetch_add(1, Ordering::SeqCst);
    Ok(())
}

/// Enable or disable item filtering
#[tauri::command]
fn set_filter_enabled(_enabled: bool, state: tauri::State<AppState>) {
    // SoE Companion's Loot Filter tab is a native ProjectD2 filter editor only.
    // Do not let the inherited companion DSL hook take over in-game labels.
    state.filter_enabled.store(false, Ordering::SeqCst);
}

/// Enable or disable the per-item `[Filter] ...` log line.
#[tauri::command]
fn set_verbose_filter_logging(enabled: bool, state: tauri::State<AppState>) {
    state
        .verbose_filter_logging
        .store(enabled, Ordering::SeqCst);
}

#[tauri::command]
fn set_zone_transition_sync_enabled(enabled: bool, state: tauri::State<AppState>) {
    state
        .zone_transition_sync_enabled
        .store(enabled, Ordering::SeqCst);
}

// ===== DSL Parser Commands =====

/// Parse DSL text into FilterConfig JSON
#[tauri::command]
fn parse_filter_dsl(text: String) -> Result<rules::FilterConfig, Vec<rules::ParseError>> {
    rules::parse_dsl(&text)
}

/// Validate DSL text and return errors/warnings
#[tauri::command]
fn validate_filter_dsl(text: String) -> Vec<rules::ValidationError> {
    rules::validate_dsl(&text)
}

/// Plain-English explanation for a single rule line, used by the
/// editor's hover tooltip. Returns `None` for blank lines, comments,
/// group close `}`, and unparseable input.
#[tauri::command]
fn explain_filter_line(line: String) -> Option<String> {
    rules::explain_line(&line)
}

/// Resolve the filter decision for a hypothetical item. Used by the UI
/// to preview what the current filter would do without actually dropping
/// anything in-game. See `docs/filter-preview-todo.md` for the planned UI
/// scenarios built around this command.
#[tauri::command]
fn get_item_filter_action(
    config: rules::FilterConfig,
    item: notifier::ItemDropEvent,
) -> rules::FilterDecision {
    use crate::rules::MatchContext;
    let ctx = MatchContext::new(&item);
    config.decide(&ctx)
}

#[cfg(target_os = "windows")]
fn apply_overlay_click_through_style(click_through: bool) -> Result<(), String> {
    let title_wide: Vec<u16> = OsStr::new("SoE Companion Overlay")
        .encode_wide()
        .chain(Some(0))
        .collect();

    let hwnd_overlay = unsafe { FindWindowW(PCWSTR::null(), PCWSTR(title_wide.as_ptr())) }
        .map_err(|_| "Overlay OS window 'SoE Companion Overlay' not found".to_string())?;

    if hwnd_overlay.0.is_null() {
        return Err("Overlay HWND is null".to_string());
    }

    unsafe {
        let ex_style = GetWindowLongW(hwnd_overlay, GWL_EXSTYLE);
        let mut new_ex = ex_style
            | WS_EX_LAYERED.0 as i32
            | WS_EX_TOOLWINDOW.0 as i32
            | WS_EX_NOACTIVATE.0 as i32;
        if click_through {
            new_ex |= WS_EX_TRANSPARENT.0 as i32;
        } else {
            new_ex &= !(WS_EX_TRANSPARENT.0 as i32);
        }
        SetWindowLongW(hwnd_overlay, GWL_EXSTYLE, new_ex);
        let _ = SetWindowPos(
            hwnd_overlay,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED,
        );
    }

    OVERLAY_LAST_CLICK_THROUGH_APPLIED.store(if click_through { 1 } else { 0 }, Ordering::SeqCst);
    Ok(())
}

#[tauri::command]
fn set_overlay_interactive(app: AppHandle, active: bool) -> Result<(), String> {
    let click_through = !active;
    OVERLAY_CLICK_THROUGH.store(click_through, Ordering::SeqCst);
    #[cfg(target_os = "windows")]
    {
        // Apply the WS_EX_TRANSPARENT change immediately instead of waiting for
        // the next game-window sync. Without this, unlocking from the main UI
        // can leave the overlay visually unlocked but still mouse-click-through.
        if let Err(e) = apply_overlay_click_through_style(click_through) {
            log_error(&format!(
                "set_overlay_interactive: direct style update failed: {}",
                e
            ));
        }
        let _ = sync_overlay_with_game_impl(&app);
    }
    #[cfg(not(target_os = "windows"))]
    {
        let _ = app;
    }
    Ok(())
}

#[tauri::command]
fn set_always_show_overlays(app: AppHandle, enabled: bool) -> Result<(), String> {
    OVERLAY_ALWAYS_VISIBLE.store(enabled, Ordering::SeqCst);
    #[cfg(target_os = "windows")]
    {
        if enabled {
            let _ = sync_overlay_with_game_impl(&app);
        }
    }
    #[cfg(not(target_os = "windows"))]
    {
        let _ = app;
    }
    Ok(())
}

#[tauri::command]
fn sync_overlay_with_game(app: AppHandle) -> Result<(), String> {
    #[cfg(target_os = "windows")]
    {
        sync_overlay_with_game_impl(&app)
    }

    #[cfg(not(target_os = "windows"))]
    {
        let _ = app;
        Err("Overlay sync is only supported on Windows".to_string())
    }
}

#[tauri::command]
fn set_tracker_overlay_window_visible(app: AppHandle, visible: bool) -> Result<(), String> {
    let window = app
        .get_webview_window("tracker-overlay")
        .ok_or("Tracker overlay window with label 'tracker-overlay' not found")?;
    if visible {
        window
            .show()
            .map_err(|e| format!("Failed to show tracker overlay window: {}", e))?;
    } else {
        window
            .hide()
            .map_err(|e| format!("Failed to hide tracker overlay window: {}", e))?;
    }
    Ok(())
}

#[tauri::command]
fn set_muling_banner_window_visible(app: AppHandle, visible: bool) -> Result<(), String> {
    let window = app
        .get_webview_window("muling-banner")
        .ok_or("Muling banner window with label 'muling-banner' not found")?;

    if !visible {
        return window
            .hide()
            .map_err(|e| format!("Failed to hide muling banner window: {}", e));
    }

    #[cfg(target_os = "windows")]
    {
        let class_wide: Vec<u16> = OsStr::new("Diablo II")
            .encode_wide()
            .chain(Some(0))
            .collect();

        let hwnd_game = unsafe { FindWindowW(PCWSTR(class_wide.as_ptr()), PCWSTR::null()) }
            .map_err(|_| {
                "Diablo II window not found (class 'Diablo II'). Is the game running?".to_string()
            })?;

        if hwnd_game.0.is_null() {
            let _ = window.hide();
            return Err("Diablo II window handle is null".to_string());
        }

        let mut rect = RECT::default();
        unsafe {
            GetWindowRect(hwnd_game, &mut rect)
                .map_err(|e| format!("GetWindowRect failed: {}", e))?;
        }

        const BANNER_WIDTH: i32 = 300;
        const BANNER_HEIGHT: i32 = 48;
        let game_width = rect.right - rect.left;
        let x = rect.left + ((game_width - BANNER_WIDTH) / 2).max(0);
        let y = rect.top + 18;

        let _ = window.set_size(PhysicalSize::new(BANNER_WIDTH as u32, BANNER_HEIGHT as u32));
        let _ = window.set_position(PhysicalPosition::new(x, y));
    }

    window
        .show()
        .map_err(|e| format!("Failed to show muling banner window: {}", e))?;
    Ok(())
}

#[derive(serde::Serialize)]
struct GameWindowRect {
    left: i32,
    top: i32,
    width: i32,
    height: i32,
}

#[cfg(target_os = "windows")]
fn get_diablo_window_rect() -> Result<GameWindowRect, String> {
    let class_wide: Vec<u16> = OsStr::new("Diablo II")
        .encode_wide()
        .chain(Some(0))
        .collect();

    let hwnd_game =
        unsafe { FindWindowW(PCWSTR(class_wide.as_ptr()), PCWSTR::null()) }.map_err(|_| {
            "Diablo II window not found (class 'Diablo II'). Is the game running?".to_string()
        })?;

    if hwnd_game.0.is_null() {
        return Err("Diablo II window handle is null".to_string());
    }

    let mut rect = RECT::default();
    unsafe {
        GetWindowRect(hwnd_game, &mut rect).map_err(|e| format!("GetWindowRect failed: {}", e))?;
    }

    Ok(GameWindowRect {
        left: rect.left,
        top: rect.top,
        width: rect.right - rect.left,
        height: rect.bottom - rect.top,
    })
}

#[tauri::command]
fn get_game_window_rect() -> Result<GameWindowRect, String> {
    #[cfg(target_os = "windows")]
    {
        get_diablo_window_rect()
    }

    #[cfg(not(target_os = "windows"))]
    {
        Err("Game window positioning is only supported on Windows".to_string())
    }
}

fn editor_window_size(label: &str) -> Option<(u32, u32)> {
    match label {
        "notification-card-overlay" => Some((320, 420)),
        "drops-card-overlay" => Some((240, 260)),
        "total-card-overlay" => Some((240, 190)),
        "grail-card-overlay" => Some((240, 290)),
        "runes-card-overlay" => Some((280, 560)),
        "mats-card-overlay" => Some((280, 420)),
        "fate-cards-card-overlay" => Some((238, 180)),
        "achievement-card-overlay" => Some((238, 86)),
        "achievement-popup-overlay" => Some((300, 88)),
        "kills-card-overlay" => Some((238, 74)),
        "muling-card-overlay" => Some((58, 58)),
        _ => None,
    }
}

fn editor_window_width_bounds(_label: &str) -> (u32, u32) {
    (1, 4000)
}

fn editor_window_height_bounds(_label: &str) -> (u32, u32) {
    (1, 4000)
}

fn clamp_editor_window_rect(
    rect: &GameWindowRect,
    screen_x: i32,
    screen_y: i32,
    width: i32,
    height: i32,
) -> (i32, i32) {
    let max_x = rect.left + (rect.width - width).max(0);
    let max_y = rect.top + (rect.height - height).max(0);
    (
        screen_x.clamp(rect.left, max_x),
        screen_y.clamp(rect.top, max_y),
    )
}

#[cfg(target_os = "windows")]
fn logical_to_physical_u32(value: u32, scale_factor: f64) -> u32 {
    ((value as f64) * scale_factor.max(0.1)).round().max(1.0) as u32
}

#[cfg(target_os = "windows")]
fn logical_to_physical_i32(value: f64, scale_factor: f64) -> i32 {
    (value * scale_factor.max(0.1)).round() as i32
}

#[cfg(target_os = "windows")]
fn overlay_editor_window_title(label: &str) -> Option<&'static str> {
    match label {
        "notification-card-overlay" => Some("SoE Companion Notifications Overlay"),
        "drops-card-overlay" => Some("SoE Companion Drops Overlay"),
        "total-card-overlay" => Some("SoE Companion Total Drops Overlay"),
        "grail-card-overlay" => Some("SoE Companion Grail Overlay"),
        "runes-card-overlay" => Some("SoE Companion Rune Overlay"),
        "mats-card-overlay" => Some("SoE Companion Mats Overlay"),
        "fate-cards-card-overlay" => Some("SoE Companion Fate Cards Overlay"),
        "achievement-card-overlay" => Some("SoE Companion Achievement Overlay"),
        "achievement-popup-overlay" => Some("SoE Companion Achievement Popup Overlay"),
        "kills-card-overlay" => Some("SoE Companion Monster Kills Overlay"),
        "muling-card-overlay" => Some("SoE Companion Muling Indicator Overlay"),
        _ => None,
    }
}

#[cfg(target_os = "windows")]
fn apply_overlay_editor_window_style(label: &str, click_through: bool) -> Result<(), String> {
    let title = overlay_editor_window_title(label)
        .ok_or_else(|| format!("Unsupported overlay editor window '{}'", label))?;
    let title_wide: Vec<u16> = OsStr::new(title).encode_wide().chain(Some(0)).collect();

    let hwnd = unsafe { FindWindowW(PCWSTR::null(), PCWSTR(title_wide.as_ptr())) }
        .map_err(|_| format!("Overlay editor OS window '{}' not found", title))?;

    if hwnd.0.is_null() {
        return Err(format!("Overlay editor HWND '{}' is null", title));
    }

    unsafe {
        let ex_style = GetWindowLongW(hwnd, GWL_EXSTYLE);
        let mut new_ex = ex_style
            | WS_EX_LAYERED.0 as i32
            | WS_EX_TOOLWINDOW.0 as i32
            | WS_EX_NOACTIVATE.0 as i32;
        if click_through {
            new_ex |= WS_EX_TRANSPARENT.0 as i32;
        } else {
            new_ex &= !(WS_EX_TRANSPARENT.0 as i32);
        }
        SetWindowLongW(hwnd, GWL_EXSTYLE, new_ex);

        let style = GetWindowLongW(hwnd, GWL_STYLE);
        let chrome_mask = (WS_CAPTION.0
            | WS_BORDER.0
            | WS_DLGFRAME.0
            | WS_THICKFRAME.0
            | WS_SYSMENU.0
            | WS_MINIMIZEBOX.0
            | WS_MAXIMIZEBOX.0) as i32;
        let new_style = (style & !chrome_mask) | WS_POPUP.0 as i32;
        if new_style != style {
            SetWindowLongW(hwnd, GWL_STYLE, new_style);
        }

        let _ = SetLayeredWindowAttributes(hwnd, COLORREF(0), 1, LWA_ALPHA);

        let _ = SetWindowPos(
            hwnd,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED,
        );
    }

    Ok(())
}

#[tauri::command]
fn set_overlay_editor_window_visible(
    app: AppHandle,
    label: String,
    visible: bool,
    x: Option<f64>,
    y: Option<f64>,
    width: Option<u32>,
    height: Option<u32>,
) -> Result<(), String> {
    let window = app
        .get_webview_window(&label)
        .ok_or_else(|| format!("Overlay editor window with label '{}' not found", label))?;

    if !visible {
        return window
            .hide()
            .map_err(|e| format!("Failed to hide overlay editor window '{}': {}", label, e));
    }

    let (default_width, default_height) = editor_window_size(&label)
        .ok_or_else(|| format!("Unsupported overlay editor window '{}'", label))?;
    let (min_width, max_width) = editor_window_width_bounds(&label);
    let (min_height, max_height) = editor_window_height_bounds(&label);
    let width = width.unwrap_or(default_width).clamp(min_width, max_width);
    let height = height
        .unwrap_or(default_height)
        .clamp(min_height, max_height);

    #[cfg(target_os = "windows")]
    {
        let rect = get_diablo_window_rect()?;
        let scale_factor = window.scale_factor().unwrap_or(1.0);
        let physical_width = logical_to_physical_u32(width, scale_factor);
        let physical_height = logical_to_physical_u32(height, scale_factor);
        let fallback_x = if label == "total-card-overlay" || label == "runes-card-overlay" {
            (rect.width - physical_width as i32 - 12).max(0)
        } else {
            12
        };
        let fallback_y = if label == "drops-card-overlay" || label == "total-card-overlay" {
            (rect.height - physical_height as i32 - 12).max(0)
        } else if label == "grail-card-overlay" {
            (rect.height - physical_height as i32 - 220).max(0)
        } else {
            12
        };

        let rel_x = x
            .map(|value| logical_to_physical_i32(value, scale_factor))
            .unwrap_or(fallback_x);
        let rel_y = y
            .map(|value| logical_to_physical_i32(value, scale_factor))
            .unwrap_or(fallback_y);
        let (screen_x, screen_y) = clamp_editor_window_rect(
            &rect,
            rect.left + rel_x,
            rect.top + rel_y,
            physical_width as i32,
            physical_height as i32,
        );

        let _ = window.set_size(PhysicalSize::new(physical_width, physical_height));
        let _ = window.set_position(PhysicalPosition::new(screen_x, screen_y));
        let _ = apply_overlay_editor_window_style(&label, false);
    }

    window
        .show()
        .map_err(|e| format!("Failed to show overlay editor window '{}': {}", label, e))?;
    #[cfg(target_os = "windows")]
    {
        let _ = apply_overlay_editor_window_style(&label, false);
    }
    Ok(())
}

#[tauri::command]
fn move_overlay_editor_window(
    app: AppHandle,
    label: String,
    screen_x: i32,
    screen_y: i32,
) -> Result<(), String> {
    let window = app
        .get_webview_window(&label)
        .ok_or_else(|| format!("Overlay editor window with label '{}' not found", label))?;

    #[cfg(target_os = "windows")]
    {
        let title = overlay_editor_window_title(&label)
            .ok_or_else(|| format!("Unsupported overlay editor window '{}'", label))?;
        let title_wide: Vec<u16> = OsStr::new(title).encode_wide().chain(Some(0)).collect();

        let hwnd = unsafe { FindWindowW(PCWSTR::null(), PCWSTR(title_wide.as_ptr())) }
            .map_err(|_| format!("Overlay editor OS window '{}' not found", title))?;

        if hwnd.0.is_null() {
            return Err(format!("Overlay editor HWND '{}' is null", title));
        }

        let rect = get_diablo_window_rect()?;
        let (fallback_width, fallback_height) = editor_window_size(&label)
            .ok_or_else(|| format!("Unsupported overlay editor window '{}'", label))?;
        let window_size = window
            .outer_size()
            .map(|size| (size.width as i32, size.height as i32))
            .unwrap_or((fallback_width as i32, fallback_height as i32));
        let (clamped_x, clamped_y) =
            clamp_editor_window_rect(&rect, screen_x, screen_y, window_size.0, window_size.1);

        unsafe {
            let _ = SetWindowPos(
                hwnd,
                HWND_TOPMOST,
                clamped_x,
                clamped_y,
                0,
                0,
                SWP_NOSIZE | SWP_NOACTIVATE,
            );
        }
    }

    #[cfg(not(target_os = "windows"))]
    {
        let _ = window.set_position(PhysicalPosition::new(screen_x, screen_y));
    }

    Ok(())
}

#[tauri::command]
fn resize_overlay_editor_window(
    app: AppHandle,
    label: String,
    width: u32,
    height: u32,
) -> Result<(), String> {
    let window = app
        .get_webview_window(&label)
        .ok_or_else(|| format!("Overlay editor window with label '{}' not found", label))?;
    let (min_width, max_width) = editor_window_width_bounds(&label);
    let (min_height, max_height) = editor_window_height_bounds(&label);
    let width = width.clamp(min_width, max_width);
    let height = height.clamp(min_height, max_height);

    #[cfg(target_os = "windows")]
    {
        let scale_factor = window.scale_factor().unwrap_or(1.0);
        let physical_width = logical_to_physical_u32(width, scale_factor);
        let physical_height = logical_to_physical_u32(height, scale_factor);
        let title = overlay_editor_window_title(&label)
            .ok_or_else(|| format!("Unsupported overlay editor window '{}'", label))?;
        let title_wide: Vec<u16> = OsStr::new(title).encode_wide().chain(Some(0)).collect();

        let hwnd = unsafe { FindWindowW(PCWSTR::null(), PCWSTR(title_wide.as_ptr())) }
            .map_err(|_| format!("Overlay editor OS window '{}' not found", title))?;

        if hwnd.0.is_null() {
            return Err(format!("Overlay editor HWND '{}' is null", title));
        }

        let rect = get_diablo_window_rect()?;
        let position = window
            .outer_position()
            .map(|position| (position.x, position.y))
            .unwrap_or((rect.left, rect.top));
        let (clamped_x, clamped_y) = clamp_editor_window_rect(
            &rect,
            position.0,
            position.1,
            physical_width as i32,
            physical_height as i32,
        );

        unsafe {
            let _ = SetWindowPos(
                hwnd,
                HWND_TOPMOST,
                clamped_x,
                clamped_y,
                physical_width as i32,
                physical_height as i32,
                SWP_NOACTIVATE,
            );
        }
    }

    #[cfg(not(target_os = "windows"))]
    {
        let _ = window.set_size(PhysicalSize::new(width, height));
    }

    Ok(())
}

static OVERLAY_WAS_VISIBLE: AtomicBool = AtomicBool::new(false);
static OVERLAY_CLICK_THROUGH: AtomicBool = AtomicBool::new(true);
static OVERLAY_ALWAYS_VISIBLE: AtomicBool = AtomicBool::new(false);
static OVERLAY_STYLES_APPLIED: AtomicBool = AtomicBool::new(false);

// -1 sentinel = never applied; forces first sync to push the style.
static OVERLAY_LAST_CLICK_THROUGH_APPLIED: std::sync::atomic::AtomicI8 =
    std::sync::atomic::AtomicI8::new(-1);

#[cfg(target_os = "windows")]
static OVERLAY_LAST_RECT: Mutex<Option<RECT>> = Mutex::new(None);

#[cfg(target_os = "windows")]
fn sync_overlay_with_game_impl(app: &AppHandle) -> Result<(), String> {
    let class_wide: Vec<u16> = OsStr::new("Diablo II")
        .encode_wide()
        .chain(Some(0))
        .collect();

    let hwnd_game =
        unsafe { FindWindowW(PCWSTR(class_wide.as_ptr()), PCWSTR::null()) }.map_err(|_| {
            "Diablo II window not found (class 'Diablo II'). Is the game running?".to_string()
        })?;

    if hwnd_game.0.is_null() {
        return Err("Diablo II window handle is null".to_string());
    }
    if !is_diablo2_window_alive(hwnd_game) {
        return Err("Diablo II window is hung or its process has exited".to_string());
    }

    let overlay_window = app
        .get_webview_window("overlay")
        .ok_or("Overlay window with label 'overlay' not found")?;

    let title_wide: Vec<u16> = OsStr::new("SoE Companion Overlay")
        .encode_wide()
        .chain(Some(0))
        .collect();

    let hwnd_overlay = unsafe { FindWindowW(PCWSTR::null(), PCWSTR(title_wide.as_ptr())) }
        .map_err(|_| "Overlay OS window 'SoE Companion Overlay' not found".to_string())?;

    if hwnd_overlay.0.is_null() {
        return Err("Overlay HWND is null".to_string());
    }

    unsafe {
        let fg = GetForegroundWindow();
        if fg.0 != hwnd_game.0 && !OVERLAY_ALWAYS_VISIBLE.load(Ordering::SeqCst) {
            let _ = ShowWindow(hwnd_overlay, SW_HIDE);
            let _ = overlay_window.hide();
            OVERLAY_WAS_VISIBLE.store(false, Ordering::SeqCst);
            OVERLAY_STYLES_APPLIED.store(false, Ordering::SeqCst);
            OVERLAY_LAST_CLICK_THROUGH_APPLIED.store(-1, Ordering::SeqCst);
            if let Ok(mut last) = OVERLAY_LAST_RECT.lock() {
                *last = None;
            }
            return Ok(());
        }
    }

    let mut rect = RECT::default();
    unsafe {
        GetWindowRect(hwnd_game, &mut rect).map_err(|e| format!("GetWindowRect failed: {}", e))?;
    }

    let width = rect.right - rect.left;
    let height = rect.bottom - rect.top;
    if width <= 0 || height <= 0 {
        return Err(format!(
            "Invalid Diablo II window rect: {}x{}",
            width, height
        ));
    }

    // Keep Tauri/WebView's own viewport in lockstep with the native overlay
    // HWND. Raw Win32 MoveWindow resizes the shell window, but WebView2 can
    // otherwise keep its original content hit-test area, leaving only the
    // initial top-left portion interactive.
    let _ = overlay_window.set_position(PhysicalPosition::new(rect.left, rect.top));
    let _ = overlay_window.set_size(PhysicalSize::new(width as u32, height as u32));

    let was_visible = OVERLAY_WAS_VISIBLE.swap(true, Ordering::SeqCst);

    unsafe {
        // WS_EX_NOACTIVATE prevents the overlay from ever stealing foreground
        // from the game — without it, alt-tabbing back triggers a focus war
        // that flickers the screen edges and steals mouse input.
        let just_applied = !OVERLAY_STYLES_APPLIED.swap(true, Ordering::SeqCst);
        if just_applied {
            let ex_style = GetWindowLongW(hwnd_overlay, GWL_EXSTYLE);
            let desired_ct = OVERLAY_CLICK_THROUGH.load(Ordering::SeqCst);
            let mut new_ex = ex_style
                | WS_EX_LAYERED.0 as i32
                | WS_EX_TOOLWINDOW.0 as i32
                | WS_EX_NOACTIVATE.0 as i32;
            if desired_ct {
                new_ex |= WS_EX_TRANSPARENT.0 as i32;
            } else {
                new_ex &= !(WS_EX_TRANSPARENT.0 as i32);
            }
            SetWindowLongW(hwnd_overlay, GWL_EXSTYLE, new_ex);
            OVERLAY_LAST_CLICK_THROUGH_APPLIED
                .store(if desired_ct { 1 } else { 0 }, Ordering::SeqCst);

            // On some systems Tauri's `decorations: false` leaks chrome bits
            // (Aero Lite, Windhawk/ExplorerPatcher, classic theme), so strip
            // them by hand and force WS_POPUP.
            let style = GetWindowLongW(hwnd_overlay, GWL_STYLE);
            let chrome_mask = (WS_CAPTION.0
                | WS_BORDER.0
                | WS_DLGFRAME.0
                | WS_THICKFRAME.0
                | WS_SYSMENU.0
                | WS_MINIMIZEBOX.0
                | WS_MAXIMIZEBOX.0) as i32;
            let new_style = (style & !chrome_mask) | WS_POPUP.0 as i32;
            if new_style != style {
                SetWindowLongW(hwnd_overlay, GWL_STYLE, new_style);
                let _ = SetWindowPos(
                    hwnd_overlay,
                    HWND_TOPMOST,
                    0,
                    0,
                    0,
                    0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED,
                );
            }

            // Suppress the 1px Win11 DWM accent frame; ignored on Win10.
            const DWMWA_COLOR_NONE: u32 = 0xFFFFFFFE;
            let _ = DwmSetWindowAttribute(
                hwnd_overlay,
                DWMWA_BORDER_COLOR,
                &DWMWA_COLOR_NONE as *const u32 as *const _,
                std::mem::size_of::<u32>() as u32,
            );
        }

        // WebView2 only commits transparency on a resize, so on the first show
        // we resize by 1px and back. SW_SHOWNA (not Tauri's show(), which uses
        // SW_SHOW) — SW_SHOW would activate and steal focus from the game.
        if !was_visible {
            let _ = MoveWindow(
                hwnd_overlay,
                rect.left,
                rect.top,
                width + 1,
                height + 1,
                BOOL(1),
            );
            let _ = ShowWindow(hwnd_overlay, SW_SHOWNA);
            let _ = MoveWindow(hwnd_overlay, rect.left, rect.top, width, height, BOOL(1));
            if let Ok(mut last) = OVERLAY_LAST_RECT.lock() {
                *last = Some(rect);
            }
        } else {
            let needs_move = OVERLAY_LAST_RECT
                .lock()
                .ok()
                .map(|guard| match *guard {
                    Some(prev) => {
                        prev.left != rect.left
                            || prev.top != rect.top
                            || prev.right != rect.right
                            || prev.bottom != rect.bottom
                    }
                    None => true,
                })
                .unwrap_or(true);
            if needs_move {
                let _ = MoveWindow(hwnd_overlay, rect.left, rect.top, width, height, BOOL(1));
                if let Ok(mut last) = OVERLAY_LAST_RECT.lock() {
                    *last = Some(rect);
                }
            }
        }

        // No SWP_SHOWWINDOW: that flag forces a frame repaint each tick, which
        // re-flashed the Win11 DWM border and was a major source of the
        // edge-flicker.
        let _ = SetWindowPos(
            hwnd_overlay,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE,
        );
    }

    let desired_ct = OVERLAY_CLICK_THROUGH.load(Ordering::SeqCst);
    let desired_i8: i8 = if desired_ct { 1 } else { 0 };
    if OVERLAY_LAST_CLICK_THROUGH_APPLIED.load(Ordering::SeqCst) != desired_i8 {
        unsafe {
            let ex_style = GetWindowLongW(hwnd_overlay, GWL_EXSTYLE);
            let new_ex = if desired_ct {
                ex_style | WS_EX_TRANSPARENT.0 as i32 | WS_EX_NOACTIVATE.0 as i32
            } else {
                (ex_style & !(WS_EX_TRANSPARENT.0 as i32)) | WS_EX_NOACTIVATE.0 as i32
            };
            SetWindowLongW(hwnd_overlay, GWL_EXSTYLE, new_ex);
        }
        OVERLAY_LAST_CLICK_THROUGH_APPLIED.store(desired_i8, Ordering::SeqCst);
    }

    Ok(())
}

/// Enable SeDebugPrivilege on the current process token, if possible.
///
/// This matches what many memory tools (including the original AutoIt-based D2Stats)
/// do before calling OpenProcess on game processes. Without this privilege, some
/// Windows configurations may return ACCESS_DENIED even for the same user.
#[cfg(target_os = "windows")]
fn enable_debug_privilege() {
    use std::mem::size_of;
    use windows::Win32::Foundation::{CloseHandle, LUID};

    unsafe {
        let mut token_handle = HANDLE::default();
        // We need both QUERY and ADJUST_PRIVILEGES to toggle SeDebugPrivilege.
        let desired_access = TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY;
        if let Err(e) = OpenProcessToken(GetCurrentProcess(), desired_access, &mut token_handle) {
            log_error(&format!("SeDebugPrivilege: OpenProcessToken failed: {}", e));
            return;
        }

        // Resolve the LUID for SeDebugPrivilege.
        let mut luid = LUID::default();
        if let Err(e) = LookupPrivilegeValueW(None, SE_DEBUG_NAME, &mut luid) {
            log_error(&format!(
                "SeDebugPrivilege: LookupPrivilegeValueW failed: {}",
                e
            ));
            let _ = CloseHandle(token_handle);
            return;
        }

        let mut tp = TOKEN_PRIVILEGES {
            PrivilegeCount: 1,
            Privileges: [LUID_AND_ATTRIBUTES {
                Luid: luid,
                Attributes: SE_PRIVILEGE_ENABLED,
            }],
        };

        // Enable SeDebugPrivilege on this token.
        let result = AdjustTokenPrivileges(
            token_handle,
            BOOL(0),
            Some(&tp as *const TOKEN_PRIVILEGES),
            size_of::<TOKEN_PRIVILEGES>() as u32,
            None,
            None,
        );

        let _ = CloseHandle(token_handle);

        if let Err(e) = result {
            log_error(&format!(
                "SeDebugPrivilege: AdjustTokenPrivileges failed: {}",
                e
            ));
        }
    }
}

#[cfg(not(target_os = "windows"))]
fn enable_debug_privilege() {
    // No-op on non-Windows platforms.
}

/// Configure WebView2 user data folder for elevated processes.
///
/// When running with administrator privileges (elevated), WebView2 may fail
/// to access the user's LocalAppData because the elevated process runs under
/// a different user context. This function detects elevation and sets
/// WEBVIEW2_USER_DATA_FOLDER to the non-elevated user's LocalAppData path.
#[cfg(target_os = "windows")]
fn setup_webview2_for_elevation() {
    use std::mem::size_of;

    unsafe {
        // Get the current process token
        let mut token_handle = HANDLE::default();
        if let Err(e) = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut token_handle) {
            log_error(&format!(
                "WebView2 setup: OpenProcessToken failed, skipping elevation check: {}",
                e
            ));
            return;
        }

        // Check elevation type
        let mut elevation_type = TOKEN_ELEVATION_TYPE::default();
        let mut return_length = 0u32;

        let result = GetTokenInformation(
            token_handle,
            TokenElevationType,
            Some(&mut elevation_type as *mut _ as *mut _),
            size_of::<TOKEN_ELEVATION_TYPE>() as u32,
            &mut return_length,
        );

        if let Err(e) = result {
            log_error(&format!(
                "WebView2 setup: GetTokenInformation(TokenElevationType) failed: {}",
                e
            ));
            let _ = windows::Win32::Foundation::CloseHandle(token_handle);
            return;
        }

        // TokenElevationTypeFull (2) means the process is elevated via UAC
        // We need to get the linked token (non-elevated user token) to find correct AppData
        if elevation_type.0 != 2 {
            // Not elevated via UAC, no need to adjust WebView2 path
            let _ = windows::Win32::Foundation::CloseHandle(token_handle);
            return;
        }

        // Get the linked token (the non-elevated user token)
        let mut linked_token = TOKEN_LINKED_TOKEN::default();
        let mut return_length = 0u32;

        let result = GetTokenInformation(
            token_handle,
            TokenLinkedToken,
            Some(&mut linked_token as *mut _ as *mut _),
            size_of::<TOKEN_LINKED_TOKEN>() as u32,
            &mut return_length,
        );

        let _ = windows::Win32::Foundation::CloseHandle(token_handle);

        if let Err(e) = result {
            log_error(&format!(
                "WebView2 setup: GetTokenInformation(TokenLinkedToken) failed: {}",
                e
            ));
            return;
        }

        // Get LocalAppData path using the linked (non-elevated) token
        let path_ptr = SHGetKnownFolderPath(
            &FOLDERID_LocalAppData,
            KF_FLAG_DEFAULT,
            linked_token.LinkedToken,
        );

        let _ = windows::Win32::Foundation::CloseHandle(linked_token.LinkedToken);

        match path_ptr {
            Ok(ptr) => {
                // Convert PWSTR to Rust String
                let path_str = ptr.to_string().unwrap_or_default();
                CoTaskMemFree(Some(ptr.as_ptr() as *const _));

                if !path_str.is_empty() {
                    // Construct WebView2 data folder path
                    let webview2_path = format!("{}\\SoECompanion\\WebView2", path_str);
                    std::env::set_var("WEBVIEW2_USER_DATA_FOLDER", &webview2_path);
                }
            }
            Err(e) => {
                log_error(&format!(
                    "WebView2 setup: SHGetKnownFolderPath(LocalAppData) failed: {:?}",
                    e
                ));
            }
        }
    }
}

#[cfg(not(target_os = "windows"))]
fn setup_webview2_for_elevation() {
    // No-op on non-Windows platforms
}

fn load_initial_filter_config(app: &AppHandle) -> Option<rules::FilterConfig> {
    let _ = app;
    None
}

#[tauri::command]
fn open_app_folder(app: AppHandle) -> Result<(), String> {
    let dir = app
        .path()
        .app_data_dir()
        .map_err(|e| format!("Failed to resolve app data dir: {}", e))?;
    std::fs::create_dir_all(&dir).map_err(|e| format!("Failed to create app data dir: {}", e))?;
    std::process::Command::new("explorer")
        .arg(&dir)
        .spawn()
        .map_err(|e| format!("Failed to open explorer: {}", e))?;
    Ok(())
}

/// Open an http(s) URL in the user's default browser.
/// Scheme validation prevents `start` from being coaxed into launching a
/// local file or custom handler via attacker-controlled URLs.
#[tauri::command]
fn get_changelog() -> &'static str {
    include_str!("../../CHANGELOG.md")
}

#[tauri::command]
fn open_external_url(url: String) -> Result<(), String> {
    if !(url.starts_with("https://") || url.starts_with("http://")) {
        return Err("Only http(s) URLs are allowed".into());
    }
    // `cmd /c start "" <url>` — the empty "" arg is the window title slot that
    // `start` consumes before the target, so the URL is parsed as the target.
    std::process::Command::new("cmd")
        .args(["/c", "start", "", &url])
        .spawn()
        .map_err(|e| format!("Failed to open url: {}", e))?;
    Ok(())
}

fn default_soe_launcher_candidates() -> Vec<std::path::PathBuf> {
    let mut candidates = vec![
        std::path::PathBuf::from(r"C:\Program Files\PD2 Sanctuary of Exile\pd2-soe-launcher.exe"),
        std::path::PathBuf::from(
            r"C:\Program Files (x86)\PD2 Sanctuary of Exile\pd2-soe-launcher.exe",
        ),
    ];
    if let Ok(local) = std::env::var("LOCALAPPDATA") {
        candidates.push(
            std::path::PathBuf::from(local)
                .join("pd2-soe-launcher")
                .join("pd2-soe-launcher.exe"),
        );
    }
    candidates
}

#[tauri::command]
fn detect_soe_launcher_path() -> Option<String> {
    default_soe_launcher_candidates()
        .into_iter()
        .find(|path| path.exists())
        .map(|path| path.display().to_string())
}

#[tauri::command]
fn launch_soe_launcher(path: Option<String>) -> Result<(), String> {
    let resolved = path
        .filter(|value| !value.trim().is_empty())
        .map(std::path::PathBuf::from)
        .or_else(|| {
            default_soe_launcher_candidates()
                .into_iter()
                .find(|candidate| candidate.exists())
        })
        .ok_or_else(|| {
            "SoE launcher was not found. Set the launcher path in General.".to_string()
        })?;

    if !resolved.exists() {
        return Err(format!("SoE launcher not found at {}", resolved.display()));
    }

    std::process::Command::new(&resolved)
        .current_dir(
            resolved
                .parent()
                .unwrap_or_else(|| std::path::Path::new(".")),
        )
        .spawn()
        .map_err(|e| format!("Failed to launch {}: {}", resolved.display(), e))?;
    Ok(())
}

fn main() {
    // Enable SeDebugPrivilege so OpenProcess has the same behavior as legacy tools.
    enable_debug_privilege();

    // Configure WebView2 data folder for elevated processes BEFORE Tauri init
    setup_webview2_for_elevation();

    tauri::Builder::default()
        .plugin(tauri_plugin_store::Builder::default().build())
        .setup(|app| {
            let cached_items = items_cache::load_items_cache(app.handle());
            let initial_filter_config = load_initial_filter_config(app.handle());

            // Shared scanner state
            let state = AppState {
                is_scanning: Arc::new(AtomicBool::new(false)),
                // General background-run flag. The live item scanner stays
                // disabled; this lets lightweight watchers stop cleanly on close.
                should_auto_scan: Arc::new(AtomicBool::new(true)),
                filter_config: Arc::new(RwLock::new(initial_filter_config)),
                filter_enabled: Arc::new(AtomicBool::new(false)),
                verbose_filter_logging: Arc::new(AtomicBool::new(false)),
                zone_transition_sync_enabled: Arc::new(AtomicBool::new(false)),
                reveal_hidden_active: Arc::new(AtomicBool::new(false)),
                filter_config_generation: Arc::new(AtomicU64::new(0)),
                scanner_thread: Arc::new(Mutex::new(None)),
                game_status: Arc::new(AtomicU8::new(GAME_STATUS_UNKNOWN)),
                items_dictionary: Arc::new(RwLock::new(cached_items)),
                loot_history: Arc::new(RwLock::new(LootHistory::new())),
                silent_drop_queue: Arc::new(Mutex::new(Vec::new())),
                grail_log_watcher: grail_log::GrailLogWatcher::new(),
                account_stats_watcher: kill_counter::AccountStatsWatcher::new(),
            };
            let is_scanning = state.is_scanning.clone();
            let should_auto_scan = state.should_auto_scan.clone();
            let filter_config = state.filter_config.clone();
            let filter_enabled = state.filter_enabled.clone();
            let verbose_filter_logging = state.verbose_filter_logging.clone();
            let zone_transition_sync_enabled = state.zone_transition_sync_enabled.clone();
            let reveal_hidden_active = state.reveal_hidden_active.clone();
            let filter_config_generation = state.filter_config_generation.clone();
            let scanner_thread = state.scanner_thread.clone();
            let silent_drop_queue = state.silent_drop_queue.clone();
            let game_status = state.game_status.clone();
            let items_dictionary = state.items_dictionary.clone();
            let loot_history = state.loot_history.clone();
            app.manage(state);

            // Calm sync model: the proven scanner still watches drops, but it queues
            // them in memory so the frontend applies one quiet batch on Save & Exit,
            // zone-change, or Sync All.
            app.state::<AppState>().account_stats_watcher.start(app.handle().clone());
            spawn_auto_scanner(
                is_scanning.clone(),
                should_auto_scan.clone(),
                filter_config,
                filter_enabled,
                verbose_filter_logging.clone(),
                reveal_hidden_active,
                filter_config_generation,
                scanner_thread.clone(),
                game_status,
                items_dictionary,
                loot_history,
                silent_drop_queue,
                app.handle().clone(),
            );
            spawn_zone_transition_watcher(
                should_auto_scan.clone(),
                zone_transition_sync_enabled.clone(),
                app.handle().clone(),
            );

            // Initialize hotkey state
            let hotkey_state = HotkeyState::new();
            let edit_mode_state = EditModeState::new();
            let muling_mode_hotkey_state = MulingModeHotkeyState::new();

            // Load settings and start hotkey listener
            let app_handle_for_hotkeys = app.handle().clone();
            let app_handle_for_edit_mode = app.handle().clone();
            let app_handle_for_muling_mode = app.handle().clone();
            match settings::load_settings(app.handle().clone()) {
                Ok(loaded_settings) => {
                    hotkey_state
                        .start(app_handle_for_hotkeys, loaded_settings.toggle_window_hotkey);
                    edit_mode_state.start(
                        app_handle_for_edit_mode,
                        loaded_settings.edit_overlay_hotkey,
                    );
                    muling_mode_hotkey_state.start(
                        app_handle_for_muling_mode,
                        loaded_settings.muling_mode_hotkey,
                    );
                    verbose_filter_logging
                        .store(loaded_settings.verbose_filter_logging, Ordering::SeqCst);
                    zone_transition_sync_enabled.store(
                        loaded_settings.zone_transition_sync_enabled,
                        Ordering::SeqCst,
                    );
                }
                Err(e) => {
                    log_error(&format!("Failed to load settings for hotkeys: {}", e));
                    // Start with default hotkeys
                    hotkey_state.start(app_handle_for_hotkeys, hotkeys::HotkeyConfig::default());
                    let defaults = settings::AppSettings::default();
                    edit_mode_state.start(app_handle_for_edit_mode, defaults.edit_overlay_hotkey);
                    muling_mode_hotkey_state
                        .start(app_handle_for_muling_mode, defaults.muling_mode_hotkey);
                }
            }

            app.manage(hotkey_state);
            app.manage(edit_mode_state);
            app.manage(muling_mode_hotkey_state);

            // Live scanner auto-start intentionally disabled. Keeping this off
            // avoids continuous memory reads and overlay churn while playing.

            // When the main window is closed, stop everything, close the overlay window
            // and terminate the application.
            if let Some(main_window) = app.get_webview_window("main") {
                let is_scanning_clone = is_scanning.clone();
                let should_auto_scan_clone = should_auto_scan.clone();
                let scanner_thread_clone = scanner_thread.clone();
                let app_handle_clone = app.handle().clone();
                main_window.on_window_event(move |event| {
                    if let WindowEvent::CloseRequested { .. } = event {
                        should_auto_scan_clone.store(false, Ordering::SeqCst);
                        is_scanning_clone.store(false, Ordering::SeqCst);

                        if let Some(overlay) = app_handle_clone.get_webview_window("overlay") {
                            if let Err(e) = overlay.close() {
                                log_error(&format!(
                                    "Failed to close overlay window on main close: {}",
                                    e
                                ));
                            }
                        }

                        if let Some(banner) = app_handle_clone.get_webview_window("muling-banner") {
                            if let Err(e) = banner.close() {
                                log_error(&format!(
                                    "Failed to close muling banner window on main close: {}",
                                    e
                                ));
                            }
                        }

                        for label in [
                            "notification-card-overlay",
                            "drops-card-overlay",
                            "total-card-overlay",
                            "grail-card-overlay",
                            "runes-card-overlay",
                            "mats-card-overlay",
                            "fate-cards-card-overlay",
                            "achievement-card-overlay",
                            "achievement-popup-overlay",
                            "kills-card-overlay",
                            "muling-card-overlay",
                        ] {
                            if let Some(window) = app_handle_clone.get_webview_window(label) {
                                if let Err(e) = window.close() {
                                    log_error(&format!(
                                        "Failed to close overlay editor window '{}' on main close: {}",
                                        label, e
                                    ));
                                }
                            }
                        }

                        let handle_opt = scanner_thread_clone.lock().unwrap().take();
                        let ah = app_handle_clone.clone();
                        thread::spawn(move || {
                            let watchdog_fired = Arc::new(AtomicBool::new(false));
                            let wf_w = watchdog_fired.clone();
                            let ah_w = ah.clone();
                            thread::spawn(move || {
                                thread::sleep(Duration::from_millis(2500));
                                wf_w.store(true, Ordering::SeqCst);
                                log_error("scanner join watchdog fired after 2.5s; exiting");
                                ah_w.exit(0);
                            });
                            if let Some(h) = handle_opt {
                                let _ = h.join();
                            }
                            if !watchdog_fired.load(Ordering::SeqCst) {
                                ah.exit(0);
                            }
                        });
                    }
                });
            }

            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            get_scanner_status,
            get_game_status,
            get_items_dictionary,
            get_loot_history,
            clear_loot_history,
            drain_collected_drops,
            set_filter_config,
            set_filter_enabled,
            set_verbose_filter_logging,
            set_zone_transition_sync_enabled,
            sync_overlay_with_game,
            set_overlay_interactive,
            set_always_show_overlays,
            set_tracker_overlay_window_visible,
            set_muling_banner_window_visible,
            set_overlay_editor_window_visible,
            move_overlay_editor_window,
            resize_overlay_editor_window,
            get_game_window_rect,
            parse_filter_dsl,
            validate_filter_dsl,
            explain_filter_line,
            get_item_filter_action,
            settings::load_settings,
            settings::save_settings,
            settings::save_settings_patch,
            settings::backup_holy_grail_found,
            settings::restore_holy_grail_backup,
            settings::get_holy_grail_backup_status,
            settings::open_holy_grail_backup_folder,
            settings::backup_fate_card_counts,
            settings::restore_fate_card_counts_backup,
            settings::get_fate_card_backup_status,
            settings::open_fate_card_backup_folder,
            settings::backup_achievement_stats,
            settings::backup_achievement_category,
            settings::restore_achievement_backup,
            settings::get_achievement_backup_status,
            settings::open_achievement_backup_folder,
            settings::get_window_state,
            settings::save_window_state,
            sounds::import_sound_file,
            sounds::delete_sound_file,
            sounds::download_filterblade_community_sound,
            soe_filter::get_bundled_soe_filter,
            soe_filter::list_soe_filter_profiles,
            soe_filter::load_soe_filter_profile,
            soe_filter::save_soe_filter_profile,
            soe_filter::create_soe_filter_profile,
            soe_filter::rename_soe_filter_profile,
            soe_filter::delete_soe_filter_profile,
            soe_filter::read_installed_soe_filter,
            soe_filter::write_installed_soe_filter,
            save_exit_automation::write_save_exit_automation_config,
            hotkeys::update_hotkey,
            hotkeys::update_edit_mode_hotkey,
            hotkeys::update_muling_mode_hotkey,
            kill_counter::sync_kills_all,
            kill_counter::sync_accountstats_kills,
            kill_counter::sync_accountstats_stash,
            kill_counter::debug_accountstats_live,
            character_levels::sync_character_levels,
            updater::check_for_updates,
            updater::start_update,
            updater::restart_app,
            runeword_planner::detect_shared_stash_paths,
            runeword_planner::sync_shared_stash_runes,
            drop_hook::get_drop_hook_status,
            drop_hook::install_drop_hook,
            drop_hook::get_drop_identified_config,
            drop_hook::write_drop_identified_config,
            drop_hook::get_drop_hook_status_for_path,
            drop_hook::install_drop_hook_for_path,
            drop_hook::restore_original_ijl11_for_path,
            drop_hook::install_auto_grail_hook_for_path,
            drop_hook::install_identified_drops_hook_for_path,
            drop_hook::uninstall_auto_grail_hook_for_path,
            drop_hook::uninstall_identified_drops_hook_for_path,
            drop_hook::get_drop_identified_config_for_path,
            drop_hook::write_drop_identified_config_for_path,
            drop_hook::detect_project_d2_dirs,
            grail_log::read_grail_log,
            grail_log::clear_grail_log,
            hook_log::read_hook_drop_events,
            open_app_folder,
            open_external_url,
            detect_soe_launcher_path,
            launch_soe_launcher,
            get_changelog
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
