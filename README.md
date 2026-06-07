# SoE Companion

SoE Companion is a Windows companion app for Sanctuary of Exile built around drop tracking, Holy Grail progress, Fate Cards tracking, achievements, loot filter editing, item sounds/notifications and movable in-game overlays.

The app is designed for players who want all their progress in one place!
It started as a way for me to track item drops and turned into a larger project.

PSA: This app is best used on a fresh start! You will need to delete your pd2_shared.stash file found in your save folder
to delete account stats data. If you don't, you will have previous data bleeding into a new run. Just deleting your
character files WILL NOT delete account data. THIS WILL DELETE YOUR STASH!

If you do not care about tracking achievements, you can disable them and not worry about deleting your pd2_shared.stash file.

SoE Companion does not work with Plugy and currently doesn't not fully support damnation mode.
Everything will work in damnation mode, but you won't be able to complete some achievements.

[Download the latest release](https://github.com/graxytv/SoE-Companion-App/releases/latest)

![SoE Companion Home](docs/screenshots/home.png)

## Highlights

- Live drop tracking for uniques,sets, runes, runewords, Fate Cards, currency orbs, essences, ascendancy items, all SoE specific items and more!
- Automatic Holy Grail Tracking
- Fate Card stash sync with card tiers, full stack sizes, full stack rewards, and full-stack grail completion.
- Drag-and-resize overlay layout mode for the in-game tracker cards.
- Customizable notification popups and item-specific sound rules.
- Built-in loot filter editor
- Account-wide and automatic achievement tracking including bossing, level tracking, grail progress, item drops, monster kills and much more.
- Master `Sync All` button for all trackers.
- GitHub release updater from the General tab so you can always stay up-to-date without losing any data!

## Screenshots

| General Settings | Drops Tracker |
| --- | --- |
| ![General settings](docs/screenshots/general.png) | ![Drops tracker](docs/screenshots/drops-tracker.png) |

| Loot Filter | Sounds |
| --- | --- |
| ![Loot filter editor](docs/screenshots/loot-filter.png) | ![Sounds tab](docs/screenshots/sounds.png) |

| Fate Cards | Holy Grail |
| --- | --- |
| ![Fate Cards tab](docs/screenshots/fate-cards.png) | ![Holy Grail tab](docs/screenshots/holy-grail.png) |

| Achievements |
| --- |
| ![Achievements tab](docs/screenshots/achievements.png) |

## Installation

1. Open the [latest GitHub Release](https://github.com/graxytv/SoE-Companion-App/releases/latest).
2. Download `soe-companion.exe`.
3. Run the app on Windows.
4. Use the `SoE Companion Setup Wizard` from the Home tab to configure paths, the loot filter, the drop hook, sounds, overlays, stash sync, and hotkeys.

Some actions touch Diablo II files. Close Diablo II before installing the Auto Grail Tracker hook, installing or changing Identified Drops settings, or resetting account stats.

## Main Tabs

### Home

The Home tab is the launchpad for the app.

- Shows game status and current app version.
- Provides the `Play` button for starting SoE Launcher.
- Opens the setup wizard.
- Summarizes total kills, achievement unlocks, and Holy Grail progress.

### General

General contains app-wide settings and maintenance tools.

- Theme selection.
- GitHub release update checks and install flow.
- `Always Show Overlays` for keeping overlays visible outside game focus.
- `Edit Overlay Layout` for moving and resizing overlay windows.
- Hotkeys for toggling the main app window and overlay layout editor.
- New Game Automation/Macro for quickly making new games when farming.
- Reset All Account Stats, including local stash data and account-settings.

### Drops Tracker

The Drops Tracker tab controls live drop counting and the tracker overlays.

- `Overview` shows recently tracked items and lets you remove accidental duplicate entries.
- `Identified Drops` installs and configures the hook that can make selected item qualities drop identified.
- `Tracker Settings` controls shared tracker overlay behavior.
- `Drops Tracker Overlay Settings` controls the resettable session tracker.
- `Total Drops Overlay Settings` controls persistent lifetime totals.
- `Mats Tracker` lets you choose which SoE materials show in the material overlay.
- `Rune Tracker` lets you choose which runes show in the rune overlay.
- `Muling Mode` pauses drop, grail, recent item, run-count, and timer tracking while moving items.

### Loot Filter

The Loot Filter tab is a built-in editor for the bundled SoE filter.

- Line numbers beside the editor for easier support and debugging.
- Install instructions for setting up your filter.
- Copy, rename, delete, and save controls.
- Bundled `Hiim_SOE` filter with Season 13 support.
- SoE Season 13 item rules for Fate Cards, essences, Hatred Orbs, ascendancy items, and currency/material additions.

### Notifications

Notifications controls visual item popup behavior.

- Enable or disable the notification overlay.
- Adjust display duration.
- Scale popup size.
- Adjust background opacity.

Sounds are configured separately in the Sounds tab so notification visuals and audio rules stay easy to reason about.

### Sounds

Sounds manages both reusable sound slots and item-specific sound rules.

- Master volume.
- Sound slots with per-slot labels, volume, preview, replacement, and clearing.
- FilterBlade community sound download helper.
- Item Sounds sub-tab for assigning sounds to individual items or categories.
- Item sound categories for uniques, sets, runes, Fate Cards, essences, materials, runewords, hellforged items, and custom rules.

### Fate Cards

Fate Cards is a dedicated tab for SoE card tracking.

- Reads Fate Card stacks from the selected `pd2_shared.stash`.
- Auto-syncs about every 30 seconds.
- Also refreshes from the header `Sync All` button.
- Shows total cards, card types owned, total full stacks, and last sync time.
- Lists each card with card tier, current count, full stack size, total full stacks, and full stack reward.
- Click a card to view its details and reward text.
- Filters by all cards, owned cards, full stacks, or incomplete stacks.
- Full stacks automatically complete Fate Card Holy Grail entries.
- Overlay settings let you track specific cards, full card tiers, or a mix of both.
- Backup and restore support keeps Fate Card progress safer between changes.

### Holy Grail

Holy Grail tracks checklist-style collection progress.

- Categories: Unique, Hellforged, Set Items, Runes, Runewords, Fate Cards, Hatred Orbs, Essences, and Ascendancy.
- Fate Card grail completion requires a full stack, not just one card.
- Search, filter, and sort checklist entries.
- Click items for details when item data is available.
- `What RW Can I Make?` syncs runes from shared stash and shows makeable runewords.
- `Install Auto Grail Tracker` installs and checks the drop hook.
- Overlay settings control the compact Grail Progress overlay.
- Notification settings control first-time grail item alerts.
- Backup and restore support protects grail progress.

### Achievements

Achievements tracks account-wide goals and unlocks.

- Categories for unique finds, kills, bossing, levels, grail completion, runes, Fate Cards, and materials.
- Bossing achievements are split into focused boss sub-tabs like DClone, Rathma, Lucion, Kiln, Uber Tristram, Uber Ancients, act bosses, map bosses, and dungeon bosses.
- Fate Card milestones include total Fate Card finds and Tier 0 Fate Card find goals.
- Character level achievements can sync from character data and can be manually edited as a fallback.
- Achievement unlock popups have their own display settings.
- Achievement progress overlay can be moved and resized from overlay layout mode.
- Full backup and category snapshot support.

### SoE Wiki

The SoE Wiki tab embeds the wiki/reference experience directly in the app so players can check information without leaving the companion.

## Overlays

SoE Companion can render multiple movable overlays:

- Item notifications.
- Drops Tracker.
- Total Drops.
- Grail Progress.
- Rune Tracker.
- Mats Tracker.
- Fate Cards.
- Achievement Progress.
- Achievement unlock popup.
- Monster Kills.
- Muling indicator.

Use `General > Edit Overlay Layout` or the edit-overlay hotkey to enter layout mode. In layout mode, overlay cards can be dragged and resized directly instead of using X/Y or width/height sliders.

## Syncing

The header `Sync All` button is the main sync entry point.

It refreshes shared-stash data, Fate Card stacks, rune materials for runeword planning, account stats, and character levels. Fate Cards and runeword materials also auto-sync periodically when their tabs are open.

## Data And Backups

The app stores progress locally. Holy Grail, Fate Cards, and Achievements include backup and restore tools so progress can be recovered or moved more safely.
