use std::collections::{HashMap, HashSet};
use std::fs::{File, OpenOptions};
use std::io::{BufRead, BufReader, Read, Seek, SeekFrom, Write};
use std::path::PathBuf;
use std::sync::OnceLock;

use serde::{Deserialize, Serialize};

use crate::notifier::ItemDropEvent;

const HOOK_DROP_LOG_PATH: &str = r"C:\SoECompanion\logs\soe_companion_drops.log";
const LEGACY_HOOK_DROP_LOG_PATH: &str = r"C:\soe_companion_drops.log";
const MAX_HOOK_LOG_INITIAL_TAIL_BYTES: u64 = 2 * 1024 * 1024;
const MAX_HOOK_LOG_READ_BYTES: u64 = 1024 * 1024;
const MAX_HOOK_LOG_LINES_PER_SYNC: usize = 5_000;
const MAX_HOOK_LOG_EVENTS_PER_SYNC: usize = 1_000;

const RUNE_NAMES: &[&str] = &[
    "El", "Eld", "Tir", "Nef", "Eth", "Ith", "Tal", "Ral", "Ort", "Thul", "Amn", "Sol",
    "Shael", "Dol", "Hel", "Io", "Lum", "Ko", "Fal", "Lem", "Pul", "Um", "Mal", "Ist",
    "Gul", "Vex", "Ohm", "Lo", "Sur", "Ber", "Jah", "Cham", "Zod",
];

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HookDropEventsResult {
    pub log_path: String,
    pub events: Vec<ItemDropEvent>,
    pub event_ids: Vec<String>,
    pub lines_read: usize,
    pub skipped_processed: usize,
    pub parse_errors: usize,
    pub cursor_before: u64,
    pub cursor_after: u64,
    pub log_length: u64,
    pub reached_limit: bool,
    pub skipped_backlog_bytes: u64,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HookDropCompactResult {
    pub log_path: String,
    pub old_length: u64,
    pub new_length: u64,
    pub compacted: bool,
}

fn hook_drop_log_path_for_read() -> PathBuf {
    let current = PathBuf::from(HOOK_DROP_LOG_PATH);
    if current.exists() {
        current
    } else {
        let legacy = PathBuf::from(LEGACY_HOOK_DROP_LOG_PATH);
        if legacy.exists() {
            legacy
        } else {
            current
        }
    }
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
struct HookDropLogLine {
    #[serde(default, alias = "id", alias = "event_id")]
    event_id: Option<String>,
    #[serde(default, alias = "unit_id")]
    unit_id: Option<u32>,
    #[serde(default, alias = "item_class")]
    class: Option<u32>,
    #[serde(default, alias = "item_code", alias = "code")]
    item_code: Option<String>,
    #[serde(default)]
    quality: Option<String>,
    #[serde(default)]
    name: Option<String>,
    #[serde(default, alias = "base_name")]
    base_name: Option<String>,
    #[serde(default, alias = "canonical_name")]
    canonical_name: Option<String>,
    #[serde(default)]
    category: Option<String>,
    #[serde(default)]
    stats: Option<String>,
    #[serde(default, alias = "is_ethereal")]
    is_ethereal: Option<bool>,
    #[serde(default, alias = "is_identified")]
    is_identified: Option<bool>,
    #[serde(default, alias = "is_runeword")]
    is_runeword: Option<bool>,
    #[serde(default, alias = "p_unit_data")]
    p_unit_data: Option<u32>,
    #[serde(default)]
    mode: Option<u32>,
    #[serde(default, alias = "file_index")]
    file_index: Option<u32>,
    #[serde(default)]
    seed: Option<u32>,
    #[serde(default)]
    source: Option<String>,
    #[serde(default, alias = "name_source")]
    name_source: Option<String>,
    #[serde(default, alias = "is_hellforged")]
    is_hellforged: Option<bool>,
    #[serde(default)]
    sockets: Option<u8>,
}

fn fnv1a64(value: &str) -> u64 {
    let mut hash = 0xcbf29ce484222325u64;
    for byte in value.as_bytes() {
        hash ^= *byte as u64;
        hash = hash.wrapping_mul(0x100000001b3);
    }
    hash
}

fn clean(value: Option<String>) -> String {
    value.unwrap_or_default().trim().to_string()
}

fn normalized_tracker_name(value: &str) -> String {
    value
        .trim()
        .to_ascii_lowercase()
        .replace(['\u{2018}', '\u{2019}'], "'")
        .chars()
        .map(|ch| if ch.is_ascii_alphanumeric() { ch } else { ' ' })
        .collect::<String>()
        .split_whitespace()
        .collect::<Vec<_>>()
        .join(" ")
}

fn runeword_name_lookup() -> &'static HashMap<String, String> {
    static LOOKUP: OnceLock<HashMap<String, String>> = OnceLock::new();
    LOOKUP.get_or_init(|| {
        let raw = include_str!("../../src/lib/data/soe-runewords.json");
        let Ok(value) = serde_json::from_str::<serde_json::Value>(raw) else {
            return HashMap::new();
        };
        let Some(rows) = value.as_array() else {
            return HashMap::new();
        };

        let mut lookup = HashMap::new();
        for row in rows {
            let display_name = row
                .get("displayName")
                .and_then(|value| value.as_str())
                .or_else(|| row.get("runewordName").and_then(|value| value.as_str()))
                .map(str::trim)
                .filter(|value| !value.is_empty());
            let Some(canonical) = display_name else {
                continue;
            };

            for key in ["displayName", "runewordName", "name"] {
                if let Some(alias) = row
                    .get(key)
                    .and_then(|value| value.as_str())
                    .map(str::trim)
                    .filter(|value| !value.is_empty())
                {
                    let normalized = normalized_tracker_name(alias);
                    if !normalized.is_empty() {
                        lookup.insert(normalized, canonical.to_string());
                    }
                }
            }
        }
        lookup
    })
}

fn runeword_name_from_text(value: &str) -> Option<String> {
    let lookup = runeword_name_lookup();
    for line in value.lines().map(str::trim).filter(|line| !line.is_empty()) {
        if let Some(name) = lookup.get(&normalized_tracker_name(line)) {
            return Some(name.clone());
        }
    }
    lookup.get(&normalized_tracker_name(value)).cloned()
}

fn rune_name_from_code(code: &str) -> Option<&'static str> {
    let code = code.trim().to_ascii_lowercase();
    let suffix = code.strip_prefix('r')?;
    let index = suffix.parse::<usize>().ok()?.checked_sub(1)?;
    RUNE_NAMES.get(index).copied()
}

