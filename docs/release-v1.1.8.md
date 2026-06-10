# SoE Companion v1.1.8

This release is a major foundation update for performance and drop accuracy.

## Major Foundation Change: Hook-Based Drop Tracking

SoE Companion now moves drop tracking away from constant live polling and toward the bundled Drops Tracker Hook. The hook records structured drop events from the game, and the app consumes those events during safe sync moments such as manual Sync All, Save & Exit, and zone-transition sync.

This is intended to reduce the rhythmic one-second stutters players were seeing while still keeping drops, Holy Grail progress, Fate Cards, runes, materials, sounds, and overlays accurate.

## What's Changed

- Added the new Drops Tracker Hook as the required companion hook for drop tracking.
- Added hook version checking so the app can tell when an installed `ijl11.dll` is outdated for the current SoE Companion version.
- Added hook update/install handling so users can update the hook without manually deleting files.
- Added an option to restore the original `ijl11.dll` when users want to remove the SoE Companion hook.
- Moved Auto Grail behavior under the Drops Tracker Hook flow instead of treating it as a separate grail-only hook.
- Added structured hook drop-event reading from `C:\soe_companion_drops.log`.
- Added processed hook event tracking so old hook events are not replayed.
- Reduced reliance on high-frequency live scanner/polling work for better in-game performance.
- Added quieter sync behavior around Save & Exit and zone transitions.
- Updated overlay refresh behavior so Holy Grail overlay progress updates when grail state changes, including Fate Card full-stack completions.
- Updated the app version display and package metadata to `v1.1.8`.

## Notes

- Players should install or update the Drops Tracker Hook from the Drops Tracker tab or Setup Wizard.
- If an older hook is already installed, the app should report that the hook needs an update and allow installing the current version.
- If a player restores the original `ijl11.dll`, drop tracking features that rely on the hook will not work until the hook is reinstalled.
