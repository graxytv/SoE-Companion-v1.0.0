import type { ItemsDictionary } from "../stores/items-dictionary.svelte";
import uniquesJson from "../../public/soe-wiki-cache/data/Uniques.json";
import {
  STATIC_SET_ITEMS,
  STATIC_HELLFORGED_UNIQUE_ITEMS,
  STATIC_UNIQUE_ITEMS,
} from "./holy-grail-static";
import { RUNE_NAMES, SOE_RUNEWORDS, runeNameFromCode, runeNameFromText, type RuneName } from "./runewords";
import { uniqueQualityLevel } from "./unique-quality-levels";
import {
  SOE_13_ASCENDANCY_ITEMS,
  SOE_13_ESSENCE_ITEMS,
  SOE_13_FATE_CARD_ITEMS,
  SOE_13_HATRED_ORB_ITEMS,
  SOE_13_UNIQUE_ITEMS,
  fateCardInfo,
  soe13ItemAliases,
  soe13ItemNameFromCode,
  soe13ListContains,
} from "./soe-13-items";


// Kept for gsf-item-catalog.ts compatibility (GSF tracker removed but import remains)
export const KNOWN_UNIQUE_JEWELS: readonly string[] = [];

export const HOLY_GRAIL_CATEGORIES = [
  { key: "su", label: "Unique" },
  { key: "ssu", label: "Hellforged" },
  { key: "sets", label: "Set Items" },
  { key: "runes", label: "Runes" },
  { key: "runewords", label: "Runewords" },
  { key: "fateCards", label: "Fate Cards" },
  { key: "hatredOrbs", label: "Hatred Orbs" },
  { key: "essences", label: "Essences" },
  { key: "ascendancy", label: "Ascendancy" },
] as const;

export type HolyGrailCategoryKey =
  (typeof HOLY_GRAIL_CATEGORIES)[number]["key"];
export type HolyGrailCategorySettings = Record<HolyGrailCategoryKey, boolean>;

export interface HolyGrailItem {
  key: string;
  name: string;
  category: HolyGrailCategoryKey;
  runes?: RuneName[];
  bases?: string[];
  requiredLevel?: number | null;
  qualityLevel?: number | null;
  fateCardAmountRequired?: number | null;
  fateCardReward?: string | null;
  fateCardDropLocation?: string | null;
}

export interface HolyGrailFoundEntry {
  key: string;
  name: string;
  category: HolyGrailCategoryKey;
  firstFoundAt: string;
}

export interface HolyGrailItemLike {
  name?: string | null;
  base_name?: string | null;
  baseName?: string | null;
  item_code?: string | null;
  itemCode?: string | null;
  canonical_name?: string | null;
  canonicalName?: string | null;
  quality?: string | null;
  is_hellforged?: boolean | null;
  isHellforged?: boolean | null;
  is_runeword?: boolean | null;
  isRuneword?: boolean | null;
}

interface WikiUniqueItem {
  displayName?: string | null;
  index?: string | null;
  code?: string | null;
  hellforged?: boolean | null;
}

export function holyGrailCategoryLabel(key: HolyGrailCategoryKey): string {
  return (
    HOLY_GRAIL_CATEGORIES.find((category) => category.key === key)?.label ?? key
  );
}

export function defaultHolyGrailOverlayCategories(): HolyGrailCategorySettings {
  return Object.fromEntries(
    HOLY_GRAIL_CATEGORIES.map((category) => [category.key, false]),
  ) as HolyGrailCategorySettings;
}

export function normalizeHolyGrailOverlayCategories(
  value: Partial<Record<string, boolean>> | undefined | null,
): HolyGrailCategorySettings {
  const defaults = defaultHolyGrailOverlayCategories();
  return Object.fromEntries(
    HOLY_GRAIL_CATEGORIES.map((category) => [
      category.key,
      value?.[category.key] ?? defaults[category.key],
    ]),
  ) as HolyGrailCategorySettings;
}

export function normalizeHolyGrailKey(value: string): string {
  return value
    .trim()
    .toLowerCase()
    .replace(/[\u2018\u2019]/g, "'")
    .replace(/[^a-z0-9]+/g, " ")
    .trim()
    .replace(/\s+/g, "-");
}

function itemKey(category: HolyGrailCategoryKey, name: string): string {
  return `${category}:${normalizeHolyGrailKey(name)}`;
}