fn hook_event_id(line: &HookDropLogLine, raw_line: &str) -> String {
    if let Some(id) = line
        .event_id
        .as_deref()
        .map(str::trim)
        .filter(|id| !id.is_empty())
    {
        return id.to_string();
    }

    let code = line.item_code.as_deref().unwrap_or("").trim();
    let quality = line.quality.as_deref().unwrap_or("").trim();
    let name = line.name.as_deref().unwrap_or("").trim();
    let unit_id = line.unit_id.unwrap_or(0);
    let seed = line.seed.unwrap_or(0);
    if seed != 0 || unit_id != 0 || !code.is_empty() || !name.is_empty() {
        return format!("hook:v1:{seed}:{unit_id}:{code}:{quality}:{name}");
    }

    format!("hook:v1:line:{:016x}", fnv1a64(raw_line))
}

fn into_item_drop(line: HookDropLogLine) -> ItemDropEvent {
    let item_code = clean(line.item_code);
    let mut quality = clean(line.quality).if_empty("Normal");
    let mut name = clean(line.name).if_empty(if item_code.is_empty() {
        "Unknown item"
    } else {
        item_code.as_str()
    });
    let base_name = clean(line.base_name);
    let mut canonical_name = clean(line.canonical_name);
    let is_runeword =
        line.is_runeword.unwrap_or(false) || quality.trim().eq_ignore_ascii_case("runeword");

    if is_runeword {
        let combined_name = format!("{name}\n{canonical_name}\n{base_name}");
        if let Some(runeword_name) = runeword_name_from_text(&combined_name) {
            name = runeword_name.clone();
            canonical_name = runeword_name;
            quality = "Runeword".to_string();
        }
    } else if let Some(rune_name) = rune_name_from_code(&item_code) {
        let display_name = format!("{rune_name} Rune");
        if name.trim().eq_ignore_ascii_case(&item_code) || name.trim().is_empty() {
            name = display_name.clone();
        }
        if canonical_name.trim().is_empty() || canonical_name.trim().eq_ignore_ascii_case(&item_code) {
            canonical_name = display_name;
        }
    }

    ItemDropEvent {
        unit_id: line.unit_id.unwrap_or(0),
        class: line.class.unwrap_or(0),
        item_code,
        quality,
        name,
        base_name,
        canonical_name,
        category: line.category.and_then(|value| {
            let trimmed = value.trim().to_string();
            if trimmed.is_empty() { None } else { Some(trimmed) }
        }),
        stats: clean(line.stats),
        is_ethereal: line.is_ethereal.unwrap_or(false),
        is_identified: line.is_identified.unwrap_or(true),
        is_runeword,
        p_unit_data: line.p_unit_data.unwrap_or(0),
        mode: line.mode.unwrap_or(3),
        file_index: line.file_index.unwrap_or(0),
        seed: line.seed.unwrap_or(0),
        history_pushed: true,
        source: clean(line.source).if_empty("hook-log"),
        name_source: clean(line.name_source).if_empty("hook-log"),
        tier: None,
        unique_kind: None,
        is_hellforged: line.is_hellforged.unwrap_or(false),
        sockets: line.sockets.unwrap_or(0),
        filter: None,
    }
}

