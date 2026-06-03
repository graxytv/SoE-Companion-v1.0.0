use serde::Serialize;
use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::UNIX_EPOCH;

const D2S_MAGIC: [u8; 4] = [0x55, 0xaa, 0x55, 0xaa];
const NAME_OFFSET: usize = 0x14;
const NAME_LEN: usize = 16;
const CLASS_OFFSET: usize = 0x28;
const LEVEL_OFFSET: usize = 0x2b;

#[derive(Debug, Clone, Serialize)]
pub struct CharacterLevelEntry {
    pub name: String,
    pub class_name: String,
    pub level: u8,
    pub path: String,
}

#[derive(Debug, Serialize)]
pub struct CharacterLevelSyncResult {
    pub characters: Vec<CharacterLevelEntry>,
    pub scanned_dirs: Vec<String>,
    pub message: String,
}

#[derive(Debug, Clone)]
struct ParsedCharacter {
    entry: CharacterLevelEntry,
    modified_ms: u128,
}

fn class_name(class_id: u8) -> &'static str {
    match class_id {
        0 => "Amazon",
        1 => "Sorceress",
        2 => "Necromancer",
        3 => "Paladin",
        4 => "Barbarian",
        5 => "Druid",
        6 => "Assassin",
        _ => "Unknown",
    }
}

fn clean_character_name(bytes: &[u8]) -> Option<String> {
    let end = bytes.iter().position(|byte| *byte == 0).unwrap_or(bytes.len());
    let name = String::from_utf8_lossy(&bytes[..end]).trim().to_string();
    let valid = !name.is_empty()
        && name
            .chars()
            .all(|ch| ch.is_ascii_alphanumeric() || ch == '_' || ch == '-');
    valid.then_some(name)
}

fn parse_character_file(path: &Path) -> Result<ParsedCharacter, String> {
    let bytes = fs::read(path).map_err(|error| format!("Failed to read {}: {error}", path.display()))?;
    if bytes.len() <= LEVEL_OFFSET || bytes.get(0..4) != Some(&D2S_MAGIC) {
        return Err(format!("Not a Diablo II character save: {}", path.display()));
    }

    let name = clean_character_name(&bytes[NAME_OFFSET..NAME_OFFSET + NAME_LEN])
        .or_else(|| path.file_stem().map(|stem| stem.to_string_lossy().trim().to_string()))
        .filter(|name| !name.is_empty())
        .ok_or_else(|| format!("Character save has no name: {}", path.display()))?;
    let level = bytes[LEVEL_OFFSET];
    if !(1..=99).contains(&level) {
        return Err(format!("Character save has an invalid level: {}", path.display()));
    }

    let modified_ms = fs::metadata(path)
        .ok()
        .and_then(|metadata| metadata.modified().ok())
        .and_then(|modified| modified.duration_since(UNIX_EPOCH).ok())
        .map(|duration| duration.as_millis())
        .unwrap_or(0);

    Ok(ParsedCharacter {
        entry: CharacterLevelEntry {
            name,
            class_name: class_name(bytes[CLASS_OFFSET]).to_string(),
            level,
            path: path.display().to_string(),
        },
        modified_ms,
    })
}

fn push_unique_dir(paths: &mut Vec<PathBuf>, path: PathBuf) {
    if !path.is_dir() {
        return;
    }
    let canonical = path.canonicalize().unwrap_or_else(|_| path.clone());
    if !paths.iter().any(|existing| existing == &canonical) {
        paths.push(canonical);
    }
}

fn candidate_save_dirs(stash_path: Option<String>) -> Vec<PathBuf> {
    let mut paths = Vec::new();

    if let Some(path) = stash_path.map(|path| path.trim().to_string()).filter(|path| !path.is_empty()) {
        let selected = PathBuf::from(path);
        if selected.is_dir() {
            push_unique_dir(&mut paths, selected);
        } else if let Some(parent) = selected.parent() {
            push_unique_dir(&mut paths, parent.to_path_buf());
        }
    }

    if let Ok(profile) = std::env::var("USERPROFILE") {
        push_unique_dir(
            &mut paths,
            PathBuf::from(&profile).join("Saved Games").join("Diablo II"),
        );
        push_unique_dir(
            &mut paths,
            PathBuf::from(&profile).join("Saved Games").join("ProjectD2"),
        );
    }

    push_unique_dir(&mut paths, PathBuf::from(r"C:\Program Files (x86)\Diablo II\Save"));
    push_unique_dir(&mut paths, PathBuf::from(r"C:\Program Files\Diablo II\Save"));

    paths
}

#[tauri::command]
pub fn sync_character_levels(stash_path: Option<String>) -> Result<CharacterLevelSyncResult, String> {
    let save_dirs = candidate_save_dirs(stash_path);
    if save_dirs.is_empty() {
        return Ok(CharacterLevelSyncResult {
            characters: Vec::new(),
            scanned_dirs: Vec::new(),
            message: "No Diablo II Save folder was found.".to_string(),
        });
    }

    let mut by_name: HashMap<String, ParsedCharacter> = HashMap::new();
    let mut scanned_dirs = Vec::new();

    for dir in save_dirs {
        let Ok(entries) = fs::read_dir(&dir) else {
            continue;
        };
        scanned_dirs.push(dir.display().to_string());
        for entry in entries.flatten() {
            let path = entry.path();
            let is_d2s = path
                .extension()
                .and_then(|extension| extension.to_str())
                .is_some_and(|extension| extension.eq_ignore_ascii_case("d2s"));
            if !is_d2s {
                continue;
            }
            let Ok(parsed) = parse_character_file(&path) else {
                continue;
            };
            let key = parsed.entry.name.to_lowercase();
            let replace = by_name
                .get(&key)
                .map(|existing| {
                    parsed.entry.level > existing.entry.level
                        || (parsed.entry.level == existing.entry.level && parsed.modified_ms > existing.modified_ms)
                })
                .unwrap_or(true);
            if replace {
                by_name.insert(key, parsed);
            }
        }
    }

    let mut characters: Vec<CharacterLevelEntry> = by_name.into_values().map(|parsed| parsed.entry).collect();
    characters.sort_by(|a, b| a.name.to_lowercase().cmp(&b.name.to_lowercase()));

    let message = match characters.len() {
        0 => "No character save files were found.".to_string(),
        1 => "Synced 1 character level.".to_string(),
        count => format!("Synced {count} character levels."),
    };

    Ok(CharacterLevelSyncResult {
        characters,
        scanned_dirs,
        message,
    })
}