export function cleanTrackedItemName(value: string | undefined | null): string {
  let name = String(value ?? "").trim();
  // Native PD2/SoE filters often append markers such as "[C]", "(3os)",
  // "*", etc. Keep the actual item name stable for grail matching.
  let changed = true;
  while (changed) {
    const before = name;
    name = name
      .replace(/\s+\[[^\]]{1,24}\]\s*$/g, "")
      .replace(/\s+\([^)]{1,24}\)\s*$/g, "")
      .replace(/\s+\{[^}]{1,24}\}\s*$/g, "")
      .replace(/\s+[|/\\\-:]+\s*$/g, "")
      .replace(/\s+[*+#]+\s*$/g, "")
      .trim();
    changed = name !== before;
  }
  return name;
}

function stripSavedTierSuffix(value: string): string {
  return cleanTrackedItemName(value);
}

function isPlaceholderItemName(value: string | undefined | null): boolean {
  return /^Item #\d+$/i.test((value ?? "").trim());
}

function uniqueNames(values: readonly string[] | string[] | undefined): string[] {
  const seen = new Set<string>();
  const out: string[] = [];
  for (const raw of values ?? []) {
    const name = raw?.trim();
    if (!name) continue;
    const key = normalizeHolyGrailKey(name);
    if (!key || seen.has(key)) continue;
    seen.add(key);
    out.push(name);
  }
  return out.sort((a, b) => a.localeCompare(b));
}

export function buildHolyGrailItems(
  _dict?: ItemsDictionary | null,
): HolyGrailItem[] {
  const out: HolyGrailItem[] = [];
  const addMany = (
    category: HolyGrailCategoryKey,
    names: readonly string[] | string[] | undefined,
  ) => {
    for (const name of uniqueNames(names)) {
      const fateCard = category === "fateCards" ? fateCardInfo(name) : null;
      out.push({
        key: itemKey(category, name),
        name,
        category,
        qualityLevel: fateCard?.tier ?? (category === "su" || category === "ssu" ? uniqueQualityLevel(name) : null),
        fateCardAmountRequired: fateCard?.amountRequired ?? null,
        fateCardReward: fateCard?.reward ?? null,
        fateCardDropLocation: fateCard?.dropLocation ?? null,
      });
    }
  };

  addMany("su", [...STATIC_UNIQUE_ITEMS, ...SOE_13_UNIQUE_ITEMS]);
  addMany("ssu", STATIC_HELLFORGED_UNIQUE_ITEMS);
  addMany("sets", STATIC_SET_ITEMS);
  addMany("runes", RUNE_NAMES);
  addMany("fateCards", SOE_13_FATE_CARD_ITEMS);
  addMany("hatredOrbs", SOE_13_HATRED_ORB_ITEMS);
  addMany("essences", SOE_13_ESSENCE_ITEMS);
  addMany("ascendancy", SOE_13_ASCENDANCY_ITEMS);
  for (const runeword of SOE_RUNEWORDS) {
    out.push({
      key: runeword.key,
      name: runeword.name,
      category: "runewords",
      runes: [...runeword.runes],
      bases: [...runeword.bases],
      requiredLevel: runeword.requiredLevel,
    });
  }

  return out;
}

let canonicalItemsByCategoryAndName: Map<string, HolyGrailItem> | null = null;
let canonicalItemsByCategory: Map<HolyGrailCategoryKey, HolyGrailItem[]> | null = null;

const HOLY_GRAIL_ITEM_ALIASES: Record<string, readonly string[]> = {
  "Aldur's Gauntlet": ["Aldur's Rhythm"],
  "Cow King's Hoofs": ["Cow King's Hooves"],
  "Griswolds's Redemption": ["Griswold's Redemption"],
  "Haemosu's Adament": ["Haemosu's Adamant"],
  "Horadrim Almanac": ["Horadric Almanac"],
  "Horadrim Navigator": ["Horadric Navigator"],
  "Heaven's Taebaek": ["Taebaek's Glory"],
  "Hwanin's Seal": ["Hwanin's Blessing"],
  "Infernal Spire": ["Infernal Spike"],
  "Sander's Paragon": ["McAuley's Paragon"],
  "Sander's Riprap": ["McAuley's Riprap"],
  "Sander's Taboo": ["McAuley's Taboo"],
  "Sander's Superstition": ["McAuley's Superstition"],
  "Spiritual Custodian": ["Dark Adherent"],
  "Tal Rasha's Fire-Spun Cloth": ["Tal Rasha's Fine-Spun Cloth", "Tal Rasha's Belt"],
  "Tal Rasha's Howling Wind": ["Tal Rasha's Guardianship", "Tal Rasha's Armor"],
  "Sage's Defiance": ["Sages Defiance"],
  "Wihtstan's Guard": ["Whitstan's Guard"],
  "Worldstone Shard": ["World Stone Shard", "World Stone Shards"],
};

