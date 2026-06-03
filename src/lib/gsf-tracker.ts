import {
  normalizeGsfItemName,
  type GsfItemCategory,
  type GsfItemSlot,
} from "./gsf-item-catalog";

export type GsfWantedStatus = "needed" | "found" | "skipped";

export interface GsfWantedItem {
  id: string;
  itemName: string;
  normalizedItemName: string;
  category: GsfItemCategory;
  slot: GsfItemSlot;
  status: GsfWantedStatus;
  notes: string;
  createdAt: string;
  updatedAt: string;
  foundAt: string | null;
}

export interface GsfPlayer {
  id: string;
  name: string;
  className: string;
  buildName: string;
  notes: string;
  wantedItems: GsfWantedItem[];
  createdAt: string;
  updatedAt: string;
}

type UniqueKind = "tu" | "su" | "ssu" | "sssu";

export interface GsfDropLike {
  quality?: string | null;
  name?: string | null;
  base_name?: string | null;
  baseName?: string | null;
  canonical_name?: string | null;
  canonicalName?: string | null;
  category?: string | null;
  unique_kind?: UniqueKind | null;
  uniqueKind?: UniqueKind | null;
}

export interface GsfMatch {
  playerId: string;
  playerName: string;
  wantedItemId: string;
  itemName: string;
  category: GsfItemCategory;
  slot: GsfItemSlot;
}

const GSF_WEAPON_BASES = [
  "Short Sword", "Scimitar", "Saber", "Falchion", "Broad Sword", "Long Sword", "War Sword",
  "Two-Handed Sword", "Claymore", "Giant Sword", "Bastard Sword", "Flamberge", "Great Sword",
  "Crystal Sword", "Hand Axe", "Axe", "Double Axe", "Military Pick", "War Axe",
  "Large Axe", "Broad Axe", "Battle Axe", "Great Axe", "Giant Axe",
  "Club", "Spiked Club", "Mace", "Morning Star", "Flail",
  "War Hammer", "Maul", "Great Maul", "Scepter", "Grand Scepter", "War Scepter",
  "Javelin", "Pilum", "Short Spear", "Glaive", "Throwing Spear",
  "Spear", "Trident", "Brandistock", "Spetum", "Pike", "Scythe",
  "Dagger", "Dirk", "Kriss", "Blade", "Throwing Knife", "Flying Knife", "Balanced Knife",
  "Throwing Axe", "Balanced Axe", "Short Staff", "Long Staff", "Gnarled Staff", "Battle Staff", "War Staff",
  "Short Bow", "Hunter's Bow", "Long Bow", "Composite Bow", "Short Battle Bow", "Long Battle Bow", "Short War Bow", "Long War Bow",
  "Light Crossbow", "Crossbow", "Heavy Crossbow", "Repeating Crossbow",
  "Stag Bow", "Reflex Bow", "Maiden Spear", "Maiden Pike", "Maiden Javelin",
  "Katar", "Wrist Blade", "Hatchet Hands", "Cestus", "Claws", "Blade Talons", "Scissors Katar",
  "Halberd", "Naginata", "Spatha", "Backsword", "Ida", "Bronze Sword", "Kriegsmesser",
  "Mammen Axe", "Hammerhead Axe", "Ono", "Valaska", "Labrys",
  "Compound Bow", "Serpent Bow", "Maple Bow", "Viper Bow", "Recurve Bow", "Flamen Staff",
  "Raptor Scythe", "Bonesplitter", "Marrow Staff", "Hexblade", "Spirit Edge",
  "Needle Crossbow", "Dart Thrower", "Stinger Crossbow", "Trebuchet",
  "Wand", "Yew Wand", "Bone Wand", "Grim Wand",
  "Bonebreaker", "Goedendag", "Angel Star", "Hand of God", "Holy Lance", "Tepoztopilli",
  "Eagle Orb", "Sacred Globe", "Smoked Sphere", "Clasped Orb", "Jared's Stone", "Warp Blade",
];

const GSF_CHEST_BASES = [
  "Quilted Armor", "Leather Armor", "Hard Leather Armor", "Studded Leather", "Ring Mail", "Scale Mail",
  "Chain Mail", "Breast Plate", "Splint Mail", "Plate Mail", "Field Plate", "Light Plate",
  "Gothic Plate", "Full Plate Mail", "Ancient Armor",
  "Gambeson", "Kazarghand", "Lamellar Armor", "Banded Plate", "Ceremonial Armor",
];

const GSF_HELM_BASES = [
  "Cap", "Skull Cap", "Helm", "Full Helm", "Great Helm", "Crown",
  "Circlet", "Coronet", "Tiara", "Diadem", "Mask", "Bone Helm",
  "Morion", "Cervelliere", "Einherjar Helm", "Spangenhelm",
  "Jawbone Cap", "Fanged Helm", "Horned Helm", "Assault Helmet", "Avenger Guard",
  "Wolf Head", "Hawk Helm", "Antlers", "Falcon Mask", "Spirit Mask",
  "Hundsgugel", "Blackguard Helm",
];

const GSF_SHIELD_BASES = [
  "Buckler", "Small Shield", "Large Shield", "Kite Shield", "Tower Shield", "Gothic Shield",
  "Bone Shield", "Spiked Shield", "Athulua's Hand", "Phoenix Shield", "Setzschild",
  "Parma", "Aspis", "Totem Shield", "Bladed Shield",
  "Bull Shield", "Bronze Shield", "Gilded Shield",
  "Preserved Head", "Zombie Head", "Unraveller Head", "Gargoyle Head", "Demon Head",
  "Targe", "Rondache", "Heraldic Shield", "Aerin Shield", "Crown Shield",
];

