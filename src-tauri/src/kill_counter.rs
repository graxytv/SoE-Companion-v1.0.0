use serde::Serialize;
use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;
use tauri::Manager;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct KillSyncResult {
    pub total_kills: u64,
    pub boss_kills: HashMap<String, u64>,
    pub matched_text: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AccountStatsMemoryCandidate {
    pub address: String,
    pub total_kills: u64,
    pub time_played: u64,
    pub deaths: u64,
    pub score: u32,
    pub summary: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AccountStatsLiveDebug {
    pub stash: Option<KillSyncResult>,
    pub direct: Option<KillSyncResult>,
    pub candidates: Vec<AccountStatsMemoryCandidate>,
    pub exact_matches: Vec<AccountStatsMemoryCandidate>,
    pub errors: Vec<String>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AccountStatsResetResult {
    pub stash_path: String,
    pub backup_path: String,
    pub previous_total_kills: u64,
    pub checksum: String,
}

#[cfg(target_os = "windows")]
pub struct AccountStatsWatcher {
    stop: Arc<AtomicBool>,
}

#[cfg(target_os = "windows")]
impl AccountStatsWatcher {
    pub fn new() -> Self {
        Self {
            stop: Arc::new(AtomicBool::new(false)),
        }
    }

    pub fn start(&self, app: tauri::AppHandle) {
        use std::collections::BTreeSet;
        use std::os::windows::ffi::OsStrExt;
        use std::sync::{Arc, Mutex};
        use tauri::Emitter;
        use windows::core::PCWSTR;
        use windows::Win32::Foundation::{CloseHandle, HANDLE, INVALID_HANDLE_VALUE};
        use windows::Win32::Storage::FileSystem::{
            CreateFileW, ReadDirectoryChangesW, FILE_FLAG_BACKUP_SEMANTICS, FILE_LIST_DIRECTORY,
            FILE_NOTIFY_CHANGE_CREATION, FILE_NOTIFY_CHANGE_FILE_NAME,
            FILE_NOTIFY_CHANGE_LAST_WRITE, FILE_SHARE_DELETE, FILE_SHARE_READ, FILE_SHARE_WRITE,
            OPEN_EXISTING,
        };

        let mut dirs = BTreeSet::new();
        for path in windows_impl::shared_stash_paths() {
            if let Some(parent) = path.parent().filter(|parent| parent.is_dir()) {
                dirs.insert(parent.to_path_buf());
            }
        }

        let last_signature = Arc::new(Mutex::new(
            windows_impl::read_accountstats_stash(None)
                .ok()
                .map(|result| account_stats_signature(&result)),
        ));
        let stop = self.stop.clone();

        if dirs.is_empty() {
            start_stash_accountstats_poll_thread(app.clone(), stop.clone(), last_signature.clone());
            start_live_accountstats_thread(app, stop, last_signature);
            return;
        }

        for dir in dirs {
            let app = app.clone();
            let stop = stop.clone();
            let last_signature = last_signature.clone();
            thread::spawn(move || {
                let wide: Vec<u16> = dir
                    .as_os_str()
                    .encode_wide()
                    .chain(std::iter::once(0))
                    .collect();
                let handle = unsafe {
                    CreateFileW(
                        PCWSTR(wide.as_ptr()),
                        FILE_LIST_DIRECTORY.0,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        None,
                        OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS,
                        HANDLE::default(),
                    )
                };
                let Ok(handle) = handle else {
                    return;
                };
                if handle == INVALID_HANDLE_VALUE {
                    return;
                }

                let mut buffer = [0u8; 4096];
                while !stop.load(Ordering::Relaxed) {
                    let mut bytes_returned = 0u32;
                    let changed = unsafe {
                        ReadDirectoryChangesW(
                            handle,
                            buffer.as_mut_ptr() as *mut _,
                            buffer.len() as u32,
                            false,
                            FILE_NOTIFY_CHANGE_LAST_WRITE
                                | FILE_NOTIFY_CHANGE_FILE_NAME
                                | FILE_NOTIFY_CHANGE_CREATION,
                            Some(&mut bytes_returned),
                            None,
                            None,
                        )
                    };

                    if changed.is_err() || bytes_returned == 0 {
                        thread::sleep(Duration::from_millis(250));
                        continue;
                    }

                    thread::sleep(Duration::from_millis(150));
                    if let Ok(result) = windows_impl::read_accountstats_stash(None) {
                        let signature = account_stats_signature(&result);
                        let mut guard = match last_signature.lock() {
                            Ok(guard) => guard,
                            Err(_) => continue,
                        };
                        if guard.as_ref() == Some(&signature) {
                            continue;
                        }
                        *guard = Some(signature);
                        let _ = app.emit("account-stats-updated", result);
                    }
                }

                let _ = unsafe { CloseHandle(handle) };
            });
        }

        start_stash_accountstats_poll_thread(
            app.clone(),
            self.stop.clone(),
            last_signature.clone(),
        );
        start_live_accountstats_thread(app, self.stop.clone(), last_signature);
    }

    pub fn stop(&self) {
        self.stop.store(true, Ordering::Relaxed);
    }
}

#[cfg(target_os = "windows")]
impl Default for AccountStatsWatcher {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(not(target_os = "windows"))]
pub struct AccountStatsWatcher;

#[cfg(not(target_os = "windows"))]
impl AccountStatsWatcher {
    pub fn new() -> Self {
        Self
    }

    pub fn start(&self, _app: tauri::AppHandle) {}

    pub fn stop(&self) {}
}

#[cfg(not(target_os = "windows"))]
impl Default for AccountStatsWatcher {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(target_os = "windows")]
fn account_stats_signature(result: &KillSyncResult) -> String {
    let mut boss: Vec<_> = result.boss_kills.iter().collect();
    boss.sort_by(|a, b| a.0.cmp(b.0));
    let boss = boss
        .into_iter()
        .map(|(key, value)| format!("{key}={value}"))
        .collect::<Vec<_>>()
        .join(",");
    format!("kills={};{}", result.total_kills, boss)
}

#[cfg(target_os = "windows")]
fn start_live_accountstats_thread(
    app: tauri::AppHandle,
    stop: Arc<AtomicBool>,
    last_signature: Arc<std::sync::Mutex<Option<String>>>,
) {
    use tauri::Emitter;

    thread::spawn(move || {
        while !stop.load(Ordering::Relaxed) {
            thread::sleep(Duration::from_millis(1500));
            let Ok(result) = windows_impl::read_accountstats_live_memory() else {
                continue;
            };
            let signature = account_stats_signature(&result);
            let mut guard = match last_signature.lock() {
                Ok(guard) => guard,
                Err(_) => continue,
            };
            if guard.as_ref() == Some(&signature) {
                continue;
            }
            *guard = Some(signature);
            let _ = app.emit("account-stats-updated", result);
        }
    });
}

#[cfg(target_os = "windows")]
fn start_stash_accountstats_poll_thread(
    app: tauri::AppHandle,
    stop: Arc<AtomicBool>,
    last_signature: Arc<std::sync::Mutex<Option<String>>>,
) {
    use tauri::Emitter;

    thread::spawn(move || {
        while !stop.load(Ordering::Relaxed) {
            thread::sleep(Duration::from_millis(1000));
            let Ok(result) = windows_impl::read_accountstats_stash(None) else {
                continue;
            };
            let signature = account_stats_signature(&result);
            let mut guard = match last_signature.lock() {
                Ok(guard) => guard,
                Err(_) => continue,
            };
            if guard.as_ref() == Some(&signature) {
                continue;
            }
            *guard = Some(signature);
            let _ = app.emit("account-stats-updated", result);
        }
    });
}

#[cfg(target_os = "windows")]
mod windows_impl {
    use super::{
        AccountStatsLiveDebug, AccountStatsMemoryCandidate, AccountStatsResetResult, KillSyncResult,
    };
    use crate::logger::info as log_info;
    use crate::offsets::{d2client, unit};
    use crate::process::D2Context;
    use regex::Regex;
    use std::collections::HashMap;
    use std::ffi::OsStr;
    use std::fs;
    use std::mem;
    use std::os::windows::ffi::OsStrExt;
    use std::path::PathBuf;
    use std::thread;
    use std::time::Duration;
    use windows::core::PCWSTR;
    use windows::Win32::Foundation::HWND;
    use windows::Win32::System::Memory::{
        VirtualQueryEx, MEMORY_BASIC_INFORMATION, MEM_COMMIT, PAGE_GUARD, PAGE_NOACCESS,
    };
    use windows::Win32::UI::Input::KeyboardAndMouse::{
        SendInput, INPUT, INPUT_0, INPUT_KEYBOARD, KEYBDINPUT, KEYBD_EVENT_FLAGS, KEYEVENTF_KEYUP,
        KEYEVENTF_UNICODE, VIRTUAL_KEY, VK_RETURN,
    };
    use windows::Win32::UI::WindowsAndMessaging::{
        FindWindowW, SetForegroundWindow, ShowWindow, SW_RESTORE,
    };

    const MAX_SCAN_ADDRESS: usize = 0x7fff_ffff;
    const MAX_REGION_READ: usize = 2 * 1024 * 1024;
    const MAX_LIVE_KILL_DELTA: u64 = 50_000;

    const ACCOUNT_TIME_PLAYED: usize = 0x3f5;
    const ACCOUNT_MONSTER_KILLS: usize = 0x3f9;
    const ACCOUNT_DCLONE_ANY: usize = 0x3fd;
    const ACCOUNT_DCLONE_T2: usize = 0x3ff;
    const ACCOUNT_RATHMA_ANY: usize = 0x401;
    const ACCOUNT_RATHMA_T2: usize = 0x403;
    const ACCOUNT_LUCION_ANY: usize = 0x405;
    const ACCOUNT_LUCION_T2: usize = 0x407;
    const ACCOUNT_UBER_TRISTRAM: usize = 0x409;
    const ACCOUNT_UBER_ANCIENTS: usize = 0x40b;
    const ACCOUNT_ANDARIEL: usize = 0x40d;
    const ACCOUNT_DURIEL: usize = 0x40f;
    const ACCOUNT_MEPHISTO: usize = 0x411;
    const ACCOUNT_DIABLO: usize = 0x413;
    const ACCOUNT_BAAL: usize = 0x415;
    const ACCOUNT_DEATHS: usize = 0x417;
    const ACCOUNT_MAP_BOSS: usize = 0x419;
    const ACCOUNT_DUNGEON_BOSS: usize = 0x41b;

    const STASH_TIME_PLAYED: usize = 0x12;
    const STASH_MONSTER_KILLS: usize = 0x16;
    const STASH_DCLONE_ANY: usize = 0x1a;
    const STASH_DCLONE_T2: usize = 0x1c;
    const STASH_RATHMA_ANY: usize = 0x1e;
    const STASH_RATHMA_T2: usize = 0x20;
    const STASH_LUCION_ANY: usize = 0x22;
    const STASH_LUCION_T2: usize = 0x24;
    const STASH_UBER_TRISTRAM: usize = 0x26;
    const STASH_UBER_ANCIENTS: usize = 0x28;
    const STASH_ANDARIEL: usize = 0x2a;
    const STASH_DURIEL: usize = 0x2c;
    const STASH_MEPHISTO: usize = 0x2e;
    const STASH_DIABLO: usize = 0x30;
    const STASH_BAAL: usize = 0x32;
    const STASH_DEATHS: usize = 0x34;
    const STASH_MAP_BOSS: usize = 0x36;
    const STASH_DUNGEON_BOSS: usize = 0x38;
    const STASH_SIZE_OFFSET: usize = 0x08;
    const STASH_CHECKSUM_OFFSET: usize = 0x0c;
    const STASH_ACCOUNT_BLOCK_OFFSET: usize = 0x12;
    const STASH_ACCOUNT_BLOCK_LEN: usize = 0x3e;
    const STASH_ACCOUNT_EXTRA_OFFSET: usize = 0x52;
    const STASH_ACCOUNT_EXTRA_LEN: usize = 0xdc;
    const STASH_ITEM_SECTION_OFFSET: usize = STASH_ACCOUNT_EXTRA_OFFSET + STASH_ACCOUNT_EXTRA_LEN;

    fn diablo_window() -> Result<HWND, String> {
        let class_name: Vec<u16> = OsStr::new("Diablo II")
            .encode_wide()
            .chain(Some(0))
            .collect();
        let hwnd = unsafe { FindWindowW(PCWSTR(class_name.as_ptr()), PCWSTR::null()) }
            .map_err(|_| "Diablo II window was not found.".to_string())?;
        if hwnd.0.is_null() {
            return Err("Diablo II window was not found.".to_string());
        }
        Ok(hwnd)
    }

    fn send_key(vk: VIRTUAL_KEY) -> Result<(), String> {
        let down = INPUT {
            r#type: INPUT_KEYBOARD,
            Anonymous: INPUT_0 {
                ki: KEYBDINPUT {
                    wVk: vk,
                    wScan: 0,
                    dwFlags: KEYBD_EVENT_FLAGS(0),
                    time: 0,
                    dwExtraInfo: 0,
                },
            },
        };
        let up = INPUT {
            r#type: INPUT_KEYBOARD,
            Anonymous: INPUT_0 {
                ki: KEYBDINPUT {
                    wVk: vk,
                    wScan: 0,
                    dwFlags: KEYEVENTF_KEYUP,
                    time: 0,
                    dwExtraInfo: 0,
                },
            },
        };
        let sent = unsafe { SendInput(&[down, up], mem::size_of::<INPUT>() as i32) };
        if sent != 2 {
            return Err("Failed to send key input to Diablo II.".to_string());
        }
        Ok(())
    }

    fn send_unicode_char(ch: char) -> Result<(), String> {
        for unit in ch.encode_utf16(&mut [0; 2]) {
            let down = INPUT {
                r#type: INPUT_KEYBOARD,
                Anonymous: INPUT_0 {
                    ki: KEYBDINPUT {
                        wVk: VIRTUAL_KEY(0),
                        wScan: *unit,
                        dwFlags: KEYEVENTF_UNICODE,
                        time: 0,
                        dwExtraInfo: 0,
                    },
                },
            };
            let up = INPUT {
                r#type: INPUT_KEYBOARD,
                Anonymous: INPUT_0 {
                    ki: KEYBDINPUT {
                        wVk: VIRTUAL_KEY(0),
                        wScan: *unit,
                        dwFlags: KEYEVENTF_UNICODE | KEYEVENTF_KEYUP,
                        time: 0,
                        dwExtraInfo: 0,
                    },
                },
            };
            let sent = unsafe { SendInput(&[down, up], mem::size_of::<INPUT>() as i32) };
            if sent != 2 {
                return Err("Failed to send text input to Diablo II.".to_string());
            }
        }
        Ok(())
    }

    fn send_chat_command(command: &str) -> Result<(), String> {
        let hwnd = diablo_window()?;
        unsafe {
            let _ = ShowWindow(hwnd, SW_RESTORE);
            if !SetForegroundWindow(hwnd).as_bool() {
                return Err("Failed to focus Diablo II window.".to_string());
            }
        }
        thread::sleep(Duration::from_millis(120));
        send_key(VK_RETURN)?;
        thread::sleep(Duration::from_millis(60));
        for ch in command.chars() {
            send_unicode_char(ch)?;
        }
        thread::sleep(Duration::from_millis(40));
        send_key(VK_RETURN)?;
        Ok(())
    }

    fn is_readable(protect: u32) -> bool {
        (protect & PAGE_NOACCESS.0) == 0 && (protect & PAGE_GUARD.0) == 0
    }

    fn parse_number(value: &str) -> Option<u64> {
        let digits: String = value.chars().filter(|ch| ch.is_ascii_digit()).collect();
        if digits.is_empty() {
            return None;
        }
        digits.parse::<u64>().ok()
    }

    fn update_boss_count(out: &mut HashMap<String, u64>, key: &str, value: u64) {
        out.entry(key.to_string())
            .and_modify(|current| *current = (*current).max(value))
            .or_insert(value);
    }

    fn read_player_data_ptr(ctx: &D2Context) -> Result<usize, String> {
        let player_unit = ctx
            .process
            .read_memory::<u32>(ctx.d2_client + d2client::PLAYER_UNIT)
            .unwrap_or(0);
        if player_unit == 0 {
            return Err("Enter a game before syncing account stats.".to_string());
        }

        let player_data = ctx
            .process
            .read_memory::<u32>(player_unit as usize + unit::UNIT_DATA)
            .unwrap_or(0);
        if player_data == 0 {
            return Err("Could not find the local player data block.".to_string());
        }

        Ok(player_data as usize)
    }

    fn read_account_u16(ctx: &D2Context, player_data: usize, offset: usize) -> Result<u64, String> {
        ctx.process
            .read_memory::<u16>(player_data + offset)
            .map(|value| value as u64)
    }

    fn read_account_u32(ctx: &D2Context, player_data: usize, offset: usize) -> Result<u64, String> {
        ctx.process
            .read_memory::<u32>(player_data + offset)
            .map(|value| value as u64)
    }

    fn read_player_name(ctx: &D2Context, player_data: usize) -> String {
        let Ok(bytes) = ctx.process.read_buffer(player_data, 32) else {
            return "unknown".to_string();
        };
        let end = bytes
            .iter()
            .position(|byte| *byte == 0)
            .unwrap_or(bytes.len());
        let name = String::from_utf8_lossy(&bytes[..end]).trim().to_string();
        if name
            .chars()
            .all(|ch| ch.is_ascii_alphanumeric() || ch == '_' || ch == '-')
            && !name.is_empty()
        {
            name
        } else {
            "unknown".to_string()
        }
    }

    pub(super) fn shared_stash_paths() -> Vec<PathBuf> {
        let mut paths = Vec::new();
        if let Ok(profile) = std::env::var("USERPROFILE") {
            paths.push(
                PathBuf::from(&profile)
                    .join("Saved Games")
                    .join("Diablo II")
                    .join("pd2_shared.stash"),
            );
            paths.push(
                PathBuf::from(&profile)
                    .join("Saved Games")
                    .join("Diablo II")
                    .join("pd2_hc_shared.stash"),
            );
            paths.push(
                PathBuf::from(&profile)
                    .join("Saved Games")
                    .join("ProjectD2")
                    .join("pd2_shared.stash"),
            );
            paths.push(
                PathBuf::from(&profile)
                    .join("Saved Games")
                    .join("ProjectD2")
                    .join("pd2_hc_shared.stash"),
            );
        }
        for base in [
            r"C:\Program Files (x86)\Diablo II\Save",
            r"C:\Program Files\Diablo II\Save",
        ] {
            paths.push(PathBuf::from(base).join("pd2_shared.stash"));
            paths.push(PathBuf::from(base).join("pd2_hc_shared.stash"));
        }
        paths
    }

    fn selected_or_detected_stash_path(stash_path: Option<String>) -> Option<PathBuf> {
        if let Some(path) = stash_path
            .map(|path| path.trim().to_string())
            .filter(|path| !path.is_empty())
        {
            return Some(PathBuf::from(path));
        }
        shared_stash_paths().into_iter().find(|path| path.is_file())
    }

    fn read_u16(bytes: &[u8], offset: usize) -> Result<u64, String> {
        if offset + 2 > bytes.len() {
            return Err("Shared stash account stats header is too short.".to_string());
        }
        Ok(u16::from_le_bytes([bytes[offset], bytes[offset + 1]]) as u64)
    }

    fn read_u32(bytes: &[u8], offset: usize) -> Result<u64, String> {
        if offset + 4 > bytes.len() {
            return Err("Shared stash account stats header is too short.".to_string());
        }
        Ok(u32::from_le_bytes([
            bytes[offset],
            bytes[offset + 1],
            bytes[offset + 2],
            bytes[offset + 3],
        ]) as u64)
    }

    fn read_u16_at(bytes: &[u8], offset: usize) -> u64 {
        u16::from_le_bytes([bytes[offset], bytes[offset + 1]]) as u64
    }

    fn read_u32_at(bytes: &[u8], offset: usize) -> u64 {
        u32::from_le_bytes([
            bytes[offset],
            bytes[offset + 1],
            bytes[offset + 2],
            bytes[offset + 3],
        ]) as u64
    }

    fn write_u32_at(bytes: &mut [u8], offset: usize, value: u32) -> Result<(), String> {
        if offset + 4 > bytes.len() {
            return Err("Shared stash account stats header is too short.".to_string());
        }
        bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        Ok(())
    }

    fn compute_stash_checksum(bytes: &[u8]) -> u32 {
        let mut checksum: u32 = 0;
        for (offset, byte) in bytes.iter().enumerate() {
            let value = if (STASH_CHECKSUM_OFFSET..STASH_CHECKSUM_OFFSET + 4).contains(&offset) {
                0
            } else {
                *byte as u32
            };
            let signed = checksum as i32;
            let sign = if signed < 0 { -1i32 } else { 0i32 };
            checksum = value
                .wrapping_sub(sign as u32)
                .wrapping_add(checksum.wrapping_mul(2));
        }
        checksum
    }

    fn validate_stash_checksum(bytes: &[u8]) -> Result<(), String> {
        if bytes.len() < STASH_CHECKSUM_OFFSET + 4 {
            return Err("Shared stash account stats header is too short.".to_string());
        }
        let stored = read_u32_at(bytes, STASH_CHECKSUM_OFFSET) as u32;
        let computed = compute_stash_checksum(bytes);
        if stored != computed {
            return Err(format!(
                "Shared stash checksum mismatch: stored=0x{stored:08x} computed=0x{computed:08x}"
            ));
        }
        Ok(())
    }

    fn game_is_running() -> bool {
        D2Context::new().is_ok()
    }

    pub(super) fn read_accountstats_stash(
        stash_path: Option<String>,
    ) -> Result<KillSyncResult, String> {
        let Some(path) = selected_or_detected_stash_path(stash_path) else {
            return Err("No pd2_shared.stash file was found.".to_string());
        };
        if !path.is_file() {
            return Err(format!(
                "Selected shared stash file does not exist: {}",
                path.display()
            ));
        }
        let bytes = fs::read(&path)
            .map_err(|e| format!("Failed to read shared stash {}: {}", path.display(), e))?;
        if bytes.len() < 0x3a || bytes.get(0..4) != Some(&[0x55, 0xbb, 0x55, 0xbb]) {
            return Err(format!(
                "Shared stash did not look like the expected PD2 account stats file: {}",
                path.display()
            ));
        }
        validate_stash_checksum(&bytes)?;

        let total_kills = read_u32(&bytes, STASH_MONSTER_KILLS)?;
        let dclone_any = read_u16(&bytes, STASH_DCLONE_ANY)?;
        let dclone_t2 = read_u16(&bytes, STASH_DCLONE_T2)?;
        let rathma_any = read_u16(&bytes, STASH_RATHMA_ANY)?;
        let rathma_t2 = read_u16(&bytes, STASH_RATHMA_T2)?;
        let lucion_any = read_u16(&bytes, STASH_LUCION_ANY)?;
        let lucion_t2 = read_u16(&bytes, STASH_LUCION_T2)?;
        let uber_tristram = read_u16(&bytes, STASH_UBER_TRISTRAM)?;
        let uber_ancients = read_u16(&bytes, STASH_UBER_ANCIENTS)?;
        let andariel = read_u16(&bytes, STASH_ANDARIEL)?;
        let duriel = read_u16(&bytes, STASH_DURIEL)?;
        let mephisto = read_u16(&bytes, STASH_MEPHISTO)?;
        let diablo = read_u16(&bytes, STASH_DIABLO)?;
        let baal = read_u16(&bytes, STASH_BAAL)?;
        let deaths = read_u16(&bytes, STASH_DEATHS)?;
        let map_boss = read_u16(&bytes, STASH_MAP_BOSS)?;
        let dungeon_boss = read_u16(&bytes, STASH_DUNGEON_BOSS)?;
        let time_played = read_u32(&bytes, STASH_TIME_PLAYED)?;

        let mut boss_kills = HashMap::new();
        update_boss_count(&mut boss_kills, "DClone:Any", dclone_any);
        update_boss_count(&mut boss_kills, "DClone:T2", dclone_t2);
        update_boss_count(&mut boss_kills, "Rathma:Any", rathma_any);
        update_boss_count(&mut boss_kills, "Rathma:T2", rathma_t2);
        update_boss_count(&mut boss_kills, "Lucion:Any", lucion_any);
        update_boss_count(&mut boss_kills, "Lucion:T2", lucion_t2);
        update_boss_count(&mut boss_kills, "Uber Tristram:Any", uber_tristram);
        update_boss_count(&mut boss_kills, "Uber Ancients:Any", uber_ancients);
        update_boss_count(&mut boss_kills, "Map Boss:Any", map_boss);
        update_boss_count(&mut boss_kills, "Dungeon Boss:Any", dungeon_boss);
        update_boss_count(&mut boss_kills, "Andariel:Any", andariel);
        update_boss_count(&mut boss_kills, "Duriel:Any", duriel);
        update_boss_count(&mut boss_kills, "Mephisto:Any", mephisto);
        update_boss_count(&mut boss_kills, "Diablo:Any", diablo);
        update_boss_count(&mut boss_kills, "Baal:Any", baal);

        Ok(KillSyncResult {
            total_kills,
            boss_kills,
            matched_text: format!(
                "shared stash: {} time={} deaths={} kills={} dclone={}/{} rathma={}/{} lucion={}/{} uber_trist={} uber_anc={} map={} dungeon={} acts={}/{}/{}/{}/{}",
                path.display(),
                time_played,
                deaths,
                total_kills,
                dclone_any,
                dclone_t2,
                rathma_any,
                rathma_t2,
                lucion_any,
                lucion_t2,
                uber_tristram,
                uber_ancients,
                map_boss,
                dungeon_boss,
                andariel,
                duriel,
                mephisto,
                diablo,
                baal
            ),
        })
    }

    fn read_accountstats_direct(ctx: &D2Context) -> Result<KillSyncResult, String> {
        let player_data = read_player_data_ptr(ctx)?;
        let player_name = read_player_name(ctx, player_data);
        let total_kills = read_account_u32(ctx, player_data, ACCOUNT_MONSTER_KILLS)?;
        if total_kills > 1_000_000_000 {
            return Err(format!(
                "Account stats pointer looked invalid: Monster Kills was {}.",
                total_kills
            ));
        }

        let dclone_any = read_account_u16(ctx, player_data, ACCOUNT_DCLONE_ANY)?;
        let dclone_t2 = read_account_u16(ctx, player_data, ACCOUNT_DCLONE_T2)?;
        let rathma_any = read_account_u16(ctx, player_data, ACCOUNT_RATHMA_ANY)?;
        let rathma_t2 = read_account_u16(ctx, player_data, ACCOUNT_RATHMA_T2)?;
        let lucion_any = read_account_u16(ctx, player_data, ACCOUNT_LUCION_ANY)?;
        let lucion_t2 = read_account_u16(ctx, player_data, ACCOUNT_LUCION_T2)?;
        let uber_tristram = read_account_u16(ctx, player_data, ACCOUNT_UBER_TRISTRAM)?;
        let uber_ancients = read_account_u16(ctx, player_data, ACCOUNT_UBER_ANCIENTS)?;
        let map_boss = read_account_u16(ctx, player_data, ACCOUNT_MAP_BOSS)?;
        let dungeon_boss = read_account_u16(ctx, player_data, ACCOUNT_DUNGEON_BOSS)?;
        let andariel = read_account_u16(ctx, player_data, ACCOUNT_ANDARIEL)?;
        let duriel = read_account_u16(ctx, player_data, ACCOUNT_DURIEL)?;
        let mephisto = read_account_u16(ctx, player_data, ACCOUNT_MEPHISTO)?;
        let diablo = read_account_u16(ctx, player_data, ACCOUNT_DIABLO)?;
        let baal = read_account_u16(ctx, player_data, ACCOUNT_BAAL)?;

        let mut boss_kills = HashMap::new();
        update_boss_count(&mut boss_kills, "DClone:Any", dclone_any);
        update_boss_count(&mut boss_kills, "DClone:T2", dclone_t2);
        update_boss_count(&mut boss_kills, "Rathma:Any", rathma_any);
        update_boss_count(&mut boss_kills, "Rathma:T2", rathma_t2);
        update_boss_count(&mut boss_kills, "Lucion:Any", lucion_any);
        update_boss_count(&mut boss_kills, "Lucion:T2", lucion_t2);
        update_boss_count(&mut boss_kills, "Uber Tristram:Any", uber_tristram);
        update_boss_count(&mut boss_kills, "Uber Ancients:Any", uber_ancients);
        update_boss_count(&mut boss_kills, "Map Boss:Any", map_boss);
        update_boss_count(&mut boss_kills, "Dungeon Boss:Any", dungeon_boss);
        update_boss_count(&mut boss_kills, "Andariel:Any", andariel);
        update_boss_count(&mut boss_kills, "Duriel:Any", duriel);
        update_boss_count(&mut boss_kills, "Mephisto:Any", mephisto);
        update_boss_count(&mut boss_kills, "Diablo:Any", diablo);
        update_boss_count(&mut boss_kills, "Baal:Any", baal);

        let time_played = read_account_u32(ctx, player_data, ACCOUNT_TIME_PLAYED).unwrap_or(0);
        let deaths = read_account_u16(ctx, player_data, ACCOUNT_DEATHS).unwrap_or(0);

        Ok(KillSyncResult {
            total_kills,
            boss_kills,
            matched_text: format!(
                "direct account stats: player={} data=0x{:x} time={} deaths={} kills={} dclone={}/{} rathma={}/{} lucion={}/{} uber_trist={} uber_anc={} map={} dungeon={} acts={}/{}/{}/{}/{}",
                player_name,
                player_data,
                time_played,
                deaths,
                total_kills,
                dclone_any,
                dclone_t2,
                rathma_any,
                rathma_t2,
                lucion_any,
                lucion_t2,
                uber_tristram,
                uber_ancients,
                map_boss,
                dungeon_boss,
                andariel,
                duriel,
                mephisto,
                diablo,
                baal
            ),
        })
    }

    fn read_accountstats_from_base(
        ctx: &D2Context,
        base: usize,
        source: &str,
    ) -> Result<KillSyncResult, String> {
        let total_kills = read_account_u32(ctx, base, ACCOUNT_MONSTER_KILLS)?;
        let time_played = read_account_u32(ctx, base, ACCOUNT_TIME_PLAYED).unwrap_or(0);
        let deaths = read_account_u16(ctx, base, ACCOUNT_DEATHS).unwrap_or(0);
        if total_kills > 1_000_000_000 || time_played > 10_000_000_000 || deaths > 1_000_000 {
            return Err(format!(
                "Account stats base looked invalid: base=0x{:x} kills={} time={} deaths={}.",
                base, total_kills, time_played, deaths
            ));
        }
        if total_kills == 0 && time_played == 0 {
            return Err(format!("Account stats base was empty: base=0x{:x}.", base));
        }

        let dclone_any = read_account_u16(ctx, base, ACCOUNT_DCLONE_ANY)?;
        let dclone_t2 = read_account_u16(ctx, base, ACCOUNT_DCLONE_T2)?;
        let rathma_any = read_account_u16(ctx, base, ACCOUNT_RATHMA_ANY)?;
        let rathma_t2 = read_account_u16(ctx, base, ACCOUNT_RATHMA_T2)?;
        let lucion_any = read_account_u16(ctx, base, ACCOUNT_LUCION_ANY)?;
        let lucion_t2 = read_account_u16(ctx, base, ACCOUNT_LUCION_T2)?;
        let uber_tristram = read_account_u16(ctx, base, ACCOUNT_UBER_TRISTRAM)?;
        let uber_ancients = read_account_u16(ctx, base, ACCOUNT_UBER_ANCIENTS)?;
        let map_boss = read_account_u16(ctx, base, ACCOUNT_MAP_BOSS)?;
        let dungeon_boss = read_account_u16(ctx, base, ACCOUNT_DUNGEON_BOSS)?;
        let andariel = read_account_u16(ctx, base, ACCOUNT_ANDARIEL)?;
        let duriel = read_account_u16(ctx, base, ACCOUNT_DURIEL)?;
        let mephisto = read_account_u16(ctx, base, ACCOUNT_MEPHISTO)?;
        let diablo = read_account_u16(ctx, base, ACCOUNT_DIABLO)?;
        let baal = read_account_u16(ctx, base, ACCOUNT_BAAL)?;

        let mut boss_kills = HashMap::new();
        update_boss_count(&mut boss_kills, "DClone:Any", dclone_any);
        update_boss_count(&mut boss_kills, "DClone:T2", dclone_t2);
        update_boss_count(&mut boss_kills, "Rathma:Any", rathma_any);
        update_boss_count(&mut boss_kills, "Rathma:T2", rathma_t2);
        update_boss_count(&mut boss_kills, "Lucion:Any", lucion_any);
        update_boss_count(&mut boss_kills, "Lucion:T2", lucion_t2);
        update_boss_count(&mut boss_kills, "Uber Tristram:Any", uber_tristram);
        update_boss_count(&mut boss_kills, "Uber Ancients:Any", uber_ancients);
        update_boss_count(&mut boss_kills, "Map Boss:Any", map_boss);
        update_boss_count(&mut boss_kills, "Dungeon Boss:Any", dungeon_boss);
        update_boss_count(&mut boss_kills, "Andariel:Any", andariel);
        update_boss_count(&mut boss_kills, "Duriel:Any", duriel);
        update_boss_count(&mut boss_kills, "Mephisto:Any", mephisto);
        update_boss_count(&mut boss_kills, "Diablo:Any", diablo);
        update_boss_count(&mut boss_kills, "Baal:Any", baal);

        Ok(KillSyncResult {
            total_kills,
            boss_kills,
            matched_text: format!(
                "{} account stats: base=0x{:x} time={} deaths={} kills={} dclone={}/{} rathma={}/{} lucion={}/{} uber_trist={} uber_anc={} map={} dungeon={} acts={}/{}/{}/{}/{}",
                source,
                base,
                time_played,
                deaths,
                total_kills,
                dclone_any,
                dclone_t2,
                rathma_any,
                rathma_t2,
                lucion_any,
                lucion_t2,
                uber_tristram,
                uber_ancients,
                map_boss,
                dungeon_boss,
                andariel,
                duriel,
                mephisto,
                diablo,
                baal
            ),
        })
    }

    pub(super) fn read_accountstats_live_memory() -> Result<KillSyncResult, String> {
        let ctx = D2Context::new()?;
        let player_data = read_player_data_ptr(&ctx)?;
        let stash = read_accountstats_stash(None).ok();
        let baseline_kills = stash.as_ref().map(|result| result.total_kills).unwrap_or(0);
        let candidates = [
            player_data.saturating_sub(0x4000),
            player_data & !0xffff,
            player_data.saturating_sub(0x3000),
            player_data.saturating_sub(0x5000),
        ];

        let mut errors = Vec::new();
        for base in candidates {
            match read_accountstats_from_base(&ctx, base, "live") {
                Ok(mut result) => {
                    result.matched_text =
                        format!("{} player_data=0x{:x}", result.matched_text, player_data);
                    if result.total_kills < baseline_kills
                        || result.total_kills > baseline_kills.saturating_add(MAX_LIVE_KILL_DELTA)
                    {
                        errors.push(format!(
                            "live candidate 0x{base:x} was outside stash baseline range: live_kills={} stash_kills={}",
                            result.total_kills, baseline_kills
                        ));
                        continue;
                    }
                    result.boss_kills.clear();
                    return Ok(result);
                }
                Err(error) => errors.push(error),
            }
        }

        Err(format!(
            "Could not find live account stats near player data: {}",
            errors.join(" | ")
        ))
    }

    fn score_candidate(
        total_kills: u64,
        time_played: u64,
        deaths: u64,
        boss_kills: &HashMap<String, u64>,
        stash: Option<&KillSyncResult>,
        baseline_kills: u64,
    ) -> Option<u32> {
        if total_kills < baseline_kills || total_kills > baseline_kills + MAX_LIVE_KILL_DELTA {
            return None;
        }
        if time_played > 10_000_000_000 || deaths > 1_000_000 {
            return None;
        }

        let mut score = 1;
        if let Some(stash) = stash {
            if total_kills >= stash.total_kills {
                score += 4;
            }
            if total_kills == stash.total_kills {
                score += 2;
            }
            for (key, stash_value) in &stash.boss_kills {
                let value = boss_kills.get(key).copied().unwrap_or(0);
                if value == *stash_value {
                    score += 1;
                } else if value < *stash_value || value > stash_value.saturating_add(10_000) {
                    return None;
                }
            }
        }
        if time_played > 0 {
            score += 1;
        }
        Some(score)
    }

    fn candidate_from_layout(
        bytes: &[u8],
        offset: usize,
        address: usize,
        stash: Option<&KillSyncResult>,
        baseline_kills: u64,
    ) -> Option<AccountStatsMemoryCandidate> {
        if offset < 4 || offset + (ACCOUNT_DUNGEON_BOSS - ACCOUNT_MONSTER_KILLS) + 2 > bytes.len() {
            return None;
        }

        let total_kills = read_u32_at(bytes, offset);
        let time_played = read_u32_at(bytes, offset - 4);
        let dclone_any = read_u16_at(bytes, offset + ACCOUNT_DCLONE_ANY - ACCOUNT_MONSTER_KILLS);
        let dclone_t2 = read_u16_at(bytes, offset + ACCOUNT_DCLONE_T2 - ACCOUNT_MONSTER_KILLS);
        let rathma_any = read_u16_at(bytes, offset + ACCOUNT_RATHMA_ANY - ACCOUNT_MONSTER_KILLS);
        let rathma_t2 = read_u16_at(bytes, offset + ACCOUNT_RATHMA_T2 - ACCOUNT_MONSTER_KILLS);
        let lucion_any = read_u16_at(bytes, offset + ACCOUNT_LUCION_ANY - ACCOUNT_MONSTER_KILLS);
        let lucion_t2 = read_u16_at(bytes, offset + ACCOUNT_LUCION_T2 - ACCOUNT_MONSTER_KILLS);
        let uber_tristram = read_u16_at(
            bytes,
            offset + ACCOUNT_UBER_TRISTRAM - ACCOUNT_MONSTER_KILLS,
        );
        let uber_ancients = read_u16_at(
            bytes,
            offset + ACCOUNT_UBER_ANCIENTS - ACCOUNT_MONSTER_KILLS,
        );
        let andariel = read_u16_at(bytes, offset + ACCOUNT_ANDARIEL - ACCOUNT_MONSTER_KILLS);
        let duriel = read_u16_at(bytes, offset + ACCOUNT_DURIEL - ACCOUNT_MONSTER_KILLS);
        let mephisto = read_u16_at(bytes, offset + ACCOUNT_MEPHISTO - ACCOUNT_MONSTER_KILLS);
        let diablo = read_u16_at(bytes, offset + ACCOUNT_DIABLO - ACCOUNT_MONSTER_KILLS);
        let baal = read_u16_at(bytes, offset + ACCOUNT_BAAL - ACCOUNT_MONSTER_KILLS);
        let deaths = read_u16_at(bytes, offset + ACCOUNT_DEATHS - ACCOUNT_MONSTER_KILLS);
        let map_boss = read_u16_at(bytes, offset + ACCOUNT_MAP_BOSS - ACCOUNT_MONSTER_KILLS);
        let dungeon_boss =
            read_u16_at(bytes, offset + ACCOUNT_DUNGEON_BOSS - ACCOUNT_MONSTER_KILLS);

        let mut boss_kills = HashMap::new();
        update_boss_count(&mut boss_kills, "DClone:Any", dclone_any);
        update_boss_count(&mut boss_kills, "DClone:T2", dclone_t2);
        update_boss_count(&mut boss_kills, "Rathma:Any", rathma_any);
        update_boss_count(&mut boss_kills, "Rathma:T2", rathma_t2);
        update_boss_count(&mut boss_kills, "Lucion:Any", lucion_any);
        update_boss_count(&mut boss_kills, "Lucion:T2", lucion_t2);
        update_boss_count(&mut boss_kills, "Uber Tristram:Any", uber_tristram);
        update_boss_count(&mut boss_kills, "Uber Ancients:Any", uber_ancients);
        update_boss_count(&mut boss_kills, "Map Boss:Any", map_boss);
        update_boss_count(&mut boss_kills, "Dungeon Boss:Any", dungeon_boss);
        update_boss_count(&mut boss_kills, "Andariel:Any", andariel);
        update_boss_count(&mut boss_kills, "Duriel:Any", duriel);
        update_boss_count(&mut boss_kills, "Mephisto:Any", mephisto);
        update_boss_count(&mut boss_kills, "Diablo:Any", diablo);
        update_boss_count(&mut boss_kills, "Baal:Any", baal);

        let score = score_candidate(
            total_kills,
            time_played,
            deaths,
            &boss_kills,
            stash,
            baseline_kills,
        )?;
        Some(AccountStatsMemoryCandidate {
            address: format!("0x{address:08x}"),
            total_kills,
            time_played,
            deaths,
            score,
            summary: format!(
                "kills={} time={} deaths={} dclone={}/{} rathma={}/{} lucion={}/{} uber_trist={} uber_anc={} map={} dungeon={} acts={}/{}/{}/{}/{}",
                total_kills,
                time_played,
                deaths,
                dclone_any,
                dclone_t2,
                rathma_any,
                rathma_t2,
                lucion_any,
                lucion_t2,
                uber_tristram,
                uber_ancients,
                map_boss,
                dungeon_boss,
                andariel,
                duriel,
                mephisto,
                diablo,
                baal
            ),
        })
    }

    fn scan_live_accountstats_candidates(
        ctx: &D2Context,
        stash: Option<&KillSyncResult>,
        baseline_kills: u64,
    ) -> Vec<AccountStatsMemoryCandidate> {
        let mut address = 0x10_000usize;
        let mut candidates = Vec::new();

        while address < MAX_SCAN_ADDRESS && candidates.len() < 200 {
            let mut info = MEMORY_BASIC_INFORMATION::default();
            let queried = unsafe {
                VirtualQueryEx(
                    ctx.process.handle,
                    Some(address as *const _),
                    &mut info,
                    mem::size_of::<MEMORY_BASIC_INFORMATION>(),
                )
            };
            if queried == 0 {
                address = address.saturating_add(0x10000);
                continue;
            }

            let base = info.BaseAddress as usize;
            let size = info.RegionSize;
            let next = base
                .saturating_add(size)
                .max(address.saturating_add(0x1000));

            if info.State == MEM_COMMIT && is_readable(info.Protect.0) && size >= 0x40 {
                let mut offset = 0usize;
                while offset < size {
                    let read_size = (size - offset).min(MAX_REGION_READ);
                    if let Ok(bytes) = ctx.process.read_buffer(base + offset, read_size) {
                        let mut i = 4usize;
                        while i + 0x30 < bytes.len() {
                            if let Some(candidate) = candidate_from_layout(
                                &bytes,
                                i,
                                base + offset + i,
                                stash,
                                baseline_kills,
                            ) {
                                candidates.push(candidate);
                                if candidates.len() >= 200 {
                                    break;
                                }
                            }
                            i += 4;
                        }
                    }
                    offset += read_size;
                    if candidates.len() >= 200 {
                        break;
                    }
                }
            }

            address = next;
        }

        candidates.sort_by(|a, b| {
            b.score
                .cmp(&a.score)
                .then_with(|| b.total_kills.cmp(&a.total_kills))
                .then_with(|| a.address.cmp(&b.address))
        });
        candidates.truncate(20);
        candidates
    }

    fn candidate_from_exact_match(
        bytes: &[u8],
        value_offset: usize,
        address: usize,
        expected_kills: u64,
    ) -> AccountStatsMemoryCandidate {
        let window_start = value_offset.saturating_sub(0x80);
        let window_end = (value_offset + 0x90).min(bytes.len());
        let mut near_words = Vec::new();
        let mut cursor = window_start;
        while cursor + 4 <= window_end && near_words.len() < 24 {
            let value = read_u32_at(bytes, cursor);
            if value > 0 && value < 10_000_000 {
                near_words.push(format!(
                    "{:+#04x}={}",
                    cursor as isize - value_offset as isize,
                    value
                ));
            }
            cursor += 4;
        }

        let mut score = 1;
        if value_offset >= 4 {
            let before = read_u32_at(bytes, value_offset - 4);
            if before > 0 && before < 10_000_000_000 {
                score += 2;
            }
        }
        if value_offset + 0x28 < bytes.len() {
            let plausible_u16s = [
                value_offset + 4,
                value_offset + 8,
                value_offset + 12,
                value_offset + 16,
                value_offset + 20,
                value_offset + 24,
                value_offset + 28,
                value_offset + 32,
            ]
            .into_iter()
            .filter(|offset| read_u16_at(bytes, *offset) < 10_000)
            .count();
            score += plausible_u16s as u32;
        }

        AccountStatsMemoryCandidate {
            address: format!("0x{address:08x}"),
            total_kills: expected_kills,
            time_played: if value_offset >= 4 {
                read_u32_at(bytes, value_offset - 4)
            } else {
                0
            },
            deaths: if value_offset + 0x1e < bytes.len() {
                read_u16_at(bytes, value_offset + 0x1e)
            } else {
                0
            },
            score,
            summary: format!(
                "exact kills={} nearby u32 [{}]",
                expected_kills,
                near_words.join(", ")
            ),
        }
    }

    fn scan_exact_kill_value_candidates(
        ctx: &D2Context,
        expected_kills: u64,
    ) -> Vec<AccountStatsMemoryCandidate> {
        if expected_kills == 0 || expected_kills > u32::MAX as u64 {
            return Vec::new();
        }

        let needle = (expected_kills as u32).to_le_bytes();
        let mut address = 0x10_000usize;
        let mut candidates = Vec::new();

        while address < MAX_SCAN_ADDRESS && candidates.len() < 200 {
            let mut info = MEMORY_BASIC_INFORMATION::default();
            let queried = unsafe {
                VirtualQueryEx(
                    ctx.process.handle,
                    Some(address as *const _),
                    &mut info,
                    mem::size_of::<MEMORY_BASIC_INFORMATION>(),
                )
            };
            if queried == 0 {
                address = address.saturating_add(0x10000);
                continue;
            }

            let base = info.BaseAddress as usize;
            let size = info.RegionSize;
            let next = base
                .saturating_add(size)
                .max(address.saturating_add(0x1000));

            if info.State == MEM_COMMIT && is_readable(info.Protect.0) && size >= 4 {
                let mut offset = 0usize;
                while offset < size {
                    let read_size = (size - offset).min(MAX_REGION_READ);
                    if let Ok(bytes) = ctx.process.read_buffer(base + offset, read_size) {
                        for (i, window) in bytes.windows(4).enumerate() {
                            if window == needle {
                                candidates.push(candidate_from_exact_match(
                                    &bytes,
                                    i,
                                    base + offset + i,
                                    expected_kills,
                                ));
                                if candidates.len() >= 200 {
                                    break;
                                }
                            }
                        }
                    }
                    offset += read_size;
                    if candidates.len() >= 200 {
                        break;
                    }
                }
            }

            address = next;
        }

        candidates.sort_by(|a, b| {
            b.score
                .cmp(&a.score)
                .then_with(|| a.address.cmp(&b.address))
        });
        candidates.truncate(40);
        candidates
    }

    fn collect_accountstats_matches(
        text: &str,
        current_kills: u64,
        kill_matches: &mut Vec<(u64, String)>,
        boss_kills: &mut HashMap<String, u64>,
    ) {
        let patterns = [
            r"(?i)monster\s+kills?\s*[:=\-]?\s*([0-9][0-9,\.]{0,15})",
            r"(?i)([0-9][0-9,\.]{0,15})\s*monster\s+kills?",
        ];
        for pattern in patterns {
            let Ok(re) = Regex::new(pattern) else {
                continue;
            };
            for capture in re.captures_iter(text) {
                let Some(value) = capture.get(1).and_then(|m| parse_number(m.as_str())) else {
                    continue;
                };
                if value < current_kills {
                    continue;
                }
                if value > 1_000_000_000 {
                    continue;
                }
                let matched = capture
                    .get(0)
                    .map(|m| m.as_str().trim().to_string())
                    .unwrap_or_else(|| format!("Monster Kills {}", value));
                kill_matches.push((value, matched));
            }
        }

        let solo_bosses = [
            ("Diablo Clone", "DClone"),
            ("Rathma", "Rathma"),
            ("Lucion", "Lucion"),
            ("Kiln", "Kiln"),
        ];
        for (label, key) in solo_bosses {
            let pattern = format!(
                r"(?i)solo\s+{}\s+kills?\s*[:=\-]?\s*([0-9][0-9,\.]{{0,15}})(?:\s*,\s*tier\s*2\s*[:=\-]?\s*([0-9][0-9,\.]{{0,15}}))?",
                regex::escape(label)
            );
            let Ok(re) = Regex::new(&pattern) else {
                continue;
            };
            for capture in re.captures_iter(text) {
                if let Some(value) = capture.get(1).and_then(|m| parse_number(m.as_str())) {
                    update_boss_count(boss_kills, &format!("{}:Any", key), value);
                }
                if let Some(value) = capture.get(2).and_then(|m| parse_number(m.as_str())) {
                    update_boss_count(boss_kills, &format!("{}:T2", key), value);
                }
            }
        }

        let simple_bosses = [
            ("Uber Tristram", "Uber Tristram"),
            ("Uber Ancients", "Uber Ancients"),
            ("Map Boss", "Map Boss"),
            ("Dungeon Boss", "Dungeon Boss"),
        ];
        for (label, key) in simple_bosses {
            let pattern = format!(
                r"(?i){}\s+kills?\s*[:=\-]?\s*([0-9][0-9,\.]{{0,15}})",
                regex::escape(label)
            );
            let Ok(re) = Regex::new(&pattern) else {
                continue;
            };
            for capture in re.captures_iter(text) {
                if let Some(value) = capture.get(1).and_then(|m| parse_number(m.as_str())) {
                    update_boss_count(boss_kills, &format!("{}:Any", key), value);
                }
            }
        }

        for boss in ["Andariel", "Duriel", "Mephisto", "Diablo", "Baal"] {
            let pattern = format!(
                r"(?i){}\s*[:=\-]\s*([0-9][0-9,\.]{{0,15}})",
                regex::escape(boss)
            );
            let Ok(re) = Regex::new(&pattern) else {
                continue;
            };
            for capture in re.captures_iter(text) {
                if let Some(value) = capture.get(1).and_then(|m| parse_number(m.as_str())) {
                    update_boss_count(boss_kills, &format!("{}:Any", boss), value);
                }
            }
        }
    }

    fn collect_kill_matches(text: &str, current_kills: u64, out: &mut Vec<(u64, String)>) {
        let patterns = [
            r"(?i)(?:total\s+)?(?:all\s+)?kills?\s*(?:all)?\s*[:=\-]?\s*([0-9][0-9,\.]{0,15})",
            r"(?i)([0-9][0-9,\.]{0,15})\s*(?:total\s+)?(?:all\s+)?kills?",
        ];
        for pattern in patterns {
            let Ok(re) = Regex::new(pattern) else {
                continue;
            };
            for capture in re.captures_iter(text) {
                let Some(value) = capture.get(1).and_then(|m| parse_number(m.as_str())) else {
                    continue;
                };
                if value < current_kills {
                    continue;
                }
                if value > 100_000_000 {
                    continue;
                }
                let matched = capture
                    .get(0)
                    .map(|m| m.as_str().trim().to_string())
                    .unwrap_or_else(|| format!("kills {}", value));
                out.push((value, matched));
            }
        }
    }

    fn ascii_text(bytes: &[u8]) -> String {
        bytes
            .iter()
            .map(|b| {
                let ch = *b as char;
                if ch.is_ascii_graphic() || ch.is_ascii_whitespace() {
                    ch
                } else {
                    ' '
                }
            })
            .collect()
    }

    fn utf16_text(bytes: &[u8]) -> String {
        let wide: Vec<u16> = bytes
            .chunks_exact(2)
            .map(|chunk| u16::from_le_bytes([chunk[0], chunk[1]]))
            .map(|unit| {
                if unit == 0 || (unit >= 0x20 && unit <= 0x7e) {
                    unit
                } else {
                    0x20
                }
            })
            .collect();
        String::from_utf16_lossy(&wide)
    }

    fn scan_game_memory(
        ctx: &D2Context,
        current_kills: u64,
        accountstats_only: bool,
    ) -> Result<KillSyncResult, String> {
        let mut address = 0x10_000usize;
        let mut matches: Vec<(u64, String)> = Vec::new();
        let mut boss_kills: HashMap<String, u64> = HashMap::new();

        while address < MAX_SCAN_ADDRESS {
            let mut info = MEMORY_BASIC_INFORMATION::default();
            let queried = unsafe {
                VirtualQueryEx(
                    ctx.process.handle,
                    Some(address as *const _),
                    &mut info,
                    mem::size_of::<MEMORY_BASIC_INFORMATION>(),
                )
            };
            if queried == 0 {
                address = address.saturating_add(0x10000);
                continue;
            }

            let base = info.BaseAddress as usize;
            let size = info.RegionSize;
            let next = base
                .saturating_add(size)
                .max(address.saturating_add(0x1000));

            if info.State == MEM_COMMIT && is_readable(info.Protect.0) && size > 0 {
                let mut offset = 0usize;
                while offset < size {
                    let read_size = (size - offset).min(MAX_REGION_READ);
                    if let Ok(bytes) = ctx.process.read_buffer(base + offset, read_size) {
                        if bytes.windows(4).any(|w| w.eq_ignore_ascii_case(b"kill")) {
                            let text = ascii_text(&bytes);
                            if accountstats_only {
                                collect_accountstats_matches(
                                    &text,
                                    current_kills,
                                    &mut matches,
                                    &mut boss_kills,
                                );
                            } else {
                                collect_kill_matches(&text, current_kills, &mut matches);
                            }
                        }
                        if bytes.windows(8).any(|w| {
                            w == [b'k', 0, b'i', 0, b'l', 0, b'l', 0]
                                || w == [b'K', 0, b'i', 0, b'l', 0, b'l', 0]
                        }) {
                            let text = utf16_text(&bytes);
                            if accountstats_only {
                                collect_accountstats_matches(
                                    &text,
                                    current_kills,
                                    &mut matches,
                                    &mut boss_kills,
                                );
                            } else {
                                collect_kill_matches(&text, current_kills, &mut matches);
                            }
                        }
                    }
                    offset += read_size;
                }
            }

            address = next;
        }

        matches
            .into_iter()
            .max_by_key(|(value, _)| *value)
            .map(|(total_kills, matched_text)| KillSyncResult {
                total_kills,
                boss_kills,
                matched_text,
            })
            .ok_or_else(|| {
                let command = if accountstats_only { ".accountstats" } else { ".kills all" };
                format!("Could not find the {} Monster Kills result in Diablo II memory. Try again after the chat result is visible.", command)
            })
    }

    pub fn sync_kills_all(current_kills: u64) -> Result<KillSyncResult, String> {
        let ctx = D2Context::new()?;
        let player_unit = ctx
            .process
            .read_memory::<u32>(ctx.d2_client + d2client::PLAYER_UNIT)
            .unwrap_or(0);
        if player_unit == 0 {
            return Err("Enter a game before syncing kills.".to_string());
        }

        send_chat_command(".kills all")?;
        thread::sleep(Duration::from_millis(900));
        let result = scan_game_memory(&ctx, current_kills, false)?;
        log_info(&format!(
            "Synced .kills all: {} ({})",
            result.total_kills, result.matched_text
        ));
        Ok(result)
    }

    pub fn sync_accountstats_kills(
        _current_kills: u64,
        stash_path: Option<String>,
    ) -> Result<KillSyncResult, String> {
        let result = match read_accountstats_stash(stash_path) {
            Ok(result) => result,
            Err(stash_error) => match read_accountstats_live_memory() {
                Ok(mut result) => {
                    result.matched_text = format!(
                        "{}; stash fallback reason: {}",
                        result.matched_text, stash_error
                    );
                    result
                }
                Err(live_error) => {
                    let ctx = D2Context::new()?;
                    let mut result = read_accountstats_direct(&ctx)?;
                    result.boss_kills.clear();
                    result.matched_text = format!(
                        "{}; stash fallback reason: {}; live fallback reason: {}",
                        result.matched_text, stash_error, live_error
                    );
                    result
                }
            },
        };
        log_info(&format!(
            "Synced account stats Monster Kills: {} ({})",
            result.total_kills, result.matched_text
        ));
        Ok(result)
    }

    pub fn debug_accountstats_live(
        current_kills: u64,
        stash_path: Option<String>,
        expected_kills: Option<u64>,
    ) -> Result<AccountStatsLiveDebug, String> {
        let stash = read_accountstats_stash(stash_path).ok();
        let mut errors = Vec::new();
        let ctx = D2Context::new()?;
        let direct = match read_accountstats_direct(&ctx) {
            Ok(result) => Some(result),
            Err(error) => {
                errors.push(format!("direct: {error}"));
                None
            }
        };
        let baseline_kills = [
            current_kills,
            stash.as_ref().map(|result| result.total_kills).unwrap_or(0),
            direct
                .as_ref()
                .map(|result| result.total_kills)
                .unwrap_or(0),
        ]
        .into_iter()
        .max()
        .unwrap_or(0);
        let candidates = scan_live_accountstats_candidates(&ctx, stash.as_ref(), baseline_kills);
        let exact_matches = expected_kills
            .map(|expected| scan_exact_kill_value_candidates(&ctx, expected))
            .unwrap_or_default();
        Ok(AccountStatsLiveDebug {
            stash,
            direct,
            candidates,
            exact_matches,
            errors,
        })
    }

    pub fn reset_accountstats_stash(
        stash_path: Option<String>,
        backup_dir: PathBuf,
    ) -> Result<AccountStatsResetResult, String> {
        if game_is_running() {
            return Err("Close Diablo II before resetting account stats.".to_string());
        }

        let Some(path) = selected_or_detected_stash_path(stash_path) else {
            return Err("No pd2_shared.stash file was found.".to_string());
        };
        if !path.is_file() {
            return Err(format!(
                "Selected shared stash file does not exist: {}",
                path.display()
            ));
        }

        let previous = read_accountstats_stash(Some(path.to_string_lossy().to_string()))?;
        let mut bytes = fs::read(&path)
            .map_err(|e| format!("Failed to read shared stash {}: {}", path.display(), e))?;
        if bytes.len() < STASH_ITEM_SECTION_OFFSET + 2
            || bytes.get(0..4) != Some(&[0x55, 0xbb, 0x55, 0xbb])
            || bytes.get(STASH_ITEM_SECTION_OFFSET..STASH_ITEM_SECTION_OFFSET + 2)
                != Some(&[b'J', b'M'])
        {
            return Err(format!(
                "Shared stash did not look like the expected PD2 account stats file: {}",
                path.display()
            ));
        }

        let stamp = chrono::Local::now().format("%Y%m%d-%H%M%S");
        let backup_dir = backup_dir.join("accountstats-backups");
        fs::create_dir_all(&backup_dir).map_err(|e| {
            format!(
                "Failed to create account stats backup folder {}: {}",
                backup_dir.display(),
                e
            )
        })?;
        let backup_name = path
            .file_name()
            .and_then(|name| name.to_str())
            .unwrap_or("pd2_shared.stash");
        let backup_path = backup_dir.join(format!("{backup_name}.accountstats-backup-{stamp}"));
        fs::copy(&path, &backup_path)
            .map_err(|e| format!("Failed to create backup {}: {}", backup_path.display(), e))?;

        bytes[STASH_ACCOUNT_BLOCK_OFFSET..STASH_ACCOUNT_BLOCK_OFFSET + STASH_ACCOUNT_BLOCK_LEN]
            .fill(0);
        bytes[STASH_ACCOUNT_EXTRA_OFFSET..STASH_ACCOUNT_EXTRA_OFFSET + STASH_ACCOUNT_EXTRA_LEN]
            .fill(0);

        let size = bytes.len() as u32;
        write_u32_at(&mut bytes, STASH_SIZE_OFFSET, size)?;
        write_u32_at(&mut bytes, STASH_CHECKSUM_OFFSET, 0)?;
        let checksum = compute_stash_checksum(&bytes);
        write_u32_at(&mut bytes, STASH_CHECKSUM_OFFSET, checksum)?;
        validate_stash_checksum(&bytes)?;

        fs::write(&path, &bytes).map_err(|e| {
            format!(
                "Failed to write reset shared stash {}: {}",
                path.display(),
                e
            )
        })?;

        Ok(AccountStatsResetResult {
            stash_path: path.display().to_string(),
            backup_path: backup_path.display().to_string(),
            previous_total_kills: previous.total_kills,
            checksum: format!("0x{checksum:08x}"),
        })
    }
}

#[cfg(target_os = "windows")]
#[tauri::command]
pub fn sync_kills_all(current_kills: u64) -> Result<KillSyncResult, String> {
    windows_impl::sync_kills_all(current_kills)
}

#[cfg(target_os = "windows")]
#[tauri::command]
pub fn sync_accountstats_kills(
    current_kills: u64,
    stash_path: Option<String>,
) -> Result<KillSyncResult, String> {
    windows_impl::sync_accountstats_kills(current_kills, stash_path)
}

#[cfg(target_os = "windows")]
#[tauri::command]
pub fn debug_accountstats_live(
    current_kills: u64,
    stash_path: Option<String>,
    expected_kills: Option<u64>,
) -> Result<AccountStatsLiveDebug, String> {
    windows_impl::debug_accountstats_live(current_kills, stash_path, expected_kills)
}

#[cfg(target_os = "windows")]
#[tauri::command]
pub fn reset_accountstats_stash(
    app: tauri::AppHandle,
    stash_path: Option<String>,
) -> Result<AccountStatsResetResult, String> {
    let backup_dir = app
        .path()
        .app_data_dir()
        .map_err(|e| format!("Failed to resolve SoE Companion app data folder: {}", e))?;
    windows_impl::reset_accountstats_stash(stash_path, backup_dir)
}

#[cfg(not(target_os = "windows"))]
#[tauri::command]
pub fn sync_kills_all(_current_kills: u64) -> Result<KillSyncResult, String> {
    Err("Kill syncing is only supported on Windows.".to_string())
}

#[cfg(not(target_os = "windows"))]
#[tauri::command]
pub fn sync_accountstats_kills(
    _current_kills: u64,
    _stash_path: Option<String>,
) -> Result<KillSyncResult, String> {
    Err("Kill syncing is only supported on Windows.".to_string())
}

#[cfg(not(target_os = "windows"))]
#[tauri::command]
pub fn debug_accountstats_live(
    _current_kills: u64,
    _stash_path: Option<String>,
    _expected_kills: Option<u64>,
) -> Result<AccountStatsLiveDebug, String> {
    Err("Kill syncing is only supported on Windows.".to_string())
}

#[cfg(not(target_os = "windows"))]
#[tauri::command]
pub fn reset_accountstats_stash(
    _app: tauri::AppHandle,
    _stash_path: Option<String>,
) -> Result<AccountStatsResetResult, String> {
    Err("Account stats reset is only supported on Windows.".to_string())
}
