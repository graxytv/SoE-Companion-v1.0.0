use std::fs;
use std::path::PathBuf;

const PROJECT_D2_DIR: &str = r"C:\Program Files (x86)\Diablo II\ProjectD2";
const INI_FILE: &str = "DropIdentified.ini";

fn automation_ini_path(project_d2_dir: Option<String>) -> PathBuf {
    project_d2_dir
        .map(|path| PathBuf::from(path.trim_matches('"').trim()))
        .filter(|path| !path.as_os_str().is_empty())
        .unwrap_or_else(|| PathBuf::from(PROJECT_D2_DIR))
        .join(INI_FILE)
}

fn normalize_difficulty(value: &str) -> &'static str {
    match value.trim().to_ascii_lowercase().as_str() {
        "nightmare" | "n" => "Nightmare",
        "hell" | "h" => "Hell",
        _ => "Normal",
    }
}

fn clamp_i32(value: i32, min: i32, max: i32) -> i32 {
    value.max(min).min(max)
}

fn set_ini_key(lines: &mut Vec<String>, section: &str, key: &str, value: &str) {
    let section_header = format!("[{}]", section);
    let key_prefix = format!("{}=", key);
    let mut section_start = None;
    let mut section_end = lines.len();

    for (idx, line) in lines.iter().enumerate() {
        let trimmed = line.trim();
        if trimmed.eq_ignore_ascii_case(&section_header) {
            section_start = Some(idx);
            continue;
        }
        if section_start.is_some() && trimmed.starts_with('[') && trimmed.ends_with(']') {
            section_end = idx;
            break;
        }
    }

    let start = match section_start {
        Some(idx) => idx,
        None => {
            if !lines.last().is_none_or(|line| line.trim().is_empty()) {
                lines.push(String::new());
            }
            lines.push(section_header);
            section_end = lines.len();
            lines.len() - 1
        }
    };

    for idx in (start + 1)..section_end {
        if lines[idx].trim_start().to_ascii_lowercase().starts_with(&key_prefix.to_ascii_lowercase()) {
            lines[idx] = format!("{}={}", key, value);
            return;
        }
    }

    lines.insert(section_end, format!("{}={}", key, value));
}

#[tauri::command]
pub fn write_save_exit_automation_config(
    difficulty: String,
    click_x: i32,
    click_y: i32,
    coordinate_mode_percent: bool,
    delay_ms: u32,
    main_menu_wait_ms: u32,
    hotkey_key_code: u32,
    hotkey_modifiers: u32,
    project_d2_dir: Option<String>,
) -> Result<String, String> {
    let path = automation_ini_path(project_d2_dir);
    let mut lines = if path.exists() {
        fs::read_to_string(&path)
            .map_err(|e| format!("Failed to read {}: {}", path.display(), e))?
            .lines()
            .map(str::to_string)
            .collect::<Vec<_>>()
    } else {
        vec![
            "[DropIdentified]".to_string(),
            "Magic=1".to_string(),
            "Rare=1".to_string(),
            "Set=1".to_string(),
            "Unique=1".to_string(),
        ]
    };

    let max_coord = if coordinate_mode_percent { 100 } else { 10000 };
    let click_x = clamp_i32(click_x, 0, max_coord);
    let click_y = clamp_i32(click_y, 0, max_coord);
    let delay_ms = delay_ms.clamp(50, 2000);
    let main_menu_wait_ms = main_menu_wait_ms.clamp(500, 30000);
    let hotkey_key_code = hotkey_key_code.min(u16::MAX as u32);
    let hotkey_modifiers = hotkey_modifiers & 0x000F;
    let difficulty = normalize_difficulty(&difficulty);

    let section = "SaveExitAutomation";
    set_ini_key(&mut lines, section, "DelayMs", &delay_ms.to_string());
    set_ini_key(&mut lines, section, "MainMenuWaitMs", &main_menu_wait_ms.to_string());
    set_ini_key(&mut lines, section, "MainMenuStableMs", "800");
    set_ini_key(
        &mut lines,
        section,
        "CoordinateModePercent",
        if coordinate_mode_percent { "1" } else { "0" },
    );
    set_ini_key(&mut lines, section, "ClickX", &click_x.to_string());
    set_ini_key(&mut lines, section, "ClickY", &click_y.to_string());
    set_ini_key(&mut lines, section, "Difficulty", difficulty);
    set_ini_key(&mut lines, section, "HotkeyVk", &hotkey_key_code.to_string());
    set_ini_key(&mut lines, section, "HotkeyModifiers", &hotkey_modifiers.to_string());
    set_ini_key(&mut lines, section, "Step1", "ESC");
    set_ini_key(&mut lines, section, "Step2", "UP");
    set_ini_key(&mut lines, section, "Step3", "ENTER");
    set_ini_key(&mut lines, section, "Step4", "WAIT_MAIN_MENU");
    set_ini_key(&mut lines, section, "Step5", "CLICK");
    set_ini_key(&mut lines, section, "Step6", "ENTER");
    set_ini_key(&mut lines, section, "Step7", "DIFFICULTY");

    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|e| format!("Failed to create {}: {}", parent.display(), e))?;
    }
    fs::write(&path, format!("{}\r\n", lines.join("\r\n")))
        .map_err(|e| format!("Failed to write {}: {}", path.display(), e))?;

    Ok(path.display().to_string())
}
