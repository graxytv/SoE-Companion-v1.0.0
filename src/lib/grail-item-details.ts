import uniquesJson from "./data/soe-uniques.json";
import type { HolyGrailItem } from "./holy-grail";
import { holyGrailCategoryLabel, normalizeHolyGrailKey } from "./holy-grail";
import { fateCardInfo } from "./soe-13-items";
import { SOE_RUNEWORDS } from "./runewords";

interface WikiBase {
  displayName?: string | null;
  name?: string | null;
  itemTier?: string | null;
  requiredStrength?: string | number | null;
  requiredDexterity?: string | number | null;
  maxSockets?: string | number | null;
}

interface WikiUnique {
  index?: string | null;
  displayName?: string | null;
  requiredLevel?: string | number | null;
  level?: string | number | null;
  itemTier?: string | null;
  hellforged?: boolean | string | number | null;
  armorBase?: WikiBase | null;
  weaponBase?: WikiBase | null;
  jeweleryBase?: WikiBase | null;
  displayOneHandDamage?: string | null;
  displayTwoHandDamage?: string | null;
  displayDefense?: string | null;
  displayDurability?: string | null;
  displayProperties?: string[] | null;
  dropSource?: string | null;
}

export interface GrailItemDetails {
  title: string;
  subtitle: string;
  meta: Array<{ label: string; value: string }>;
  properties: string[];
  note?: string;
}

function cleanWikiText(value: unknown): string {
  return String(value ?? "")
    .replace(/\[([^\]]+)\]\([^)]+\)/g, "$1")
    .replace(/\s+/g, " ")
    .trim();
}

function valueText(value: unknown): string | null {
  const text = cleanWikiText(value);
  return text && text !== "0" ? text : null;
}

function baseName(unique: WikiUnique): string | null {
  const base = unique.weaponBase ?? unique.armorBase ?? unique.jeweleryBase ?? null;
  return valueText(base?.displayName ?? base?.name);
}

function baseMeta(unique: WikiUnique): Array<{ label: string; value: string }> {
  const base = unique.weaponBase ?? unique.armorBase ?? unique.jeweleryBase ?? null;
  const rows: Array<{ label: string; value: string }> = [];
  const baseDisplay = baseName(unique);
  if (baseDisplay) rows.push({ label: "Base", value: baseDisplay });
  const tier = valueText(base?.itemTier ?? unique.itemTier);
  if (tier) rows.push({ label: "Tier", value: tier });
  const reqLevel = valueText(unique.requiredLevel);
  if (reqLevel) rows.push({ label: "Required Level", value: reqLevel });
  const qualityLevel = valueText(unique.level);
  if (qualityLevel) rows.push({ label: "Quality Level", value: qualityLevel });
  const reqStrength = valueText(base?.requiredStrength);
  if (reqStrength) rows.push({ label: "Required Strength", value: reqStrength });
  const reqDexterity = valueText(base?.requiredDexterity);
  if (reqDexterity) rows.push({ label: "Required Dexterity", value: reqDexterity });
  const sockets = valueText(base?.maxSockets);
  if (sockets) rows.push({ label: "Max Sockets", value: sockets });
  return rows;
}

function uniqueCombatMeta(unique: WikiUnique): Array<{ label: string; value: string }> {
  const rows: Array<{ label: string; value: string }> = [];
  const oneHand = valueText(unique.displayOneHandDamage);
  if (oneHand) rows.push({ label: "One-Hand Damage", value: oneHand });
  const twoHand = valueText(unique.displayTwoHandDamage);
  if (twoHand) rows.push({ label: "Two-Hand Damage", value: twoHand });
  const defense = valueText(unique.displayDefense);
  if (defense) rows.push({ label: "Defense", value: defense });
  const durability = valueText(unique.displayDurability);
  if (durability) rows.push({ label: "Durability", value: durability });
  return rows;
}

const uniquesByName = new Map<string, WikiUnique>(
  (uniquesJson as WikiUnique[]).flatMap((unique) => {
    const names = [unique.displayName, unique.index].map(valueText).filter((name): name is string => !!name);
    return names.map((name) => [normalizeHolyGrailKey(name), unique] as const);
  }),
);

export function detailsForGrailItem(item: HolyGrailItem): GrailItemDetails {
  if (item.category === "runewords") {
    const runeword = SOE_RUNEWORDS.find((candidate) => candidate.key === item.key);
    return {
      title: item.name,
      subtitle: "Runeword",
      meta: [
        { label: "Runes", value: (runeword?.runes ?? item.runes ?? []).join(" - ") },
        { label: "Bases", value: (runeword?.bases ?? item.bases ?? []).join(", ") },
        ...(runeword?.requiredLevel ? [{ label: "Required Level", value: String(runeword.requiredLevel) }] : []),
      ].filter((row) => row.value),
      properties: runeword?.properties ?? [],
    };
  }

  if (item.category === "su" || item.category === "ssu") {
    const unique = uniquesByName.get(normalizeHolyGrailKey(item.name));
    if (unique) {
      return {
        title: valueText(unique.displayName) ?? item.name,
        subtitle: item.category === "ssu" ? "Hellforged Unique" : "Unique",
        meta: [...baseMeta(unique), ...uniqueCombatMeta(unique)],
        properties: (unique.displayProperties ?? []).map(cleanWikiText).filter(Boolean),
        note: valueText(unique.dropSource) ? `Drop source: ${valueText(unique.dropSource)}` : undefined,
      };
    }
  }

  if (item.category === "fateCards") {
    const card = fateCardInfo(item.name);
    if (card) {
      return {
        title: card.name,
        subtitle: `Fate Card - Tier ${card.tier}`,
        meta: [
          { label: "Card Tier", value: String(card.tier) },
          { label: "Cards Needed", value: String(card.amountRequired) },
          { label: "Drop Location", value: card.dropLocation },
        ],
        properties: [`Full stack reward: ${card.reward}`],
      };
    }
  }

  return {
    title: item.name,
    subtitle: holyGrailCategoryLabel(item.category),
    meta: [],
    properties: [],
    note: item.category === "sets"
      ? "Set stat data is not exposed in the current Archivist wiki export yet."
      : "Detailed stat data is not bundled for this item yet.",
  };
}
