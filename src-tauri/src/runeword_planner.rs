use serde::Serialize;
use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;

const RUNE_NAMES: &[&str] = &[
    "El", "Eld", "Tir", "Nef", "Eth", "Ith", "Tal", "Ral", "Ort", "Thul", "Amn", "Sol", "Shael",
    "Dol", "Hel", "Io", "Lum", "Ko", "Fal", "Lem", "Pul", "Um", "Mal", "Ist", "Gul", "Vex", "Ohm",
    "Lo", "Sur", "Ber", "Jah", "Cham", "Zod",
];

const FATE_CARD_NAMES: &[&str] = &[
    "The Apothecary",
    "The Eye of Terror",
    "The Doctor",
    "The Sephiroth",
    "The Immortal",
    "Wealth and Power",
    "House of Mirrors",
    "Unrequited Love",
    "Seraphic Favor",
    "Alluring Bounty",
    "A Fate Worse Than Death",
    "A Stone Perfected",
    "Lonely Warrior",
    "The Polymath",
    "Iridescent Dream",
    "The Web",
    "I see Brothers",
    "The Reaper",
    "The Shieldbearer",
    "The Avenger",
    "Gemcutter's Promise",
    "Chaotic Disposition",
    "The Unexpected Prize",
    "Further Invention",
    "Gemcutter's Mercy",
    "Lost Worlds",
    "Boundless Realms",
    "The Journey",
    "Cartographer's Delight",
    "The Cartographer",
    "More is Never Enough",
    "Ambush",
    "The Artist",
    "Misery in Darkness",
    "Bowyer's Dream",
    "Dormant Allure",
    "The Assegai",
    "The Rite of Elements",
    "Call of the First Ones",
    "Therianthropy",
    "Cursed Words",
    "The Dark Mage",
    "The Lich",
    "The Celestial Justicar",
    "The Crusader",
    "The Bulwark",
    "A Chilling Wind",
    "The Blazing Fire",
    "The Coming Storm",
    "The Warlord",
    "The Battle Born",
    "The Skirmisher",
    "The Undaunted",
    "Gemcutter's Gift",
    "The Archmage's Right Hand",
    "The Survivalist",
    "Abandoned Wealth",
    "The Scout",
    "Glimmer of Hope",
    "The Escape",
    "Humility",
    "The Saint's Treasure",
    "The Inventor",
];

#[derive(Debug, Serialize)]
pub struct RuneStashSyncResult {
    pub counts: HashMap<String, u32>,
    pub fate_card_counts: HashMap<String, u32>,
    pub fate_card_sync_available: bool,
    pub scanned_files: Vec<String>,
    pub message: String,
}

fn empty_counts() -> HashMap<String, u32> {
    RUNE_NAMES
        .iter()
        .map(|rune| ((*rune).to_string(), 0))
        .collect()
}

