use std::collections::HashSet;
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::PathBuf;

use serde::{Deserialize, Serialize};

use crate::notifier::ItemDropEvent;

const HOOK_DROP_LOG_PATH: &str = r"C:\soe_companion_drops.log";

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HookDropEventsResult {
    pub log_path: String,
    pub events: Vec<ItemDropEvent>,
    pub event_ids: Vec<String>,
    pub lines_read: usize,
    pub skipped_processed: usize,
    pub parse_errors: usize,
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
    let quality = clean(line.quality).if_empty("Normal");
    let name = clean(line.name).if_empty(if item_code.is_empty() {
        "Unknown item"
    } else {
        item_code.as_str()
    });

    ItemDropEvent {
        unit_id: line.unit_id.unwrap_or(0),
        class: line.class.unwrap_or(0),
        item_code,
        quality,
        name,
        base_name: clean(line.base_name),
        canonical_name: clean(line.canonical_name),
        category: line.category.and_then(|value| {
            let trimmed = value.trim().to_string();
            if trimmed.is_empty() { None } else { Some(trimmed) }
        }),
        stats: clean(line.stats),
        is_ethereal: line.is_ethereal.unwrap_or(false),
        is_identified: line.is_identified.unwrap_or(true),
        is_runeword: line.is_runeword.unwrap_or(false),
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
pub fn read_hook_drop_events(processed_ids: Vec<String>) -> HookDropEventsResult {
    let path = PathBuf::from(HOOK_DROP_LOG_PATH);
    let processed: HashSet<String> = processed_ids
        .into_iter()
        .map(|id| id.trim().to_string())
        .filter(|id| !id.is_empty())
        .collect();

    let file = match File::open(&path) {
        Ok(file) => file,
        Err(_) => {
            return HookDropEventsResult {
                log_path: path.display().to_string(),
                events: Vec::new(),
                event_ids: Vec::new(),
                lines_read: 0,
                skipped_processed: 0,
                parse_errors: 0,
            };
        }
    };

    let mut events = Vec::new();
    let mut event_ids = Vec::new();
    let mut lines_read = 0usize;
    let mut skipped_processed = 0usize;
    let mut parse_errors = 0usize;

    for raw_line in BufReader::new(file).lines().map_while(Result::ok) {
        lines_read += 1;
        let trimmed = raw_line.trim();
        if trimmed.is_empty() || trimmed.starts_with('#') {
            continue;
        }

        let parsed = match serde_json::from_str::<HookDropLogLine>(trimmed) {
            Ok(parsed) => parsed,
            Err(_) => {
                parse_errors += 1;
                continue;
            }
        };
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
    }
}