const CODE_ONLY_GRAIL_UNIQUES: Record<string, string> = {
  rtp: "Horadrim Navigator",
  rid: "Horadrim Almanac",
  rkey: "Skeleton Key",
  uhl: "Sage's Defiance",
};

let unambiguousUniqueGrailItemsByCategoryAndCode: Map<string, HolyGrailItem | null> | null = null;

function normalizedItemCode(item: HolyGrailItemLike): string {
  return String(item.item_code || item.itemCode || "").trim().toLowerCase();
}

function itemLooksUnique(item: HolyGrailItemLike): boolean {
  return String(item.quality ?? "").trim().toLowerCase() === "unique";
}

function itemLooksHellforged(item: HolyGrailItemLike): boolean {
  return item.is_hellforged === true ||
    item.isHellforged === true ||
    /\bhellforged\b/i.test(String(item.name ?? "")) ||
    /\bhellforged\b/i.test(String(item.canonical_name ?? item.canonicalName ?? ""));
}

function itemLooksRuneword(item: HolyGrailItemLike): boolean {
  return item.is_runeword === true ||
    item.isRuneword === true ||
    String(item.quality ?? "").trim().toLowerCase() === "runeword";
}

function uniqueGrailCodeLookup(): Map<string, HolyGrailItem | null> {
  if (unambiguousUniqueGrailItemsByCategoryAndCode) {
    return unambiguousUniqueGrailItemsByCategoryAndCode;
  }

  const namesByCategoryAndCode = new Map<string, Set<string>>();
  for (const unique of uniquesJson as WikiUniqueItem[]) {
    const code = String(unique.code ?? "").trim().toLowerCase();
    const displayName = String(unique.displayName || unique.index || "").trim();
    if (!code || !displayName) continue;

    const category: HolyGrailCategoryKey = unique.hellforged ? "ssu" : "su";
    const item = canonicalHolyGrailItem(category, displayName);
    if (!item) continue;

    const key = `${category}:${code}`;
    const set = namesByCategoryAndCode.get(key) ?? new Set<string>();
    set.add(item.name);
    namesByCategoryAndCode.set(key, set);
  }

  unambiguousUniqueGrailItemsByCategoryAndCode = new Map();
  for (const [key, names] of namesByCategoryAndCode) {
    if (names.size !== 1) {
      unambiguousUniqueGrailItemsByCategoryAndCode.set(key, null);
      continue;
    }

    const [category] = key.split(":") as [HolyGrailCategoryKey, string];
    const [name] = [...names];
    unambiguousUniqueGrailItemsByCategoryAndCode.set(
      key,
      canonicalHolyGrailItem(category, name),
    );
  }

  return unambiguousUniqueGrailItemsByCategoryAndCode;
}

function codeOnlyGrailUnique(item: HolyGrailItemLike): HolyGrailItem | null {
  const code = normalizedItemCode(item);
  const manualName = CODE_ONLY_GRAIL_UNIQUES[code];
  if (manualName) return canonicalHolyGrailItem("su", manualName);

  if (!code || !itemLooksUnique(item)) return null;

  const category: HolyGrailCategoryKey = itemLooksHellforged(item) ? "ssu" : "su";
  return uniqueGrailCodeLookup().get(`${category}:${code}`) ?? null;
}

function codeOnlyRune(item: HolyGrailItemLike): HolyGrailItem | null {
  const name = runeNameFromCode(normalizedItemCode(item));
  return name ? canonicalHolyGrailItem("runes", name) : null;
}

function codeOnlyFateCard(item: HolyGrailItemLike): HolyGrailItem | null {
  const match = /^fa(\d{1,2})$/i.exec(normalizedItemCode(item));
  if (!match) return null;
  const index = Number.parseInt(match[1], 10) - 1;
  const name = SOE_13_FATE_CARD_ITEMS[index];
  return name ? canonicalHolyGrailItem("fateCards", name) : null;
}

function canonicalLookup(): Map<string, HolyGrailItem> {
  if (!canonicalItemsByCategoryAndName) {
    canonicalItemsByCategoryAndName = new Map(
      buildHolyGrailItems().flatMap((item) => {
        const normalizedName = normalizeHolyGrailKey(item.name);
        const entries: Array<readonly [string, HolyGrailItem]> = [
          [`${item.category}:${normalizedName}`, item],
          [`${item.category}:${normalizeHolyGrailKey(stripSavedTierSuffix(item.name))}`, item],
        ];
        for (const alias of HOLY_GRAIL_ITEM_ALIASES[item.name] ?? []) {
          entries.push([`${item.category}:${normalizeHolyGrailKey(alias)}`, item]);
        }
        for (const alias of soe13ItemAliases(item.name)) {
          entries.push([`${item.category}:${normalizeHolyGrailKey(alias)}`, item]);
        }
        if (item.category === "ssu") {
          const withoutHellforged = item.name.replace(/^Hellforged\s+/i, "").trim();
          if (withoutHellforged) {
            entries.push([`ssu:${normalizeHolyGrailKey(withoutHellforged)}`, item]);
          }
        }
        return entries;
      }),
    );
  }
  return canonicalItemsByCategoryAndName;
}

