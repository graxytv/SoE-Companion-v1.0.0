use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};

const PROJECT_D2_DIR: &str = r"C:\Program Files (x86)\Diablo II\ProjectD2";
const DLL_NAME: &str = "ijl11.dll";
const ORIGINAL_DLL_NAME: &str = "ijl11_orig.dll";
const INI_NAME: &str = "DropIdentified.ini";
const LOG_PATH: &str = r"C:\SoECompanion\logs\soe_companion_drops.log";
const DROP_HOOK_VERSION: &str = "drop-events-v5";

const BUNDLED_DLL: &[u8] = include_bytes!("../resources/ijl11.dll");
const BUNDLED_INI: &str = include_str!("../resources/DropIdentified.ini");

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DropHookStatus {
    pub project_d2_dir: String,
    pub dll_path: String,
    pub original_dll_path: String,
    pub ini_path: String,
    pub log_path: String,
    pub project_d2_dir_exists: bool,
    pub dll_exists: bool,
    pub original_dll_exists: bool,
    pub ini_exists: bool,
    pub log_exists: bool,
    pub log_size: u64,
    pub installed: bool,
    pub grail_installed: bool,
    pub identified_installed: bool,
    pub dll_matches_bundled: bool,
    pub managed_hook_present: bool,
    pub hook_needs_update: bool,
    pub unknown_dll_present: bool,
    pub can_restore_original: bool,
    pub hook_version_matches: bool,
    pub hook_version: Option<String>,
    pub expected_hook_version: String,
    pub current_dll_hash: Option<String>,
    pub bundled_dll_hash: String,
    pub message: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DropIdentifiedConfig {
    pub enabled: bool,
    pub magic: bool,
    pub rare: bool,
    pub set: bool,
    pub unique: bool,
    pub small_charm: bool,
    pub large_charm: bool,
    pub grand_charm: bool,
}

impl Default for DropIdentifiedConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            magic: false,
            rare: false,
            set: false,
            unique: false,
            small_charm: false,
            large_charm: false,
            grand_charm: false,
        }
    }
}

impl DropIdentifiedConfig {
    fn enabled_defaults() -> Self {
        Self {
            enabled: true,
            magic: true,
            rare: true,
            set: true,
            unique: true,
            small_charm: false,
            large_charm: false,
            grand_charm: false,
        }
    }
}

fn normalize_project_d2_dir(project_d2_dir: Option<String>) -> PathBuf {
    project_d2_dir
        .map(|path| PathBuf::from(path.trim_matches('"').trim()))
        .filter(|path| !path.as_os_str().is_empty())
        .unwrap_or_else(|| {
            auto_detect_project_d2_dir().unwrap_or_else(|| PathBuf::from(PROJECT_D2_DIR))
        })
}

fn dll_path(project_d2_dir: &Path) -> PathBuf {
    project_d2_dir.join(DLL_NAME)
}

fn original_dll_path(project_d2_dir: &Path) -> PathBuf {
    project_d2_dir.join(ORIGINAL_DLL_NAME)
}

fn ini_path(project_d2_dir: &Path) -> PathBuf {
    project_d2_dir.join(INI_NAME)
}

fn log_path() -> PathBuf {
    PathBuf::from(LOG_PATH)
}

fn is_project_d2_dir(path: &Path) -> bool {
    path.is_dir()
        && (path.join("ProjectDiablo.dll").exists()
            || path.join("BH.dll").exists()
            || path.join("Game.exe").exists())
}