fn empty_fate_card_counts() -> HashMap<String, u32> {
    FATE_CARD_NAMES
        .iter()
        .map(|card| ((*card).to_string(), 0))
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
    paths.push(PathBuf::from(
        r"C:\Program Files (x86)\Diablo II\Save\pd2_shared.stash",
    ));
    paths.push(PathBuf::from(
        r"C:\Program Files\Diablo II\Save\pd2_shared.stash",
    ));
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

fn material_counter_slots(bytes: &[u8]) -> Vec<u32> {
    if bytes.len() < 4 {
        return Vec::new();
    }

    for offset in 0..bytes.len().saturating_sub(4) {
        if bytes[offset] != b'c' || bytes[offset + 1] != b'u' {
            continue;
        }

        let counts_start = offset + 2;
        let counts_end = bytes[counts_start..]
            .windows(2)
            .position(|window| window == b"JM")
            .map(|pos| counts_start + pos)
            .unwrap_or(bytes.len());
        let counts_end = counts_start + ((counts_end.saturating_sub(counts_start)) / 2) * 2;
        if counts_end <= counts_start {
            continue;
        }

        let mut parsed = Vec::new();
        let mut plausible = true;
        for start in (counts_start..counts_end).step_by(2) {
            let value = u16::from_le_bytes([bytes[start], bytes[start + 1]]) as u32;
            if value > 9999 {
                plausible = false;
                break;
            }
            parsed.push(value);
        }

        if plausible {
            return parsed;
        }
    }

    Vec::new()
}

fn read_bits(bytes: &[u8], bit_offset: usize, bit_count: usize) -> Option<u32> {
    if bit_count == 0 || bit_count > 32 {
        return None;
    }
    let end_bit = bit_offset.checked_add(bit_count)?;
    if end_bit > bytes.len() * 8 {
        return None;
    }

    let mut value = 0u32;
    for bit in 0..bit_count {
        let absolute = bit_offset + bit;
        let byte = bytes[absolute / 8];
        if ((byte >> (absolute % 8)) & 1) != 0 {
            value |= 1u32 << bit;
        }
    }
    Some(value)
}

fn is_d2_item_list_header(bytes: &[u8], offset: usize) -> bool {
    offset + 5 < bytes.len()
        && bytes[offset] == b'J'
        && bytes[offset + 1] == b'M'
        && bytes[offset + 4] == b'J'
        && bytes[offset + 5] == b'M'
}

fn shared_stash_has_item_list(bytes: &[u8]) -> bool {
    bytes
        .windows(6)
        .enumerate()
        .any(|(offset, _)| is_d2_item_list_header(bytes, offset))
}

fn d2_item_code(bytes: &[u8], item_offset: usize) -> Option<String> {
    if item_offset + 14 > bytes.len()
        || bytes[item_offset] != b'J'
        || bytes[item_offset + 1] != b'M'
    {
        return None;
    }

    let base_bit = item_offset.checked_mul(8)?;
    let mut code = String::new();
    for bit in [76usize, 84, 92, 100] {
        let ch = read_bits(bytes, base_bit + bit, 8)? as u8;
        if ch == 0 || ch == b' ' {
            break;
        }
        if !ch.is_ascii_alphanumeric() {
            return None;
        }
        code.push(char::from(ch).to_ascii_lowercase());
    }

    if code.is_empty() {
        None
    } else {
        Some(code)
    }
}

fn fate_card_name_from_code(code: &str) -> Option<&'static str> {
    let suffix = code
        .trim()
        .to_ascii_lowercase()
        .strip_prefix("fa")?
        .to_string();
    let index = suffix.parse::<usize>().ok()?.checked_sub(1)?;
    FATE_CARD_NAMES.get(index).copied()
}

fn d2_stack_quantity(bytes: &[u8], item_offset: usize) -> u32 {
    let Some(base_bit) = item_offset.checked_mul(8) else {
        return 1;
    };

    if read_bits(bytes, base_bit + 37, 1).unwrap_or(1) == 1 {
        return 1;
    }

    let rarity = read_bits(bytes, base_bit + 150, 4).unwrap_or(0);
    let mut bit = base_bit + 154;

    if read_bits(bytes, bit, 1).unwrap_or(0) == 1 {
        bit += 4;
    } else {
        bit += 1;
    }

    if read_bits(bytes, bit, 1).unwrap_or(0) == 1 {
        bit += 12;
    } else {
        bit += 1;
    }

    match rarity {
        1 | 3 => bit += 3,
        4 => bit += 22,
        5 | 7 => bit += 12,
        6 | 8 => {
            bit += 16;
            for _ in 0..6 {
                let has_affix = read_bits(bytes, bit, 1).unwrap_or(0);
                bit += 1;
                if has_affix == 1 {
                    bit += 11;
                }
            }
        }
        _ => {}
    }

    if read_bits(bytes, base_bit + 42, 1).unwrap_or(0) == 1 {
        bit += 16;
    }

    if read_bits(bytes, base_bit + 40, 1).unwrap_or(0) == 1 {
        for _ in 0..16 {
            let Some(ch) = read_bits(bytes, bit, 7) else {
                return 1;
            };
            bit += 7;
            if ch == 0 {
                break;
            }
        }
    }

    bit += 1;
    let quantity = read_bits(bytes, bit, 9).unwrap_or(1);
    if quantity == 0 || quantity > 999 {
        1
    } else {
        quantity
    }
}

