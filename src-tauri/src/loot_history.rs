//! Loot history for SoE Companion.
//!
//! Tracks items seen during a session. In MXL this was populated by the
//! live D2Sigma scanner; in PD2 the scanner is inactive so no entries are
//! ever pushed via the scanner path, but the data structures must exist
//! so the rest of the codebase compiles unchanged.

use serde::{Deserialize, Serialize};
use std::time::{SystemTime, UNIX_EPOCH};

/// Current time in milliseconds since UNIX epoch.
pub fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as u64)
        .unwrap_or(0)
}

/// Result of attempting to push an entry into the history.
#[derive(Debug, Clone, PartialEq)]
pub enum PushOutcome {
    Inserted,
    Duplicate,
}

/// Pickup lifecycle of a dropped item.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "snake_case")]
pub enum PickupState {
    Pending,
    PickedUp,
    Lost,
}

/// A single loot-history entry.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LootEntry {
    pub unit_id: u32,
    pub seed: u32,
    pub timestamp_ms: u64,
    pub name: String,
    pub quality: String,
    pub color: Option<String>,
    pub pickup: PickupState,
    pub unique_kind: Option<String>,
    pub is_relic: bool,
}

/// Session loot history.
#[derive(Debug, Default)]
pub struct LootHistory {
    entries: Vec<LootEntry>,
}

impl LootHistory {
    pub fn new() -> Self {
        Self::default()
    }

    /// Push a new entry. Returns `Duplicate` only when an item with the same
    /// key is still pending on the ground. Picked-up/lost entries should not
    /// suppress later drops that reuse a seed, which can happen with stackable
    /// SoE items.
    pub fn push(&mut self, entry: LootEntry) -> PushOutcome {
        let key = Self::key(&entry);
        if self
            .entries
            .iter()
            .any(|e| Self::key(e) == key && e.pickup == PickupState::Pending)
        {
            return PushOutcome::Duplicate;
        }
        self.entries.push(entry);
        PushOutcome::Inserted
    }

    /// Returns true if any entry is still in the `Pending` state.
    pub fn has_pending(&self) -> bool {
        self.entries
            .iter()
            .any(|e| e.pickup == PickupState::Pending)
    }

    /// Marks entries whose `unit_id` appears in `ids` as `PickedUp`.
    /// Returns a list of `(unit_id, seed, new_state)` tuples for the
    /// caller to emit as pickup-update events.
    pub fn resolve_pending(
        &mut self,
        ids: &std::collections::HashSet<u32>,
    ) -> Vec<(u32, u32, PickupState)> {
        let mut resolved = Vec::new();
        for entry in &mut self.entries {
            if entry.pickup == PickupState::Pending && ids.contains(&entry.unit_id) {
                entry.pickup = PickupState::PickedUp;
                resolved.push((entry.unit_id, entry.seed, PickupState::PickedUp));
            }
        }
        resolved
    }

    /// Transitions all `Pending` entries to `Lost`.
    /// Returns `(unit_id, seed, Lost)` tuples for each affected entry.
    pub fn mark_all_pending_lost(&mut self) -> Vec<(u32, u32, PickupState)> {
        let mut lost = Vec::new();
        for entry in &mut self.entries {
            if entry.pickup == PickupState::Pending {
                entry.pickup = PickupState::Lost;
                lost.push((entry.unit_id, entry.seed, PickupState::Lost));
            }
        }
        lost
    }

    /// Returns a clone of all entries.
    pub fn snapshot(&self) -> Vec<LootEntry> {
        self.entries.clone()
    }

    /// Clears all entries.
    pub fn clear(&mut self) {
        self.entries.clear();
    }

    fn key(e: &LootEntry) -> u32 {
        if e.seed != 0 {
            e.seed
        } else {
            e.unit_id
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entry(seed: u32, unit_id: u32, pickup: PickupState) -> LootEntry {
        LootEntry {
            unit_id,
            seed,
            timestamp_ms: 0,
            name: "A Fate Worse Than Death".to_string(),
            quality: "Normal".to_string(),
            color: None,
            pickup,
            unique_kind: None,
            is_relic: false,
        }
    }

    #[test]
    fn pending_entry_blocks_same_key() {
        let mut history = LootHistory::new();
        assert_eq!(
            history.push(entry(123, 1, PickupState::Pending)),
            PushOutcome::Inserted
        );
        assert_eq!(
            history.push(entry(123, 2, PickupState::Pending)),
            PushOutcome::Duplicate
        );
    }

    #[test]
    fn picked_up_entry_does_not_block_same_key() {
        let mut history = LootHistory::new();
        assert_eq!(
            history.push(entry(123, 1, PickupState::PickedUp)),
            PushOutcome::Inserted
        );
        assert_eq!(
            history.push(entry(123, 2, PickupState::Pending)),
            PushOutcome::Inserted
        );
    }

    #[test]
    fn lost_entry_does_not_block_same_key() {
        let mut history = LootHistory::new();
        assert_eq!(
            history.push(entry(123, 1, PickupState::Lost)),
            PushOutcome::Inserted
        );
        assert_eq!(
            history.push(entry(123, 2, PickupState::Pending)),
            PushOutcome::Inserted
        );
    }
}