fn auto_detect_project_d2_dir() -> Option<PathBuf> {
    let mut candidates = vec![
        PathBuf::from(PROJECT_D2_DIR),
        PathBuf::from(r"C:\Program Files\Diablo II\ProjectD2"),
        PathBuf::from(r"C:\Program Files (x86)\ProjectD2"),
        PathBuf::from(r"C:\Program Files\ProjectD2"),
    ];

    if let Ok(program_files_x86) = std::env::var("ProgramFiles(x86)") {
        candidates.push(
            PathBuf::from(program_files_x86)
                .join("Diablo II")
                .join("ProjectD2"),
        );
    }
    if let Ok(program_files) = std::env::var("ProgramFiles") {
        candidates.push(
            PathBuf::from(program_files)
                .join("Diablo II")
                .join("ProjectD2"),
        );
    }

    candidates.into_iter().find(|path| is_project_d2_dir(path))
}

fn parse_bool_value(value: &str) -> Option<bool> {
    match value.trim().to_ascii_lowercase().as_str() {
        "1" | "true" | "yes" | "on" => Some(true),
        "0" | "false" | "no" | "off" => Some(false),
        _ => None,
    }
}

fn bool_ini(value: bool) -> &'static str {
    if value {
        "1"
    } else {
        "0"
    }
}

fn config_from_ini(contents: &str) -> DropIdentifiedConfig {
    let mut config = DropIdentifiedConfig::default();
    let mut in_drop_section = false;
    let mut found_drop_section = false;

    for raw_line in contents.lines() {
        let line = raw_line.trim();
        if line.starts_with('[') && line.ends_with(']') {
            in_drop_section = line.eq_ignore_ascii_case("[DropIdentified]");
            found_drop_section |= in_drop_section;
            continue;
        }
        if !in_drop_section || line.is_empty() || line.starts_with(';') || line.starts_with('#') {
            continue;
        }
        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        let Some(parsed) = parse_bool_value(value) else {
            continue;
        };
        match key.trim().to_ascii_lowercase().as_str() {
            "magic" => config.magic = parsed,
            "rare" => config.rare = parsed,
            "set" => config.set = parsed,
            "unique" => config.unique = parsed,
            "smallcharm" => config.small_charm = parsed,
            "largecharm" => config.large_charm = parsed,
            "grandcharm" => config.grand_charm = parsed,
            _ => {}
        }
    }

    if found_drop_section {
        config.enabled = config.magic
            || config.rare
            || config.set
            || config.unique
            || config.small_charm
            || config.large_charm
            || config.grand_charm;
    }
    config
}

fn drop_identified_section(config: &DropIdentifiedConfig) -> String {
    let mut active = config.clone();
    if !active.enabled {
        active.magic = false;
        active.rare = false;
        active.set = false;
        active.unique = false;
        active.small_charm = false;
        active.large_charm = false;
        active.grand_charm = false;
    }

    format!(
        "[DropIdentified]\r\n; Set each rarity to 1 (identified) or 0 (unidentified)\r\n; Charm options only affect magic charms and work even when Magic=0.\r\n; Changes take effect on next game launch\r\nMagic={}\r\nRare={}\r\nSet={}\r\nUnique={}\r\nSmallCharm={}\r\nLargeCharm={}\r\nGrandCharm={}\r\n",
        bool_ini(active.magic),
        bool_ini(active.rare),
        bool_ini(active.set),
        bool_ini(active.unique),
        bool_ini(active.small_charm),
        bool_ini(active.large_charm),
        bool_ini(active.grand_charm)
    )
}

fn replace_drop_identified_section(contents: &str, config: &DropIdentifiedConfig) -> String {
    let replacement = drop_identified_section(config);
    let lines: Vec<&str> = contents.lines().collect();
    let start = lines
        .iter()
        .position(|line| line.trim().eq_ignore_ascii_case("[DropIdentified]"));

    let Some(start) = start else {
        if contents.trim().is_empty() {
            return replacement;
        }
        return format!("{}\r\n{}", replacement.trim_end(), contents.trim_start());
    };

    let end = lines
        .iter()
        .enumerate()
        .skip(start + 1)
        .find_map(|(idx, line)| {
            let trimmed = line.trim();
            if trimmed.starts_with('[') && trimmed.ends_with(']') {
                Some(idx)
            } else {
                None
            }
        })
        .unwrap_or(lines.len());

    let before = lines[..start].join("\r\n");
    let after = lines[end..].join("\r\n");
    let mut output = String::new();
    if !before.trim().is_empty() {
        output.push_str(before.trim_end());
        output.push_str("\r\n\r\n");
    }
    output.push_str(replacement.trim_end());
    if !after.trim().is_empty() {
        output.push_str("\r\n\r\n");
        output.push_str(after.trim_start());
    }
    output.push_str("\r\n");
    output
}

