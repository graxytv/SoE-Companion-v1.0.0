//! Drop-sound file management.
//!
//! Custom audio files live in `app_data_dir/sounds/slot-{N}.{ext}`
//! (one file per slot). Used by the Sounds tab UI; the slot metadata
//! (label, volume, source kind) is persisted via `AppSettings.sounds`.

use std::fs;
use std::io::Read;
use std::path::PathBuf;

use tauri::{AppHandle, Manager};

use crate::logger::{error as log_error, info as log_info};

const SOUNDS_DIR: &str = "sounds";
const MAX_BYTES: usize = 5 * 1024 * 1024; // 5 MB
const ALLOWED_EXTS: &[&str] = &["mp3", "wav", "ogg", "m4a", "flac"];
const FILTERBLADE_SOUND_BASE_URL: &str = "https://www.filterblade.xyz/assets/communitySounds";

fn sounds_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = app
        .path()
        .app_data_dir()
        .map_err(|e| format!("failed to resolve app_data_dir: {}", e))?
        .join(SOUNDS_DIR);
    if !dir.exists() {
        fs::create_dir_all(&dir).map_err(|e| format!("failed to create {:?}: {}", dir, e))?;
    }
    Ok(dir)
}

fn extension_from(file_name: &str) -> Option<String> {
    PathBuf::from(file_name)
        .extension()
        .and_then(|s| s.to_str())
        .map(|s| s.to_ascii_lowercase())
}

fn remove_existing_for_slot(dir: &std::path::Path, slot: u32) -> std::io::Result<()> {
    for ext in ALLOWED_EXTS {
        let path = dir.join(format!("slot-{}.{}", slot, ext));
        if path.exists() {
            fs::remove_file(&path)?;
        }
    }
    Ok(())
}

fn validate_slot(slot: u32) -> Result<(), String> {
    if slot < 1 {
        return Err("slot must be >= 1".to_string());
    }
    Ok(())
}

fn validate_audio_file_name(file_name: &str) -> Result<String, String> {
    let ext = extension_from(file_name).ok_or_else(|| "file has no extension".to_string())?;
    if !ALLOWED_EXTS.contains(&ext.as_str()) {
        return Err(format!(
            "unsupported format '{}' (allowed: {})",
            ext,
            ALLOWED_EXTS.join(", ")
        ));
    }
    Ok(ext)
}

fn write_sound_slot(
    app: &AppHandle,
    slot: u32,
    file_name: &str,
    bytes: &[u8],
) -> Result<String, String> {
    validate_slot(slot)?;
    if bytes.len() > MAX_BYTES {
        return Err(format!(
            "file too large ({} bytes, max {} bytes)",
            bytes.len(),
            MAX_BYTES
        ));
    }
    let ext = validate_audio_file_name(file_name)?;

    let dir = sounds_dir(app)?;
    let new_name = format!("slot-{}.{}", slot, ext);
    let new_path = dir.join(&new_name);

    // Write the new file first; only on success do we delete any prior file
    // for this slot (so a write failure doesn't leave the slot empty).
    fs::write(&new_path, bytes).map_err(|e| format!("failed to write {:?}: {}", new_path, e))?;
    if let Err(e) = remove_existing_for_slot(&dir, slot) {
        log_error(&format!(
            "write_sound_slot: failed to clean prior file for slot {}: {}",
            slot, e
        ));
    }
    // After cleanup, ensure the new file is still present (the cleanup pass
    // would have deleted it if its extension equals the new one).
    if !new_path.exists() {
        fs::write(&new_path, bytes)
            .map_err(|e| format!("failed to re-write {:?}: {}", new_path, e))?;
    }
    Ok(new_name)
}

fn is_safe_filterblade_segment(segment: &str) -> bool {
    !segment.is_empty()
        && !segment.contains("..")
        && !segment.contains('/')
        && !segment.contains('\\')
        && segment.chars().all(|c| {
            c.is_ascii_alphanumeric() || matches!(c, ' ' | '-' | '_' | '(' | ')' | '.' | '\'')
        })
}

fn encode_url_segment(segment: &str) -> String {
    let mut out = String::new();
    for byte in segment.as_bytes() {
        match *byte {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' => out.push(*byte as char),
            b' ' => out.push_str("%20"),
            other => out.push_str(&format!("%{:02X}", other)),
        }
    }
    out
}

/// Validates extension/size, writes bytes to `app_data_dir/sounds/slot-{N}.{ext}`,
/// removes any prior file for the slot. Returns the saved file name.
#[tauri::command]
pub fn import_sound_file(
    app: AppHandle,
    slot: u32,
    file_name: String,
    bytes: Vec<u8>,
) -> Result<String, String> {
    let new_name = write_sound_slot(&app, slot, &file_name, &bytes)?;
    log_info(&format!(
        "Imported sound for slot {} ({} bytes, file={})",
        slot,
        bytes.len(),
        file_name
    ));
    Ok(new_name)
}

/// Downloads one FilterBlade community sound into the selected local sound slot.
///
/// The app does not bundle or redistribute these files. This is a user-triggered
/// local download from FilterBlade's public static assets.
#[tauri::command]
pub fn download_filterblade_community_sound(
    app: AppHandle,
    slot: u32,
    pack: String,
    file_name: String,
) -> Result<String, String> {
    validate_slot(slot)?;
    if !is_safe_filterblade_segment(&pack) {
        return Err("invalid FilterBlade sound-pack name".to_string());
    }
    if !is_safe_filterblade_segment(&file_name) {
        return Err("invalid FilterBlade sound file name".to_string());
    }
    validate_audio_file_name(&file_name)?;

    let url = format!(
        "{}/{}/{}",
        FILTERBLADE_SOUND_BASE_URL,
        encode_url_segment(&pack),
        encode_url_segment(&file_name)
    );
    let response = ureq::get(&url)
        .timeout(std::time::Duration::from_secs(20))
        .call()
        .map_err(|e| format!("failed to download FilterBlade sound: {}", e))?;

    let mut reader = response.into_reader();
    let mut bytes = Vec::new();
    reader
        .by_ref()
        .take((MAX_BYTES + 1) as u64)
        .read_to_end(&mut bytes)
        .map_err(|e| format!("failed to read FilterBlade sound response: {}", e))?;
    if bytes.len() > MAX_BYTES {
        return Err(format!(
            "downloaded sound is too large (max {} bytes)",
            MAX_BYTES
        ));
    }

    let new_name = write_sound_slot(&app, slot, &file_name, &bytes)?;
    log_info(&format!(
        "Downloaded FilterBlade community sound {}/{} into slot {}",
        pack, file_name, slot
    ));
    Ok(new_name)
}

/// Removes any file for the slot. Idempotent.
#[tauri::command]
pub fn delete_sound_file(app: AppHandle, slot: u32) -> Result<(), String> {
    let dir = sounds_dir(&app)?;
    remove_existing_for_slot(&dir, slot)
        .map_err(|e| format!("failed to delete sound file(s) for slot {}: {}", slot, e))?;
    log_info(&format!("Deleted sound files for slot {}", slot));
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn extension_from_lowercases_and_strips_dot() {
        assert_eq!(extension_from("foo.MP3").as_deref(), Some("mp3"));
        assert_eq!(extension_from("with.spaces.ogg").as_deref(), Some("ogg"));
        assert_eq!(extension_from("no_ext"), None);
    }
}
