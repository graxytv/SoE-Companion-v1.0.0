import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

const trackerSourcePath = resolve("src/lib/gsf-tracker.ts");
const tabSourcePath = resolve("src/views/GSFTrackerTab.svelte");
const trackerSource = readFileSync(trackerSourcePath, "utf8");
const tabSource = readFileSync(tabSourcePath, "utf8");

assert.match(
  trackerSource,
  /if \(quality === "unique" && text\.includes\("jewel"\)\) return "uniqueJewel";\s*if \(uniqueKind === "tu"\) return "tu";/s,
  "unique jewels must be classified before unique tier buckets",
);

assert.match(
  trackerSource,
  /export function isGsfSlotCompatible\(/,
  "GSF matching should use a central slot compatibility helper",
);

assert.match(
  trackerSource,
  /if \(wantedSlot === "any" \|\| wantedSlot === "other"\) return true;/,
  "unknown/other wanted slots should stay permissive for exact item-name matches",
);

assert.match(
  trackerSource,
  /return value\.replace\(\/\\s\+\(\?:TU\|SU\|SSU\|SSSU\)\$\/i, ""\)\.trim\(\);/,
  "drop names should strip backend-appended unique kind suffixes before matching",
);

assert.match(
  trackerSource,
  /const playerName = player\.name\.trim\(\) \|\| "Unnamed Player";/,
  "blank player names should match with the same fallback shown in the UI",
);

assert.match(
  tabSource,
  /return inferred === 'other' \? 'any' : inferred;/,
  "catalog selections with unknown slots should store any, not other",
);

const overlaySource = readFileSync(resolve("src/views/OverlayWindow.svelte"), "utf8");
assert.match(
  overlaySource,
  /const gsfMatches = gsfMatchesForDrop\(item\);\s*const gsfNeededBy = gsfNotificationEnabled \? summarizeGsfNeededBy\(gsfMatches\) : \[\];/s,
  "GSF matches should be computed independently from notification text visibility",
);
assert.match(
  overlaySource,
  /if \(gsfMatches\.length > 0 && gsfSoundSlot != null\)/,
  "GSF sound should play for matches even when notification text is disabled",
);

const activeSettingsSource = readFileSync(resolve("src-tauri/src/settings.rs"), "utf8");
assert.match(activeSettingsSource, /pub struct GsfWantedItem/, "active Tauri settings must persist GSF wanted items");
assert.match(activeSettingsSource, /pub gsf_players: Vec<GsfPlayer>/, "active Tauri settings must persist GSF players");

const normalizeGsfItemName = (value) =>
  String(value ?? "")
    .trim()
    .toLowerCase()
    .replace(/\s+/g, " ");

const bestGsfDropName = (item) => {
  const name = String(item.name ?? "").trim();
  return name.replace(/\s+(?:TU|SU|SSU|SSSU)$/i, "").trim();
};

const isGsfSlotCompatible = (wantedSlot, dropSlot) => {
  if (wantedSlot === "any" || wantedSlot === "other") return true;
  if (wantedSlot === dropSlot) return true;
  return wantedSlot === "offhand" && (dropSlot === "weapon" || dropSlot === "shield");
};

const inferGsfDropCategory = (item) => {
  const uniqueKind = item.unique_kind ?? item.uniqueKind ?? null;
  const quality = String(item.quality ?? "").toLowerCase();
  const text = [item.name, item.base_name, item.baseName, item.category]
    .filter(Boolean)
    .join(" ")
    .toLowerCase();

  if (quality === "unique" && text.includes("jewel")) return "uniqueJewel";
  if (uniqueKind === "tu") return "tu";
  if (uniqueKind === "su") return "su";
  if (uniqueKind === "ssu") return "ssu";
  if (uniqueKind === "sssu") return "sssu";
  if (quality === "set") return "set";
  return null;
};

const matchGsfDrop = (item, players) => {
  const category = inferGsfDropCategory(item);
  if (!category) return [];
  const dropName = normalizeGsfItemName(bestGsfDropName(item));
  const dropSlot = item.dropSlot;
  const matches = [];
  for (const player of players) {
    const playerName = player.name.trim() || "Unnamed Player";
    for (const wanted of player.wantedItems ?? []) {
      if (wanted.status !== "needed") continue;
      if (wanted.category !== category) continue;
      const wantedName = wanted.normalizedItemName || normalizeGsfItemName(wanted.itemName);
      if (wantedName !== dropName) continue;
      if (!isGsfSlotCompatible(wanted.slot, dropSlot)) continue;
      matches.push(playerName);
    }
  }
  return matches;
};

assert.equal(
  inferGsfDropCategory({
    quality: "Unique",
    name: "Heavenstone",
    base_name: "Jewel",
    unique_kind: "su",
  }),
  "uniqueJewel",
  "unique jewel drops should not be misclassified as SU",
);

assert.deepEqual(
  matchGsfDrop(
    {
      quality: "Unique",
      name: "Adrenaline Rush",
      base_name: "Light Gauntlets",
      unique_kind: "su",
      dropSlot: "gloves",
    },
    [
      {
        name: "",
        wantedItems: [
          {
            itemName: "Adrenaline Rush",
            normalizedItemName: "adrenaline rush",
            category: "su",
            slot: "other",
            status: "needed",
          },
        ],
      },
    ],
  ),
  ["Unnamed Player"],
  "legacy GSF entries stored with slot=other and blank player names should still match exact item drops",
);

assert.deepEqual(
  matchGsfDrop(
    {
      quality: "Unique",
      name: "Windforce SU",
      base_name: "Long War Bow",
      unique_kind: "su",
      dropSlot: "weapon",
    },
    [
      {
        name: "Bowzon",
        wantedItems: [
          {
            itemName: "Windforce",
            normalizedItemName: "windforce",
            category: "su",
            slot: "any",
            status: "needed",
          },
        ],
      },
    ],
  ),
  ["Bowzon"],
  "backend-appended unique kind suffixes should not prevent GSF matches",
);

assert.equal(isGsfSlotCompatible("offhand", "shield"), true);
assert.equal(isGsfSlotCompatible("offhand", "weapon"), true);
assert.equal(isGsfSlotCompatible("offhand", "helm"), false);

console.log("GSF tracker verification passed");