fn auto_grail_section(enabled: bool) -> String {
    format!(
        "[AutoGrail]\r\n; Controls whether SoE Companion treats the shared hook as the required Drops Tracker Hook.\r\n; The shared ijl11.dll can also be used for Identified Drops.\r\nEnabled={}\r\nHookVersion={}\r\n",
        bool_ini(enabled),
        DROP_HOOK_VERSION
    )
}

#[derive(Debug, Clone, Default)]
struct AutoGrailConfig {
    enabled: Option<bool>,
    hook_version: Option<String>,
}

fn parse_auto_grail_config(contents: &str) -> AutoGrailConfig {
    let mut in_auto_grail_section = false;
    let mut config = AutoGrailConfig::default();
    for raw_line in contents.lines() {
        let line = raw_line.trim();
        if line.starts_with('[') && line.ends_with(']') {
            in_auto_grail_section = line.eq_ignore_ascii_case("[AutoGrail]");
            continue;
        }
        if !in_auto_grail_section || line.is_empty() || line.starts_with(';') || line.starts_with('#') {
            continue;
        }
        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        let key = key.trim();
        if key.eq_ignore_ascii_case("Enabled") {
            config.enabled = parse_bool_value(value);
        } else if key.eq_ignore_ascii_case("HookVersion") {
            let version = value.trim();
            if !version.is_empty() {
                config.hook_version = Some(version.to_string());
            }
        }
    }
    config
}

fn parse_auto_grail_enabled(contents: &str) -> Option<bool> {
    parse_auto_grail_config(contents).enabled
}

fn replace_auto_grail_section(contents: &str, enabled: bool) -> String {
    let replacement = auto_grail_section(enabled);
    let lines: Vec<&str> = contents.lines().collect();
    let start = lines
        .iter()
        .position(|line| line.trim().eq_ignore_ascii_case("[AutoGrail]"));

    let Some(start) = start else {
        if contents.trim().is_empty() {
            return replacement;
        }
        return format!("{}\r\n{}", replacement.trim_end(), contents.trim_start());
    };

    let end = lines
        .iter()
        .enumerate()
        .skip(start + 1)
        .find_map(|(idx, line)| {
            let trimmed = line.trim();
            if trimmed.starts_with('[') && trimmed.ends_with(']') {
                Some(idx)
            } else {
                None
            }
        })
        .unwrap_or(lines.len());

    let before = lines[..start].join("\r\n");
    let after = lines[end..].join("\r\n");
    let mut output = String::new();
    if !before.trim().is_empty() {
        output.push_str(before.trim_end());
        output.push_str("\r\n\r\n");
    }
    output.push_str(replacement.trim_end());
    if !after.trim().is_empty() {
        output.push_str("\r\n\r\n");
        output.push_str(after.trim_start());
    }
    output.push_str("\r\n");
    output
}

fn fnv1a64(bytes: &[u8]) -> u64 {
    let mut hash = 0xcbf29ce484222325u64;
    for byte in bytes {
        hash ^= *byte as u64;
        hash = hash.wrapping_mul(0x100000001b3);
    }
    hash
}

fn hash_bytes(bytes: &[u8]) -> String {
    format!("{:016x}", fnv1a64(bytes))
}

fn hash_file(path: &Path) -> Option<String> {
    fs::read(path).ok().map(|bytes| hash_bytes(&bytes))
}

