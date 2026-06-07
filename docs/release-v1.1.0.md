## What's Changed Since v1.0.0

### SoE 13.0.0 Item Support
- Added the new SoE 13.0.0 item data for Fate Cards, Hatred Orbs, Essences, Ascendancy items, new uniques, and new materials.
- Added Fate Cards, Hatred Orbs, Essences, and Ascendancy as Holy Grail/checklist categories.
- Added full Fate Card metadata including card tier, full stack size, and full stack reward descriptions.
- Updated the bundled Hiim_SOE loot filter for the new patch items and in-game notifier rules.
- Added `docs/soe-13-new-items-filter-codes.txt` with category, item name, and filter code.

### Fate Cards
- Added a dedicated Fate Cards main tab with Card List, Overlay Settings, and Backup sub-tabs.
- Fate Card stash counts now sync from `pd2_shared.stash` item stacks.
- Fate Card Grail completion now requires a full stack instead of finding one card.
- Added Fate Card filtering by owned cards, incomplete cards, and full stacks.
- Added a Fate Cards overlay that can track specific cards and/or whole card tiers.
- Added Fate Card achievements for total cards found and Tier 0 card milestones.

### Tracking And Sync
- Added a header `Sync All` button that refreshes shared stash data, Fate Cards, account stats, and character levels.
- Removed scattered per-page sync buttons in favor of the single master sync button.
- Improved Fate Card drop tracking, duplicate handling, stash syncing, and crash resistance.
- Fixed Holy Grail latest-drop behavior so Fate Card stash sync does not spam the latest item.
- Added automatic Fate Card shared-stash sync every 30 seconds.

### Overlays
- Added reliable native drag-to-move overlay layout mode.
- Added resize support for movable overlays.
- Added the Achievement popup overlay to layout editing.
- Removed overlay X/Y and width/height slider controls now that overlays can be dragged/resized directly.
- Updated the setup wizard with a move/resize overlays button.
- Improved edit-mode overlay matching so edit windows more closely reflect the final overlay layout.

### Drops Tracker
- Moved Identified Drops into its own Drops Tracker sub-tab.
- Added a clear note that Diablo II must be closed before installing the hook or changing Identified Drops settings.
- Updated Drops Tracker and Total Drops behavior for new patch categories and Fate Cards.
- Removed unnecessary Fate Card, Essence, and Ascendancy items from the Mats Tracker overlay.

### Sounds And Notifications
- Moved Item Sounds from Notifications into the Sounds tab.
- Split Fate Cards and Essences into their own Item Sounds categories.
- Removed quality-level suffixes from unique item dropdown labels in Item Sounds.
- Kept quality-level text searchable behind the scenes.

### Achievements And Grail
- Added category-completion achievements for new Grail categories.
- Added bossing sub-tabs for easier achievement browsing.
- Added character-level syncing for level achievements.
- Removed unreliable corruption tracking and perfect gems from material achievements.
- Updated Grail item detail views for new Fate Card fields and descriptions.

### UI And Setup
- Removed the unused Breakpoints tab/code.
- Cleaned up the setup wizard and overlay setup flow.
- Moved Donate to the footer and made Sync All the primary header action.
- Renamed the General tab account section to Reset Account Stats with clearer reset wording.
- Added line numbers to the loot filter editor.

### Updates
- Added GitHub release updater wiring for post-1.0 releases.
