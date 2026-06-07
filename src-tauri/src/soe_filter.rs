use std::fs;
use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::path::{Path, PathBuf};
use tauri::{AppHandle, Manager};

const FILTER_FILE_NAME: &str = "SoE_Filter.filter";
const FILTER_DIR: &str = r"C:\Program Files (x86)\Diablo II\ProjectD2\filters\local";
const BUNDLED_FILTER: &str = include_str!("../resources/Hiim_SOE.filter");
const DEFAULT_FILTER_NAME: &str = "Hiim_SOE";
const PRESET_EXT: &str = "filter";

#[derive(Debug, Clone, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SoeFilterState {
    pub path: String,
    pub exists: bool,
    pub text: String,
}

#[derive(Debug, Clone, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SoeFilterProfile {
    pub name: String,
    pub modified: Option<String>,
}

fn filter_path() -> PathBuf {
    PathBuf::from(FILTER_DIR).join(FILTER_FILE_NAME)
}

fn presets_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = app
        .path()
        .app_data_dir()
        .map_err(|e| format!("Failed to get app data directory: {}", e))?
        .join("soe-filters");
    fs::create_dir_all(&dir)
        .map_err(|e| format!("Failed to create SoE filters directory: {}", e))?;
    Ok(dir)
}

fn sanitize_name(name: &str) -> String {
    name.chars()
        .map(|c| {
            if c.is_alphanumeric() || c == '-' || c == '_' || c == ' ' {
                c
            } else {
                '_'
            }
        })
        .collect::<String>()
        .trim()
        .to_string()
}

fn preset_path(dir: &Path, safe_name: &str) -> PathBuf {
    dir.join(format!("{}.{}", safe_name, PRESET_EXT))
}

fn bundled_filter_marker_path(dir: &Path) -> PathBuf {
    dir.join(format!("{}.bundled-hash", DEFAULT_FILTER_NAME))
}

fn bundled_filter_hash() -> String {
    let mut hasher = DefaultHasher::new();
    BUNDLED_FILTER.hash(&mut hasher);
    format!("{:016x}", hasher.finish())
}

fn text_hash(text: &str) -> String {
    let mut hasher = DefaultHasher::new();
    text.hash(&mut hasher);
    format!("{:016x}", hasher.finish())
}

fn modified_iso(path: &Path) -> Option<String> {
    path.metadata()
        .ok()
        .and_then(|m| m.modified().ok())
        .map(|t| {
            chrono::DateTime::<chrono::Utc>::from(t)
                .format("%Y-%m-%dT%H:%M:%SZ")
                .to_string()
        })
}

fn ensure_default_preset(app: &AppHandle) -> Result<(), String> {
    let dir = presets_dir(app)?;
    let path = preset_path(&dir, DEFAULT_FILTER_NAME);
    let marker_path = bundled_filter_marker_path(&dir);
    let next_hash = bundled_filter_hash();
    let previous_hash = fs::read_to_string(&marker_path).ok().map(|s| s.trim().to_string());
    let should_write = if !path.exists() {
        true
    } else if previous_hash.is_none() {
        true
    } else if previous_hash.as_deref() == Some(next_hash.as_str()) {
        false
    } else {
        let current_text = fs::read_to_string(&path).unwrap_or_default();
        previous_hash.as_deref() == Some(text_hash(&current_text).as_str())
    };

    if should_write {
        fs::write(&path, BUNDLED_FILTER)
            .map_err(|e| format!("Failed to seed bundled SoE filter preset: {}", e))?;
        fs::write(&marker_path, &next_hash)
            .map_err(|e| format!("Failed to save bundled SoE filter marker: {}", e))?;
    }
    Ok(())
}

fn is_retired_builtin_profile(name: &str) -> bool {
    name.eq_ignore_ascii_case("Kryzard_SoE") || name.eq_ignore_ascii_case("Kryzard SoE")
}

fn profile_info(dir: &Path, name: String) -> SoeFilterProfile {
    SoeFilterProfile {
        modified: modified_iso(&preset_path(dir, &name)),
        name,
    }
}

#[tauri::command]
pub fn get_bundled_soe_filter() -> String {
    BUNDLED_FILTER.to_string()
}

#[tauri::command]
pub fn list_soe_filter_profiles(app: AppHandle) -> Result<Vec<SoeFilterProfile>, String> {
    ensure_default_preset(&app)?;
    let dir = presets_dir(&app)?;
    let mut profiles = Vec::new();

    for entry in
        fs::read_dir(&dir).map_err(|e| format!("Failed to read SoE filter presets: {}", e))?
    {
        let entry = match entry {
            Ok(entry) => entry,
            Err(_) => continue,
        };
        let path = entry.path();
        if !path.extension().is_some_and(|ext| ext == PRESET_EXT) {
            continue;
        }
        let Some(stem) = path.file_stem().and_then(|s| s.to_str()) else {
            continue;
        };
        if is_retired_builtin_profile(stem) {
            continue;
        }
        profiles.push(SoeFilterProfile {
            name: stem.to_string(),
            modified: modified_iso(&path),
        });
    }

    profiles.sort_by(|a, b| a.name.to_lowercase().cmp(&b.name.to_lowercase()));
    Ok(profiles)
}