fn bytes_contain(haystack: &[u8], needle: &[u8]) -> bool {
    !needle.is_empty() && haystack.windows(needle.len()).any(|window| window == needle)
}

fn dll_looks_like_soe_hook(path: &Path) -> bool {
    let Ok(bytes) = fs::read(path) else {
        return false;
    };
    bytes_contain(&bytes, b"DropIdentified")
        && (bytes_contain(&bytes, b"soe_companion_drops.log")
            || bytes_contain(&bytes, b"grail_drops.log")
            || bytes_contain(&bytes, b"ijl11_orig.dll"))
}

fn timestamp_suffix() -> String {
    chrono::Local::now().format("%Y%m%d-%H%M%S").to_string()
}

fn backup_existing_dll(path: &Path) -> Result<(), String> {
    if !path.exists() {
        return Ok(());
    }
    let backup = path.with_file_name(format!("ijl11_backup_{}.dll", timestamp_suffix()));
    fs::copy(path, &backup).map_err(|e| {
        format!(
            "Failed to backup existing {} to {}: {}",
            path.display(),
            backup.display(),
            e
        )
    })?;
    Ok(())
}

fn has_soe_hook_marker(ini: &Path) -> bool {
    if !ini.exists() {
        return false;
    }
    let Ok(contents) = fs::read_to_string(ini) else {
        return false;
    };
    let auto_grail = parse_auto_grail_config(&contents);
    let identified = config_from_ini(&contents);
    auto_grail.enabled.unwrap_or(false) || identified.enabled
}

fn install_shared_hook_dll(project_dir: &Path) -> Result<(), String> {
    let dll = dll_path(project_dir);
    let original = original_dll_path(project_dir);
    let ini = ini_path(project_dir);
    let bundled_hash = hash_bytes(BUNDLED_DLL);
    let current_hash = hash_file(&dll);
    let already_installed = current_hash
        .as_deref()
        .map(|hash| hash == bundled_hash)
        .unwrap_or(false);

    if already_installed {
        return Ok(());
    }

    let existing_is_managed_hook = dll_looks_like_soe_hook(&dll)
        || (original.exists() && has_soe_hook_marker(&ini));
    if dll.exists() && !original.exists() && !existing_is_managed_hook {
        fs::rename(&dll, &original).map_err(|e| {
            format!(
                "Failed to rename {} to {}. Try running SoE Companion as administrator. {}",
                dll.display(),
                original.display(),
                e
            )
        })?;
    } else if dll.exists() {
        backup_existing_dll(&dll)?;
    }

    fs::write(&dll, BUNDLED_DLL).map_err(|e| {
        format!(
            "Failed to install {}. Try running SoE Companion as administrator. {}",
            dll.display(),
            e
        )
    })?;

    Ok(())
}

fn restore_original_ijl11(project_dir: &Path) -> Result<(), String> {
    let dll = dll_path(project_dir);
    let original = original_dll_path(project_dir);
    let ini = ini_path(project_dir);

    if !original.exists() {
        return Err(format!(
            "No original ijl11.dll backup was found at {}.",
            original.display()
        ));
    }

    let status = build_status(Some(project_dir.display().to_string()), String::new());
    if !status.can_restore_original {
        return Err(
            "The active ijl11.dll is not recognized as a SoE Companion hook, so it was not replaced."
                .to_string(),
        );
    }

    if dll.exists() {
        backup_existing_dll(&dll)?;
        fs::remove_file(&dll).map_err(|e| {
            format!(
                "Failed to remove {}. Try running SoE Companion as administrator. {}",
                dll.display(),
                e
            )
        })?;
    }

    fs::rename(&original, &dll).map_err(|e| {
        format!(
            "Failed to restore {} to {}. Try running SoE Companion as administrator. {}",
            original.display(),
            dll.display(),
            e
        )
    })?;

    let _ = write_ini_with_auto_grail_enabled(&ini, false);
    let _ = write_ini_with_drop_identified_config(&ini, &DropIdentifiedConfig::default());

    Ok(())
}