const GSF_BELT_BASES = ["Sash", "Light Belt", "Belt", "Heavy Belt", "Plated Belt"];
const GSF_GLOVE_BASES = ["Leather Gloves", "Heavy Gloves", "Chain Gloves", "Light Gauntlets", "Gauntlets"];
const GSF_BOOT_BASES = ["Boots", "Heavy Boots", "Chain Boots", "Light Plated Boots", "Greaves"];

function isPlaceholderItemName(value: unknown): boolean {
  return /^Item #\d+(?:\s+(?:SU|SSU|SSSU|TU))?$/i.test(
    String(value ?? "").trim(),
  );
}

function escapeRegExp(value: string): string {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function hasBaseName(text: string, bases: readonly string[]): boolean {
  return bases.some((base) => new RegExp(`\\b${escapeRegExp(base.toLowerCase())}\\b`).test(text));
}

function stripTrailingUniqueKind(value: string): string {
  return value.replace(/\s+(?:TU|SU|SSU|SSSU)$/i, "").trim();
}

export function bestGsfDropName(item: GsfDropLike): string {
  const canonicalName = String(item.canonical_name ?? item.canonicalName ?? "").trim();
  if (canonicalName) return stripTrailingUniqueKind(canonicalName);
  const name = String(item.name ?? "").trim();
  if (name && !isPlaceholderItemName(name)) return stripTrailingUniqueKind(name);
  return stripTrailingUniqueKind(String(item.base_name ?? item.baseName ?? name).trim());
}

export function inferGsfDropCategory(
  item: GsfDropLike,
): GsfItemCategory | null {
  const uniqueKind = item.unique_kind ?? item.uniqueKind ?? null;
  const quality = String(item.quality ?? "").toLowerCase();
  const text = [item.canonical_name, item.canonicalName, item.name, item.base_name, item.baseName, item.category]
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
}

export function inferGsfSlot(item: GsfDropLike): GsfItemSlot {
  const text = [item.canonical_name, item.canonicalName, item.name, item.base_name, item.baseName, item.category]
    .filter(Boolean)
    .join(" ")
    .toLowerCase();

  if (/\b(jewel)\b/.test(text)) return "jewel";
  if (/\b(ring)\b/.test(text)) return "ring";
  if (/\b(amulet|talisman|lamen)\b/.test(text)) return "amulet";
  if (hasBaseName(text, GSF_SHIELD_BASES)) return "shield";
  if (hasBaseName(text, GSF_HELM_BASES)) return "helm";
  if (hasBaseName(text, GSF_CHEST_BASES)) return "chest";
  if (hasBaseName(text, GSF_GLOVE_BASES)) return "gloves";
  if (hasBaseName(text, GSF_BOOT_BASES)) return "boots";
  if (hasBaseName(text, GSF_BELT_BASES)) return "belt";
  if (hasBaseName(text, GSF_WEAPON_BASES)) return "weapon";
  if (/\b(boots?|greaves?|sabatons?|shoes?|treks?)\b/.test(text)) return "boots";
  if (/\b(gloves?|gauntlets?|mitts?|fist|grip)\b/.test(text)) return "gloves";
  if (/\b(belt|sash|girdle|cord)\b/.test(text)) return "belt";
  if (/\b(helm|helmet|crown|cap|mask|visage|cowl|hood|pelt|skull|horns?|circlet|coronet|tiara|diadem)\b/.test(text)) return "helm";
  if (/\b(armor|armour|mail|plate|robe|vest|chest|skin|hide|carapace|dress|mantle|cloak|suit)\b/.test(text)) return "chest";
  if (/\b(shield|buckler|ward|aegis|barrier|defender|head)\b/.test(text)) return "shield";
  if (/\b(sword|axe|mace|club|hammer|spear|polearm|bow|crossbow|staff|wand|orb|knife|dagger|claw|javelin|throwing|flail|scythe|blade|pike|scepter|rod|maul|reaver|cutlass|hwacha|naginata)\b/.test(text)) return "weapon";
  return "other";
}

export function matchGsfDrop(
  item: GsfDropLike,
  players: readonly GsfPlayer[],
): GsfMatch[] {
  const category = inferGsfDropCategory(item);
  if (!category) return [];

  const dropName = normalizeGsfItemName(bestGsfDropName(item));
  if (!dropName) return [];

  const dropSlot = inferGsfSlot(item);
  const matches: GsfMatch[] = [];

  for (const player of players) {
    const playerName = player.name.trim() || "Unnamed Player";
    for (const wanted of player.wantedItems ?? []) {
      if (wanted.status !== "needed") continue;
      if (wanted.category !== category) continue;
      const wantedName =
        wanted.normalizedItemName || normalizeGsfItemName(wanted.itemName);
      if (wantedName !== dropName) continue;
      if (!isGsfSlotCompatible(wanted.slot, dropSlot)) continue;
      matches.push({
        playerId: player.id,
        playerName,
        wantedItemId: wanted.id,
        itemName: wanted.itemName,
        category,
        slot: wanted.slot,
      });
    }
  }

  return matches;
}

export function isGsfSlotCompatible(
  wantedSlot: GsfItemSlot,
  dropSlot: GsfItemSlot,
): boolean {
  if (wantedSlot === "any" || wantedSlot === "other") return true;
  if (wantedSlot === dropSlot) return true;
  return wantedSlot === "offhand" && (dropSlot === "weapon" || dropSlot === "shield");
}

export function summarizeGsfNeededBy(matches: readonly GsfMatch[]): string[] {
  return Array.from(new Set(matches.map((match) => match.playerName))).filter(Boolean);
}
