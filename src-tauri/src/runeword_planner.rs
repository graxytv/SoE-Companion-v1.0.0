use serde::Serialize;
use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;

const RUNE_NAMES: &[&str] = &[
    "El", "Eld", "Tir", "Nef", "Eth", "Ith", "Tal", "Ral", "Ort", "Thul", "Amn", "Sol",
    "Shael", "Dol", "Hel", "Io", "Lum", "Ko", "Fal", "Lem", "Pul", "Um", "Mal", "Ist",
    "Gul", "Vex", "Ohm", "Lo", "Sur", "Ber", "Jah", "Cham", "Zod",
];

#[derive(Debug, Serialize)]
pub struct RuneStashSyncResult {
    pub counts: HashMap<String, u32>,
    pub scanned_files: Vec<String>,
    pub message: String,
}

fn empty_counts() -> HashMap<String, u32> {
    RUNE_NAMES
        .iter()
        .map(|rune| ((*rune).to_string(), 0))
        .collect()
}

fn shared_stash_paths() -> Vec<PathBuf> {
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
                .join("ProjectD2")
                .join("pd2_shared.stash"),
        );
    }
    paths.push(PathBuf::from(r"C:\Program Files (x86)\Diablo II\Save\pd2_shared.stash"));
    paths.push(PathBuf::from(r"C:\Program Files\Diablo II\Save\pd2_shared.stash"));
    paths
}

fn material_rune_counts(bytes: &[u8]) -> [u32; 33] {
    let mut counts = [0u32; 33];
    if bytes.len() < 4 {
        return counts;
    }

    for offset in 0..bytes.len().saturating_sub(4) {
        if bytes[offset] != b'c' || bytes[offset + 1] != b'u' {
            continue;
        }

        // PD2's materials tab stores compact counters in the shared stash "cu"
        // section. Controlled rune conversion probes show cu + 2 maps El,
        // Eld, Tir, Nef ... through Zod.
        let counts_start = offset + 2;
        if counts_start + counts.len() * 2 > bytes.len() {
            continue;
        }

        let mut plausible = true;
        let mut parsed = [0u32; 33];
        for (idx, slot) in parsed.iter_mut().enumerate() {
            let start = counts_start + idx * 2;
            let value = u16::from_le_bytes([bytes[start], bytes[start + 1]]) as u32;
            if value > 9999 {
                plausible = false;
                break;
            }
            *slot = value;
        }

        if plausible && parsed.iter().any(|value| *value > 0) {
            counts = parsed;
            break;
        }
    }

    counts
}

fn selected_or_detected_stash_path(stash_path: Option<String>) -> Option<PathBuf> {
    if let Some(path) = stash_path.map(|path| path.trim().to_string()).filter(|path| !path.is_empty()) {
        return Some(PathBuf::from(path));
    }
    shared_stash_paths().into_iter().find(|path| path.is_file())
}

#[tauri::command]
pub fn detect_shared_stash_paths() -> Vec<String> {
    shared_stash_paths()
        .into_iter()
        .filter(|path| path.is_file())
        .map(|path| path.display().to_string())
        .collect()
}

#[tauri::command]
pub fn sync_shared_stash_runes(stash_path: Option<String>) -> Result<RuneStashSyncResult, String> {
    let mut counts = empty_counts();
    let mut scanned_files = Vec::new();

    let Some(path) = selected_or_detected_stash_path(stash_path) else {
        return Ok(RuneStashSyncResult {
            counts,
            scanned_files,
            message: "No pd2_shared.stash file was found. Select your shared stash file to sync runes.".to_string(),
        });
    };

    if !path.is_file() {
        return Err(format!("Selected shared stash file does not exist: {}", path.display()));
    }

    let bytes = fs::read(&path)
        .map_err(|e| format!("Failed to read shared stash {}: {}", path.display(), e))?;
    scanned_files.push(path.display().to_string());
    let material_counts = material_rune_counts(&bytes);
    for (idx, rune) in RUNE_NAMES.iter().enumerate() {
        if material_counts[idx] > 0 {
            *counts.entry((*rune).to_string()).or_insert(0) += material_counts[idx];
        }
    }

    let total: u32 = counts.values().copied().sum();
    let message = if total == 0 {
        "No runes were found in the shared stash materials tab.".to_string()
    } else {
        "Rune materials synced.".to_string()
    };

    Ok(RuneStashSyncResult {
        counts,
        scanned_files,
        message,
    })
}