fn uninstall_shared_hook_dll(project_dir: &Path) -> Result<(), String> {
    let dll = dll_path(project_dir);
    let original = original_dll_path(project_dir);
    let bundled_hash = hash_bytes(BUNDLED_DLL);
    let current_hash = hash_file(&dll);
    let is_bundled_hook = current_hash
        .as_deref()
        .map(|hash| hash == bundled_hash)
        .unwrap_or(false);

    if !is_bundled_hook {
        return Ok(());
    }

    fs::remove_file(&dll).map_err(|e| {
        format!(
            "Failed to remove {}. Try running SoE Companion as administrator. {}",
            dll.display(),
            e
        )
    })?;

    if original.exists() {
        fs::rename(&original, &dll).map_err(|e| {
            format!(
                "Failed to restore {} to {}. Try running SoE Companion as administrator. {}",
                original.display(),
                dll.display(),
                e
            )
        })?;
    }

    Ok(())
}

fn uninstall_shared_hook_dll_if_unused(project_dir: &Path, ini: &Path) -> Result<(), String> {
    let auto_grail_enabled = read_auto_grail_enabled_from_path(ini)?.unwrap_or(false);
    let identified_enabled = read_drop_identified_config_from_path(ini)?.enabled;
    if !auto_grail_enabled && !identified_enabled {
        uninstall_shared_hook_dll(project_dir)?;
    }
    Ok(())
}

fn read_drop_identified_config_from_path(ini: &Path) -> Result<DropIdentifiedConfig, String> {
    if !ini.exists() {
        return Ok(DropIdentifiedConfig::default());
    }
    let contents =
        fs::read_to_string(ini).map_err(|e| format!("Failed to read {}: {}", ini.display(), e))?;
    Ok(config_from_ini(&contents))
}

fn read_auto_grail_enabled_from_path(ini: &Path) -> Result<Option<bool>, String> {
    if !ini.exists() {
        return Ok(None);
    }
    let contents =
        fs::read_to_string(ini).map_err(|e| format!("Failed to read {}: {}", ini.display(), e))?;
    Ok(parse_auto_grail_enabled(&contents))
}

fn read_auto_grail_config_from_path(ini: &Path) -> Result<AutoGrailConfig, String> {
    if !ini.exists() {
        return Ok(AutoGrailConfig::default());
    }
    let contents =
        fs::read_to_string(ini).map_err(|e| format!("Failed to read {}: {}", ini.display(), e))?;
    Ok(parse_auto_grail_config(&contents))
}

fn write_ini_with_drop_identified_config(
    ini: &Path,
    config: &DropIdentifiedConfig,
) -> Result<(), String> {
    let contents = if ini.exists() {
        fs::read_to_string(ini).map_err(|e| format!("Failed to read {}: {}", ini.display(), e))?
    } else {
        BUNDLED_INI.to_string()
    };
    let updated = replace_drop_identified_section(&contents, config);
    fs::write(ini, updated).map_err(|e| {
        format!(
            "Failed to write {}. Try running SoE Companion as administrator. {}",
            ini.display(),
            e
        )
    })
}

fn write_ini_with_auto_grail_enabled(ini: &Path, enabled: bool) -> Result<(), String> {
    let contents = if ini.exists() {
        fs::read_to_string(ini).map_err(|e| format!("Failed to read {}: {}", ini.display(), e))?
    } else {
        BUNDLED_INI.to_string()
    };
    let updated = replace_auto_grail_section(&contents, enabled);
    fs::write(ini, updated).map_err(|e| {
        format!(
            "Failed to write {}. Try running SoE Companion as administrator. {}",
            ini.display(),
            e
        )
    })
}