fn hook_line_is_runeword(line: &HookDropLogLine) -> bool {
    line.is_runeword.unwrap_or(false)
        || line
            .quality
            .as_deref()
            .map(str::trim)
            .is_some_and(|quality| quality.eq_ignore_ascii_case("runeword"))
}

fn hook_line_is_ground_drop(line: &HookDropLogLine) -> bool {
    line.mode == Some(3)
}

trait IfEmpty {
    fn if_empty(self, fallback: &str) -> String;
}

impl IfEmpty for String {
    fn if_empty(self, fallback: &str) -> String {
        if self.trim().is_empty() {
            fallback.to_string()
        } else {
            self
        }
    }
}

#[tauri::command]
pub fn read_hook_drop_events(
    processed_ids: Vec<String>,
    cursor: Option<u64>,
    skip_existing: Option<bool>,
) -> HookDropEventsResult {
    let path = hook_drop_log_path_for_read();
    let processed: HashSet<String> = processed_ids
        .into_iter()
        .map(|id| id.trim().to_string())
        .filter(|id| !id.is_empty())
        .collect();

    let mut file = match File::open(&path) {
        Ok(file) => file,
        Err(_) => {
            return HookDropEventsResult {
                log_path: path.display().to_string(),
                events: Vec::new(),
                event_ids: Vec::new(),
                lines_read: 0,
                skipped_processed: 0,
                parse_errors: 0,
                cursor_before: 0,
                cursor_after: 0,
                log_length: 0,
                reached_limit: false,
                skipped_backlog_bytes: 0,
            };
        }
    };

    let log_length = file.metadata().map(|metadata| metadata.len()).unwrap_or(0);
    let requested_cursor = cursor.unwrap_or(0);
    if skip_existing.unwrap_or(false) && requested_cursor == 0 && log_length > 0 {
        return HookDropEventsResult {
            log_path: path.display().to_string(),
            events: Vec::new(),
            event_ids: Vec::new(),
            lines_read: 0,
            skipped_processed: 0,
            parse_errors: 0,
            cursor_before: log_length,
            cursor_after: log_length,
            log_length,
            reached_limit: false,
            skipped_backlog_bytes: log_length,
        };
    }

    let mut cursor_before = if requested_cursor > log_length {
        0
    } else {
        requested_cursor
    };
    let mut skipped_backlog_bytes = 0u64;
    if cursor_before == 0 && log_length > MAX_HOOK_LOG_INITIAL_TAIL_BYTES {
        let tail_start = log_length.saturating_sub(MAX_HOOK_LOG_INITIAL_TAIL_BYTES);
        if file.seek(SeekFrom::Start(tail_start)).is_ok() {
            let mut reader = BufReader::new(&file);
            let mut partial = String::new();
            let line_bytes = reader.read_line(&mut partial).unwrap_or(0) as u64;
            cursor_before = tail_start.saturating_add(line_bytes).min(log_length);
            skipped_backlog_bytes = cursor_before;
        }
    }
    if file.seek(SeekFrom::Start(cursor_before)).is_err() {
        return HookDropEventsResult {
            log_path: path.display().to_string(),
            events: Vec::new(),
            event_ids: Vec::new(),
            lines_read: 0,
            skipped_processed: 0,
            parse_errors: 0,
            cursor_before: 0,
            cursor_after: 0,
            log_length,
            reached_limit: false,
            skipped_backlog_bytes,
        };
    }

    let mut events = Vec::new();
    let mut event_ids = Vec::new();
    let mut lines_read = 0usize;
    let mut skipped_processed = 0usize;
    let mut parse_errors = 0usize;
    let mut cursor_after = cursor_before;
    let mut reader = BufReader::new(file);
    let mut raw_line = String::new();
    let mut reached_limit = false;

    loop {
        if lines_read >= MAX_HOOK_LOG_LINES_PER_SYNC
            || events.len() >= MAX_HOOK_LOG_EVENTS_PER_SYNC
            || cursor_after.saturating_sub(cursor_before) >= MAX_HOOK_LOG_READ_BYTES
        {
            reached_limit = cursor_after < log_length;
            break;
        }

        raw_line.clear();
        let line_start = cursor_after;
        let bytes_read = match reader.read_line(&mut raw_line) {
            Ok(bytes) => bytes,
            Err(_) => {
                parse_errors += 1;
                break;
            }
        };
        if bytes_read == 0 {
            break;
        }
        cursor_after = cursor_after.saturating_add(bytes_read as u64);
        lines_read += 1;
        let trimmed = raw_line.trim();
        if trimmed.is_empty() || trimmed.starts_with('#') {
            continue;
        }

        let parsed = match serde_json::from_str::<HookDropLogLine>(trimmed) {
            Ok(parsed) => parsed,
            Err(_) => {
                if !raw_line.ends_with('\n') && cursor_after >= log_length {
                    cursor_after = line_start;
                    break;
                }
                parse_errors += 1;
                continue;
            }
        };
        if hook_line_is_runeword(&parsed) {
            continue;
        }
        if !hook_line_is_ground_drop(&parsed) {
            continue;
        }
        let event_id = hook_event_id(&parsed, trimmed);
        if processed.contains(&event_id) {
            skipped_processed += 1;
            continue;
        }

        events.push(into_item_drop(parsed));
        event_ids.push(event_id);
    }

    HookDropEventsResult {
        log_path: path.display().to_string(),
        events,
        event_ids,
        lines_read,
        skipped_processed,
        parse_errors,
        cursor_before,
        cursor_after,
        log_length,
        reached_limit,
        skipped_backlog_bytes,
    }
}