#[tauri::command]
pub fn load_soe_filter_profile(app: AppHandle, name: String) -> Result<String, String> {
    ensure_default_preset(&app)?;
    let dir = presets_dir(&app)?;
    let safe_name = sanitize_name(&name);
    if safe_name.is_empty() {
        return Err("Filter name cannot be empty".to_string());
    }
    let path = preset_path(&dir, &safe_name);
    fs::read_to_string(&path)
        .map_err(|e| format!("Failed to read SoE filter preset '{}': {}", safe_name, e))
}

#[tauri::command]
pub fn save_soe_filter_profile(
    app: AppHandle,
    name: String,
    text: String,
) -> Result<SoeFilterProfile, String> {
    let dir = presets_dir(&app)?;
    let safe_name = sanitize_name(&name);
    if safe_name.is_empty() {
        return Err("Filter name cannot be empty".to_string());
    }
    fs::write(preset_path(&dir, &safe_name), text)
        .map_err(|e| format!("Failed to save SoE filter preset '{}': {}", safe_name, e))?;
    Ok(profile_info(&dir, safe_name))
}

#[tauri::command]
pub fn create_soe_filter_profile(
    app: AppHandle,
    name: String,
    text: String,
) -> Result<SoeFilterProfile, String> {
    let dir = presets_dir(&app)?;
    let safe_name = sanitize_name(&name);
    if safe_name.is_empty() {
        return Err("Filter name cannot be empty".to_string());
    }
    let path = preset_path(&dir, &safe_name);
    if path.exists() {
        return Err(format!("Filter preset '{}' already exists", safe_name));
    }
    fs::write(&path, text)
        .map_err(|e| format!("Failed to create SoE filter preset '{}': {}", safe_name, e))?;
    Ok(profile_info(&dir, safe_name))
}

#[tauri::command]
pub fn rename_soe_filter_profile(
    app: AppHandle,
    old_name: String,
    new_name: String,
) -> Result<SoeFilterProfile, String> {
    ensure_default_preset(&app)?;
    let dir = presets_dir(&app)?;
    let old_safe = sanitize_name(&old_name);
    let new_safe = sanitize_name(&new_name);
    if old_safe.is_empty() || new_safe.is_empty() {
        return Err("Filter name cannot be empty".to_string());
    }
    if old_safe == new_safe {
        return Ok(profile_info(&dir, new_safe));
    }
    let old_path = preset_path(&dir, &old_safe);
    let new_path = preset_path(&dir, &new_safe);
    if !old_path.exists() {
        return Err(format!("Filter preset '{}' does not exist", old_safe));
    }
    if new_path.exists() {
        return Err(format!("Filter preset '{}' already exists", new_safe));
    }
    fs::rename(&old_path, &new_path)
        .map_err(|e| format!("Failed to rename filter preset '{}': {}", old_safe, e))?;
    Ok(profile_info(&dir, new_safe))
}

#[tauri::command]
pub fn delete_soe_filter_profile(app: AppHandle, name: String) -> Result<(), String> {
    ensure_default_preset(&app)?;
    let dir = presets_dir(&app)?;
    let safe_name = sanitize_name(&name);
    if safe_name.is_empty() {
        return Err("Filter name cannot be empty".to_string());
    }
    let path = preset_path(&dir, &safe_name);
    if !path.exists() {
        return Err(format!("Filter preset '{}' does not exist", safe_name));
    }
    fs::remove_file(&path)
        .map_err(|e| format!("Failed to delete filter preset '{}': {}", safe_name, e))?;
    ensure_default_preset(&app)?;
    Ok(())
}

#[tauri::command]
pub fn read_installed_soe_filter() -> Result<SoeFilterState, String> {
    let path = filter_path();
    let exists = path.exists();
    let text = if exists {
        std::fs::read_to_string(&path)
            .map_err(|e| format!("Failed to read installed SoE filter: {}", e))?
    } else {
        BUNDLED_FILTER.to_string()
    };

    Ok(SoeFilterState {
        path: path.to_string_lossy().to_string(),
        exists,
        text,
    })
}

#[tauri::command]
pub fn write_installed_soe_filter(text: String) -> Result<SoeFilterState, String> {
    let path = filter_path();
    let dir = path
        .parent()
        .ok_or_else(|| "SoE filter path has no parent directory".to_string())?;

    fs::create_dir_all(dir)
        .map_err(|e| format!("Failed to create ProjectD2 local filters directory: {}", e))?;
    fs::write(&path, text).map_err(|e| format!("Failed to write SoE_Filter.filter: {}", e))?;

    read_installed_soe_filter()
}