fn build_status(project_d2_dir: Option<String>, message: String) -> DropHookStatus {
    let project_dir = normalize_project_d2_dir(project_d2_dir);
    let dll = dll_path(&project_dir);
    let original = original_dll_path(&project_dir);
    let ini = ini_path(&project_dir);
    let log = log_path();
    let bundled_hash = hash_bytes(BUNDLED_DLL);
    let current_hash = hash_file(&dll);
    let log_size = fs::metadata(&log).map(|m| m.len()).unwrap_or(0);
    let dll_exists = dll.exists();
    let dll_matches_bundled = current_hash
        .as_deref()
        .map(|hash| hash == bundled_hash)
        .unwrap_or(false);
    let identified_config = read_drop_identified_config_from_path(&ini).unwrap_or_default();
    let auto_grail_config = read_auto_grail_config_from_path(&ini).unwrap_or_default();
    let auto_grail_enabled = auto_grail_config.enabled.unwrap_or(false);
    let dll_looks_managed = dll_exists && dll_looks_like_soe_hook(&dll);
    let ini_looks_managed = original.exists()
        && dll_exists
        && (auto_grail_enabled || identified_config.enabled);
    let hook_version_matches = auto_grail_config
        .hook_version
        .as_deref()
        .map(|version| version == DROP_HOOK_VERSION)
        .unwrap_or(false);
    let managed_hook_present = dll_matches_bundled || dll_looks_managed || ini_looks_managed;
    let installed = dll_matches_bundled && hook_version_matches;
    let hook_needs_update = dll_exists && managed_hook_present && !installed;
    let unknown_dll_present = dll_exists && original.exists() && !managed_hook_present;
    let can_restore_original =
        original.exists() && (!dll_exists || dll_matches_bundled || managed_hook_present);
    let identified_installed = installed && identified_config.enabled;
    let grail_installed = installed && auto_grail_enabled;

    DropHookStatus {
        project_d2_dir: project_dir.display().to_string(),
        dll_path: dll.display().to_string(),
        original_dll_path: original.display().to_string(),
        ini_path: ini.display().to_string(),
        log_path: log.display().to_string(),
        project_d2_dir_exists: project_dir.exists(),
        dll_exists,
        original_dll_exists: original.exists(),
        ini_exists: ini.exists(),
        log_exists: log.exists(),
        log_size,
        installed,
        grail_installed,
        identified_installed,
        dll_matches_bundled,
        managed_hook_present,
        hook_needs_update,
        unknown_dll_present,
        can_restore_original,
        hook_version_matches,
        hook_version: auto_grail_config.hook_version,
        expected_hook_version: DROP_HOOK_VERSION.to_string(),
        current_dll_hash: current_hash,
        bundled_dll_hash: bundled_hash,
        message,
    }
}

#[tauri::command]
pub fn get_drop_hook_status() -> DropHookStatus {
    get_drop_hook_status_for_path(None)
}

#[tauri::command]
pub fn get_drop_hook_status_for_path(project_d2_dir: Option<String>) -> DropHookStatus {
    let status = build_status(project_d2_dir.clone(), String::new());
    let message = if !status.project_d2_dir_exists {
        format!(
            "ProjectD2 folder was not found at {}.",
            status.project_d2_dir
        )
    } else if status.grail_installed && status.identified_installed {
        "Shared SoE hook is installed. Drops Tracker Hook and Identified Drops are enabled."
            .to_string()
    } else if status.grail_installed {
        "Drops Tracker Hook is installed. Identified Drops are off.".to_string()
    } else if status.identified_installed {
        "Identified Drops are installed. Drops Tracker Hook is off.".to_string()
    } else if status.hook_needs_update {
        "Drops Tracker Hook is installed but needs an update for this app version.".to_string()
    } else if status.unknown_dll_present {
        "ProjectD2 has an ijl11.dll that is not recognized as the SoE Companion hook. Installing will back it up before replacing it.".to_string()
    } else if status.installed {
        "Shared SoE hook is installed, but Drops Tracker Hook and Identified Drops are both off."
            .to_string()
    } else if status.dll_exists {
        "ProjectD2 has an ijl11.dll, but it is not the bundled SoE Drops Tracker Hook.".to_string()
    } else {
        "Drops Tracker Hook is not installed.".to_string()
    };
    build_status(project_d2_dir, message)
}