function canonicalCategoryLookup(): Map<HolyGrailCategoryKey, HolyGrailItem[]> {
  if (!canonicalItemsByCategory) {
    canonicalItemsByCategory = new Map();
    for (const category of HOLY_GRAIL_CATEGORIES) {
      canonicalItemsByCategory.set(category.key, []);
    }
    for (const item of buildHolyGrailItems()) {
      canonicalItemsByCategory.get(item.category)?.push(item);
    }
    for (const items of canonicalItemsByCategory.values()) {
      items.sort((a, b) => normalizeHolyGrailKey(b.name).length - normalizeHolyGrailKey(a.name).length);
    }
  }
  return canonicalItemsByCategory;
}

function normalizedWordText(value: string | undefined | null): string {
  return normalizeHolyGrailKey(value ?? "").replace(/-/g, " ");
}

function canonicalItemInsideDecoratedName(
  category: HolyGrailCategoryKey,
  name: string,
): HolyGrailItem | null {
  if (
    category !== "su" &&
    category !== "ssu" &&
    category !== "sets" &&
    category !== "fateCards" &&
    category !== "hatredOrbs" &&
    category !== "essences" &&
    category !== "ascendancy"
  ) return null;

  const haystack = ` ${normalizedWordText(name)} `;
  if (!haystack.trim()) return null;

  for (const item of canonicalCategoryLookup().get(category) ?? []) {
    const needle = normalizedWordText(item.name);
    if (!needle) continue;
    if (haystack.includes(` ${needle} `)) return item;
  }

  return null;
}

export function canonicalHolyGrailItem(
  category: string | undefined | null,
  name: string | undefined | null,
): HolyGrailItem | null {
  const categoryKey = category as HolyGrailCategoryKey;
  if (!HOLY_GRAIL_CATEGORIES.some((item) => item.key === categoryKey)) return null;
  const rawName = (name ?? "").trim();
  const cleanName = cleanTrackedItemName(rawName);
  if (!cleanName || isPlaceholderItemName(cleanName)) return null;

  const lookup = canonicalLookup();
  const normalizedName = normalizeHolyGrailKey(cleanName);
  if (categoryKey === "runes") {
    const rune = runeNameFromText(cleanName);
    if (rune) return lookup.get(`runes:${normalizeHolyGrailKey(rune)}`) ?? null;
  }
  return (
    lookup.get(`${categoryKey}:${normalizedName}`) ??
    canonicalItemInsideDecoratedName(categoryKey, rawName) ??
    null
  );
}

// Find which category an item belongs to by searching all categories
export function findHolyGrailItem(name: string | undefined | null): HolyGrailItem | null {
  if (!name) return null;
  for (const cat of HOLY_GRAIL_CATEGORIES) {
    const found = canonicalHolyGrailItem(cat.key, name);
    if (found) return found;
  }
  return null;
}

export function canonicalTrackedItemName(
  name: string | undefined | null,
  category?: HolyGrailCategoryKey | null,
): string {
  const cleanName = cleanTrackedItemName(name);
  if (!cleanName) return "";

  if (category) {
    return canonicalHolyGrailItem(category, cleanName)?.name ?? cleanName;
  }

  if (/\bhellforged\b/i.test(cleanName)) {
    return canonicalHolyGrailItem("ssu", cleanName)?.name ?? cleanName;
  }

  return findHolyGrailItem(cleanName)?.name ?? cleanName;
}

export function mergeFoundIntoHolyGrailItems(
  items: HolyGrailItem[],
  found: Record<string, HolyGrailFoundEntry>,
): HolyGrailItem[] {
  const byKey = new Map(items.map((item) => [item.key, item]));
  for (const entry of Object.values(found)) {
    if (!entry?.key || !entry?.name || !entry?.category) continue;
    if (!byKey.has(entry.key)) {
      byKey.set(entry.key, {
        key: entry.key,
        name: entry.name,
        category: entry.category,
      });
    }
  }
  return Array.from(byKey.values()).sort((a, b) => {
    const categoryOrder =
      HOLY_GRAIL_CATEGORIES.findIndex((category) => category.key === a.category) -
      HOLY_GRAIL_CATEGORIES.findIndex((category) => category.key === b.category);
    return categoryOrder || a.name.localeCompare(b.name);
  });
}

