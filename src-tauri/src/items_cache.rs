//! Items cache stub for SoE Companion.
//!
//! The MXL live scanner cached the item dictionary to disk so it survived
//! restarts. In PD2 the scanner doesn't run, so we provide no-op stubs
//! that satisfy the call sites in main.rs.

use crate::notifier::ItemsDictionary;
use tauri::{AppHandle, Manager};

/// Save the items dictionary to the app data directory.
/// No-op in SoE Companion — the PD2 scanner is not active.
pub fn save_items_cache(_app: &AppHandle, _dict: &ItemsDictionary) -> Result<(), String> {
    Ok(())
}

/// Load a previously cached items dictionary.
/// Always returns None in SoE Companion.
pub fn load_items_cache(_app: &AppHandle) -> Option<ItemsDictionary> {
    None
}