#[tauri::command]
pub fn install_drop_hook() -> Result<DropHookStatus, String> {
    install_auto_grail_hook_for_path(None)
}

#[tauri::command]
pub fn install_drop_hook_for_path(
    project_d2_dir: Option<String>,
) -> Result<DropHookStatus, String> {
    install_auto_grail_hook_for_path(project_d2_dir)
}

#[tauri::command]
pub fn restore_original_ijl11_for_path(
    project_d2_dir: Option<String>,
) -> Result<DropHookStatus, String> {
    let project_dir = normalize_project_d2_dir(project_d2_dir.clone());
    if !project_dir.exists() {
        return Err(format!(
            "ProjectD2 folder was not found at {}.",
            project_dir.display()
        ));
    }

    restore_original_ijl11(&project_dir)?;
    Ok(build_status(
        project_d2_dir,
        "Original ijl11.dll restored. Drops Tracker Hook and Identified Drops are disabled.".to_string(),
    ))
}

#[tauri::command]
pub fn install_auto_grail_hook_for_path(
    project_d2_dir: Option<String>,
) -> Result<DropHookStatus, String> {
    let project_dir = normalize_project_d2_dir(project_d2_dir.clone());
    if !project_dir.exists() {
        return Err(format!(
            "ProjectD2 folder was not found at {}.",
            project_dir.display()
        ));
    }

    let ini = ini_path(&project_dir);
    let previous_status = build_status(project_d2_dir.clone(), String::new());
    install_shared_hook_dll(&project_dir)?;

    if !ini.exists() {
        write_ini_with_drop_identified_config(&ini, &DropIdentifiedConfig::default())?;
    }
    write_ini_with_auto_grail_enabled(&ini, true)?;
    let identified_enabled = read_drop_identified_config_from_path(&ini)?.enabled;

    Ok(build_status(
        project_d2_dir,
        if previous_status.hook_needs_update {
            if identified_enabled {
                "Drops Tracker Hook updated for this app version. Identified Drops remain enabled.".to_string()
            } else {
                "Drops Tracker Hook updated for this app version. Identified Drops remain off.".to_string()
            }
        } else if previous_status.unknown_dll_present {
            if identified_enabled {
                "Unknown ijl11.dll backed up and Drops Tracker Hook installed. Identified Drops remain enabled.".to_string()
            } else {
                "Unknown ijl11.dll backed up and Drops Tracker Hook installed. Identified Drops remain off.".to_string()
            }
        } else if identified_enabled {
            "Drops Tracker Hook installed. Identified Drops remain enabled.".to_string()
        } else {
            "Drops Tracker Hook installed. Identified Drops remain off.".to_string()
        },
    ))
}

#[tauri::command]
pub fn install_identified_drops_hook_for_path(
    project_d2_dir: Option<String>,
) -> Result<DropHookStatus, String> {
    let project_dir = normalize_project_d2_dir(project_d2_dir.clone());
    if !project_dir.exists() {
        return Err(format!(
            "ProjectD2 folder was not found at {}.",
            project_dir.display()
        ));
    }

    install_shared_hook_dll(&project_dir)?;

    let ini = ini_path(&project_dir);
    let current = read_drop_identified_config_from_path(&ini)?;
    let current_auto_grail = read_auto_grail_enabled_from_path(&ini)?;
    let next = if current.enabled {
        current
    } else {
        DropIdentifiedConfig::enabled_defaults()
    };
    write_ini_with_drop_identified_config(&ini, &next)?;
    let next_auto_grail = current_auto_grail.unwrap_or(false);
    write_ini_with_auto_grail_enabled(&ini, next_auto_grail)?;

    Ok(build_status(
        project_d2_dir,
        if next_auto_grail {
            "Identified Drops installed. Drops Tracker Hook remains enabled.".to_string()
        } else {
            "Identified Drops installed. Drops Tracker Hook remains off.".to_string()
        },
    ))
}