fn material_fate_card_counts(bytes: &[u8]) -> HashMap<String, u32> {
    let mut counts = empty_fate_card_counts();
    if bytes.len() < 14 {
        return counts;
    }

    for offset in 0..bytes.len().saturating_sub(1) {
        if bytes[offset] != b'J' || bytes[offset + 1] != b'M' {
            continue;
        }
        if is_d2_item_list_header(bytes, offset) {
            continue;
        }

        let Some(code) = d2_item_code(bytes, offset) else {
            continue;
        };
        let Some(name) = fate_card_name_from_code(&code) else {
            continue;
        };

        *counts.entry(name.to_string()).or_insert(0) += d2_stack_quantity(bytes, offset);
    }

    counts
}

fn selected_or_detected_stash_path(stash_path: Option<String>) -> Option<PathBuf> {
    if let Some(path) = stash_path
        .map(|path| path.trim().to_string())
        .filter(|path| !path.is_empty())
    {
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
    let mut fate_card_counts = empty_fate_card_counts();
    let mut scanned_files = Vec::new();

    let Some(path) = selected_or_detected_stash_path(stash_path) else {
        return Ok(RuneStashSyncResult {
            counts,
            fate_card_counts,
            fate_card_sync_available: false,
            scanned_files,
            message: "No pd2_shared.stash file was found. Shared-stash sync could not run."
                .to_string(),
        });
    };

    if !path.is_file() {
        return Err(format!(
            "Selected shared stash file does not exist: {}",
            path.display()
        ));
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
    fate_card_counts = material_fate_card_counts(&bytes);

    let total: u32 = counts.values().copied().sum();
    let fate_card_total: u32 = fate_card_counts.values().copied().sum();
    let fate_card_sync_available = shared_stash_has_item_list(&bytes);
    let message = if total == 0 && fate_card_total == 0 {
        "No runes or Fate Cards were found in the shared stash.".to_string()
    } else if fate_card_total == 0 {
        "Rune materials synced. No Fate Cards were found in shared stash item slots.".to_string()
    } else if total == 0 {
        format!(
            "Synced {} Fate Card{} from shared stash item slots. No runes were found in the materials tab.",
            fate_card_total,
            if fate_card_total == 1 { "" } else { "s" }
        )
    } else {
        format!(
            "Rune materials synced and {} Fate Card{} synced from shared stash item slots.",
            fate_card_total,
            if fate_card_total == 1 { "" } else { "s" }
        )
    };

    Ok(RuneStashSyncResult {
        counts,
        fate_card_counts,
        fate_card_sync_available,
        scanned_files,
        message,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn bytes(hex: &[u8]) -> Vec<u8> {
        hex.to_vec()
    }

    #[test]
    fn decodes_fate_card_code_and_quantity() {
        let card = bytes(&[
            0x4a, 0x4d, 0x10, 0x00, 0x80, 0x00, 0x67, 0x20, 0x61, 0x6c, 0x16, 0x66, 0x23, 0x03,
            0xce, 0xc1, 0xe8, 0xa6, 0xaa, 0xc0, 0xc0, 0xff,
        ]);

        assert_eq!(d2_item_code(&card, 0).as_deref(), Some("fa62"));
        assert_eq!(d2_stack_quantity(&card, 0), 6);
    }

    #[test]
    fn counts_fate_card_stacks_from_item_list() {
        let mut stash = bytes(&[0x4a, 0x4d, 0x02, 0x00]);
        stash.extend_from_slice(&[
            0x4a, 0x4d, 0x10, 0x00, 0x80, 0x00, 0x67, 0x20, 0x61, 0x6c, 0x16, 0x66, 0x23, 0x03,
            0xce, 0xc1, 0xe8, 0xa6, 0xaa, 0xc0, 0xc0, 0xff, 0x4a, 0x4d, 0x10, 0x00, 0x80, 0x00,
            0x67, 0x20, 0xa1, 0x6c, 0x16, 0x36, 0x73, 0x03, 0x62, 0x80, 0xd4, 0x0e, 0xac, 0x20,
            0xc0, 0xff,
        ]);

        let counts = material_fate_card_counts(&stash);
        assert_eq!(counts.get("The Saint's Treasure").copied(), Some(6));
        assert_eq!(counts.get("The Assegai").copied(), Some(1));
        assert!(shared_stash_has_item_list(&stash));
    }
}