export function inferHolyGrailCategory(
  item: HolyGrailItemLike,
): HolyGrailCategoryKey | null {
  // Runewords are intentionally manual-only. The hook cannot reliably resolve
  // completed runeword names without false positives from loaded/generated items.
  if (itemLooksRuneword(item)) return null;
  if (codeOnlyGrailUnique(item)) return "su";
  if (codeOnlyRune(item)) return "runes";
  if (codeOnlyFateCard(item)) return "fateCards";
  const quality = (item.quality ?? "").toLowerCase();
  if (quality === "set") return "sets";

  const name = cleanTrackedItemName(
    item.canonical_name ||
    item.canonicalName ||
    item.name ||
    item.base_name ||
    item.baseName ||
    "",
  );
  if (
    RUNE_NAMES.some((rune) => rune.toLowerCase() === name.toLowerCase()) ||
    RUNE_NAMES.some((rune) => new RegExp(`\\b${rune}\\s+rune\\b`, "i").test(name))
  ) return "runes";
  if (soe13ListContains(SOE_13_FATE_CARD_ITEMS, name)) return "fateCards";
  if (soe13ListContains(SOE_13_HATRED_ORB_ITEMS, name)) return "hatredOrbs";
  if (soe13ListContains(SOE_13_ESSENCE_ITEMS, name)) return "essences";
  if (soe13ListContains(SOE_13_ASCENDANCY_ITEMS, name)) return "ascendancy";
  if (item.is_hellforged === true || item.isHellforged === true || /\bhellforged\b/i.test(name) || canonicalHolyGrailItem("ssu", name)) return "ssu";

  if (quality === "unique") return "su";
  return null;
}

export function holyGrailItemFromDrop(
  item: HolyGrailItemLike,
): HolyGrailItem | null {
  if (itemLooksRuneword(item)) return null;
  const byCode = codeOnlyGrailUnique(item);
  if (byCode) return byCode;
  const runeByCode = codeOnlyRune(item);
  if (runeByCode) return runeByCode;
  const fateCardByCode = codeOnlyFateCard(item);
  if (fateCardByCode) return fateCardByCode;
  const soe13NameByCode = soe13ItemNameFromCode(normalizedItemCode(item));
  const soe13GrailByCode = soe13NameByCode ? findHolyGrailItem(soe13NameByCode) : null;
  if (soe13GrailByCode) return soe13GrailByCode;

  const category = inferHolyGrailCategory(item);
  const candidates = [
    item.canonical_name,
    item.canonicalName,
    item.name,
    item.base_name,
    item.baseName,
  ];

  for (const candidate of candidates) {
    const match = canonicalHolyGrailItem(category, cleanTrackedItemName(candidate));
    if (match) return match;
  }
  return null;
}

export function holyGrailProgress(
  items: HolyGrailItem[],
  found: Record<string, HolyGrailFoundEntry>,
) {
  const total = items.length;
  const foundCount = items.filter((item) => !!found[item.key]).length;
  return {
    total,
    found: foundCount,
    percent: total > 0 ? (foundCount / total) * 100 : 0,
  };
}

export function holyGrailCategoryProgress(
  items: HolyGrailItem[],
  found: Record<string, HolyGrailFoundEntry>,
  category: HolyGrailCategoryKey,
) {
  const categoryItems = items.filter((item) => item.category === category);
  const total = categoryItems.length;
  const foundCount = categoryItems.filter((item) => !!found[item.key]).length;
  return {
    total,
    found: foundCount,
    percent: total > 0 ? (foundCount / total) * 100 : 0,
  };
}

export function mostRecentHolyGrailFind(
  found: Record<string, HolyGrailFoundEntry>,
  options: { includeFateCards?: boolean } = {},
): HolyGrailFoundEntry | null {
  let latest: { entry: HolyGrailFoundEntry; time: number; index: number } | null = null;
  let index = 0;

  for (const entry of Object.values(found)) {
    if (!options.includeFateCards && entry?.category === "fateCards") {
      index += 1;
      continue;
    }
    const time = Date.parse(entry?.firstFoundAt ?? "");
    if (!Number.isFinite(time)) { index += 1; continue; }
    if (!latest || time > latest.time || (time === latest.time && index > latest.index)) {
      latest = { entry, time, index };
    }
    index += 1;
  }

  return latest?.entry ?? null;
}