#[tauri::command]
pub fn uninstall_auto_grail_hook_for_path(
    project_d2_dir: Option<String>,
) -> Result<DropHookStatus, String> {
    let project_dir = normalize_project_d2_dir(project_d2_dir.clone());
    if !project_dir.exists() {
        return Err(format!(
            "ProjectD2 folder was not found at {}.",
            project_dir.display()
        ));
    }

    let ini = ini_path(&project_dir);
    write_ini_with_auto_grail_enabled(&ini, false)?;
    uninstall_shared_hook_dll_if_unused(&project_dir, &ini)?;
    let identified_enabled = read_drop_identified_config_from_path(&ini)?.enabled;

    Ok(build_status(
        project_d2_dir,
        if identified_enabled {
            "Drops Tracker Hook removed. Identified Drops remain installed.".to_string()
        } else {
            "Drops Tracker Hook removed. Shared hook restored/removed.".to_string()
        },
    ))
}

#[tauri::command]
pub fn uninstall_identified_drops_hook_for_path(
    project_d2_dir: Option<String>,
) -> Result<DropHookStatus, String> {
    let project_dir = normalize_project_d2_dir(project_d2_dir.clone());
    if !project_dir.exists() {
        return Err(format!(
            "ProjectD2 folder was not found at {}.",
            project_dir.display()
        ));
    }

    let ini = ini_path(&project_dir);
    write_ini_with_drop_identified_config(&ini, &DropIdentifiedConfig::default())?;
    uninstall_shared_hook_dll_if_unused(&project_dir, &ini)?;
    let auto_grail_enabled = read_auto_grail_enabled_from_path(&ini)?.unwrap_or(false);

    Ok(build_status(
        project_d2_dir,
        if auto_grail_enabled {
            "Identified Drops removed. Drops Tracker Hook remains installed.".to_string()
        } else {
            "Identified Drops removed. Shared hook restored/removed.".to_string()
        },
    ))
}

#[tauri::command]
pub fn get_drop_identified_config() -> Result<DropIdentifiedConfig, String> {
    get_drop_identified_config_for_path(None)
}

#[tauri::command]
pub fn get_drop_identified_config_for_path(
    project_d2_dir: Option<String>,
) -> Result<DropIdentifiedConfig, String> {
    let project_dir = normalize_project_d2_dir(project_d2_dir);
    let ini = ini_path(&project_dir);
    read_drop_identified_config_from_path(&ini)
}

#[tauri::command]
pub fn write_drop_identified_config(
    config: DropIdentifiedConfig,
) -> Result<DropIdentifiedConfig, String> {
    write_drop_identified_config_for_path(config, None)
}

#[tauri::command]
pub fn write_drop_identified_config_for_path(
    config: DropIdentifiedConfig,
    project_d2_dir: Option<String>,
) -> Result<DropIdentifiedConfig, String> {
    let project_dir = normalize_project_d2_dir(project_d2_dir.clone());
    if !project_dir.exists() {
        return Err(format!(
            "ProjectD2 folder was not found at {}.",
            project_dir.display()
        ));
    }

    let ini = ini_path(&project_dir);
    write_ini_with_drop_identified_config(&ini, &config)?;
    get_drop_identified_config_for_path(project_d2_dir)
}

#[tauri::command]
pub fn detect_project_d2_dirs() -> Vec<String> {
    auto_detect_project_d2_dir()
        .into_iter()
        .map(|path| path.display().to_string())
        .collect()
}
