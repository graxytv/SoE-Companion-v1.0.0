//! Grail drop log watcher for SoE Companion
//!
//! Watches C:\grail_drops.log (written by the ijl11.dll hook)
//! and emits "grail-drop" events to the frontend whenever a new
//! unique or set item is appended.
//!
//! Each line in the log is: "ItemName|quality\n"
//! Comment lines starting with # are ignored.

use std::fs::File;
use std::io::{BufRead, BufReader, Seek, SeekFrom};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;

use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct GrailDropEvent {
    pub item_name: String,
    pub quality: String,
}

pub struct GrailLogWatcher {
    stop: Arc<AtomicBool>,
}

impl GrailLogWatcher {
    pub fn new() -> Self {
        Self {
            stop: Arc::new(AtomicBool::new(false)),
        }
    }

    /// Start watching the log file. Spawns a background thread.
    pub fn start(&self, app: AppHandle) {
        let stop = self.stop.clone();
        thread::spawn(move || {
            let log_path = PathBuf::from(r"C:\grail_drops.log");
            let mut last_pos: u64 = 0;

            // If file exists, start from end so we don't replay old entries on startup
            if let Ok(meta) = std::fs::metadata(&log_path) {
                last_pos = meta.len();
            }

            while !stop.load(Ordering::Relaxed) {
                thread::sleep(Duration::from_millis(500));

                let meta = match std::fs::metadata(&log_path) {
                    Ok(m) => m,
                    Err(_) => continue,
                };

                let current_len = meta.len();

                // File was truncated or replaced — reset
                if current_len < last_pos {
                    last_pos = current_len;
                    continue;
                }

                if current_len <= last_pos {
                    continue;
                }

                // Read new lines
                let file = match File::open(&log_path) {
                    Ok(f) => f,
                    Err(_) => continue,
                };

                let mut reader = BufReader::new(file);
                if reader.seek(SeekFrom::Start(last_pos)).is_err() {
                    continue;
                }

                let mut new_pos = last_pos;
                let mut line = String::new();

                loop {
                    line.clear();
                    match reader.read_line(&mut line) {
                        Ok(0) => break,
                        Ok(n) => {
                            new_pos += n as u64;
                            let trimmed = line.trim();

                            // Skip comments and empty lines
                            if trimmed.is_empty() || trimmed.starts_with('#') {
                                continue;
                            }

                            // Parse "ItemName|quality"
                            if let Some((name, quality)) = trimmed.split_once('|') {
                                let event = GrailDropEvent {
                                    item_name: name.trim().to_string(),
                                    quality: quality.trim().to_string(),
                                };
                                let _ = app.emit("grail-drop", event);
                            }
                        }
                        Err(_) => break,
                    }
                }

                last_pos = new_pos;
            }
        });
    }

    pub fn stop(&self) {
        self.stop.store(true, Ordering::Relaxed);
    }
}

impl Default for GrailLogWatcher {
    fn default() -> Self {
        Self::new()
    }
}

/// Tauri command: read the full log and return all parsed drop entries.
/// Used to import drops from a previous session.
#[tauri::command]
pub fn read_grail_log() -> Vec<GrailDropEvent> {
    let log_path = PathBuf::from(r"C:\grail_drops.log");
    let file = match File::open(&log_path) {
        Ok(f) => f,
        Err(_) => return vec![],
    };

    let reader = BufReader::new(file);
    let mut out = Vec::new();

    for line in reader.lines().flatten() {
        let trimmed = line.trim().to_string();
        if trimmed.is_empty() || trimmed.starts_with('#') {
            continue;
        }
        if let Some((name, quality)) = trimmed.split_once('|') {
            out.push(GrailDropEvent {
                item_name: name.trim().to_string(),
                quality: quality.trim().to_string(),
            });
        }
    }

    out
}

/// Tauri command: clear the grail log file.
#[tauri::command]
pub fn clear_grail_log() -> Result<(), String> {
    let log_path = PathBuf::from(r"C:\grail_drops.log");
    std::fs::write(&log_path, "").map_err(|e| e.to_string())
}