#[tauri::command]
pub fn compact_hook_drop_log(cursor: u64) -> HookDropCompactResult {
    let path = hook_drop_log_path_for_read();
    let log_path = path.display().to_string();
    let old_length = match std::fs::metadata(&path) {
        Ok(metadata) => metadata.len(),
        Err(_) => {
            return HookDropCompactResult {
                log_path,
                old_length: 0,
                new_length: 0,
                compacted: false,
            };
        }
    };

    if cursor == 0 || cursor > old_length {
        return HookDropCompactResult {
            log_path,
            old_length,
            new_length: old_length,
            compacted: false,
        };
    }

    let mut file = match File::open(&path) {
        Ok(file) => file,
        Err(_) => {
            return HookDropCompactResult {
                log_path,
                old_length,
                new_length: old_length,
                compacted: false,
            };
        }
    };
    if file.seek(SeekFrom::Start(cursor)).is_err() {
        return HookDropCompactResult {
            log_path,
            old_length,
            new_length: old_length,
            compacted: false,
        };
    }

    let mut tail = Vec::new();
    if file.read_to_end(&mut tail).is_err() {
        return HookDropCompactResult {
            log_path,
            old_length,
            new_length: old_length,
            compacted: false,
        };
    }
    drop(file);

    let mut writer = match OpenOptions::new().write(true).truncate(true).open(&path) {
        Ok(file) => file,
        Err(_) => {
            return HookDropCompactResult {
                log_path,
                old_length,
                new_length: old_length,
                compacted: false,
            };
        }
    };
    if writer.write_all(&tail).and_then(|_| writer.flush()).is_err() {
        return HookDropCompactResult {
            log_path,
            old_length,
            new_length: old_length,
            compacted: false,
        };
    }

    HookDropCompactResult {
        log_path,
        old_length,
        new_length: tail.len() as u64,
        compacted: true,
    }
}
