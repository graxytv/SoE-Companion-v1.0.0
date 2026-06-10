# Hook Drop Event Log

SoE Companion can now consume structured drop events from a future `ijl11.dll`
hook without using loot-filter display text. The hook should append one JSON
object per physical ground-drop event to:

```text
C:\soe_companion_drops.log
```

Each line should be valid UTF-8 JSON followed by `\n`. The app reads this log on
manual sync and quiet sync triggers, applies each event through the same Holy
Grail/Drops Tracker/Fate Card/Material tracker code used by the Rust scanner,
then remembers processed event IDs to avoid replaying old lines.

## Recommended Line Shape

```json
{"eventId":"drop:13F5C120:6A91B003","unitId":334872864,"seed":1787936771,"itemCode":"r01","quality":"Normal","name":"El Rune","baseName":"El Rune","canonicalName":"El Rune","mode":3,"isIdentified":true,"isRuneword":false,"fileIndex":610,"source":"ijl11-drop-hook","nameSource":"item-code"}
```

## Fields

- `eventId`: Stable unique ID for this physical drop. Prefer a combination of
  drop unit pointer or unit ID, seed, item code, quality, and spawn frame/tick if
  available.
- `unitId`: Diablo item unit ID, if available.
- `seed`: Item seed. This is useful for dedupe when unit IDs change after zone
  transitions.
- `itemCode`: Three or four character item code from the game data files.
- `quality`: Game item quality, for example `Normal`, `Magic`, `Rare`, `Set`,
  `Unique`, or `Runeword`.
- `name`: Best known display name. For code-only events, this can be the base
  name. SoE Companion can infer runes, fate cards, materials, and unambiguous
  uniques from item codes.
- `baseName`: Base item name from the item code.
- `canonicalName`: Exact unique/set/material/fate-card name when the hook can
  resolve it safely.
- `mode`: Item unit mode. Ground items should normally be mode `3`.
- `isIdentified`: Whether the item is identified.
- `isRuneword`: Whether the item is a runeword.
- `fileIndex`: Item txt row index, if available.
- `isHellforged`: Set this to `true` for Hellforged uniques when the hook can
  identify that variant.
- `source`: Suggested value: `ijl11-drop-hook`.
- `nameSource`: Suggested values: `item-code`, `txt-file`, `unique-table`, or
  `hook`.

The reader also accepts snake_case aliases such as `event_id`, `item_code`,
`base_name`, `canonical_name`, and `is_identified`.

## Compatibility Notes

The existing bundled `ijl11.dll` is an IJL proxy and already supports the Auto
Grail/Identified Drops behavior through `DropIdentified.ini` and
`C:\grail_drops.log`. A replacement hook must preserve:

- IJL proxy exports: `ijlFree`, `ijlGetErrorString`, `ijlGetLibVersion`,
  `ijlInit`, `ijlRead`, and `ijlWrite`.
- Forwarding to `ijl11_orig.dll`.
- Existing `DropIdentified.ini` behavior.
- Existing Auto Grail log behavior until the structured hook fully replaces it.

The safest next DLL-side hook target is a ground-item creation or ground-item
hover/update path that has access to the item unit pointer plus item data. The
current app-side reader does not require constant live polling; it only needs the
hook to append reliable structured events as drops happen.
