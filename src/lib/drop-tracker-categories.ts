export const RUNE_NAMES = [
  "El",
  "Eld",
  "Tir",
  "Nef",
  "Eth",
  "Ith",
  "Tal",
  "Ral",
  "Ort",
  "Thul",
  "Amn",
  "Sol",
  "Shael",
  "Dol",
  "Hel",
  "Io",
  "Lum",
  "Ko",
  "Fal",
  "Lem",
  "Pul",
  "Um",
  "Mal",
  "Ist",
  "Gul",
  "Vex",
  "Ohm",
  "Lo",
  "Sur",
  "Ber",
  "Jah",
  "Cham",
  "Zod",
] as const;

export type RuneName = (typeof RUNE_NAMES)[number];
export type RuneTrackerCounts = Partial<Record<RuneName, number>>;
export type RuneTrackerVisibility = Record<RuneName, boolean>;

const RUNE_SET = new Set<string>(RUNE_NAMES.map((name) => name.toLowerCase()));
const LOW_RUNE_SET = new Set<string>(
  RUNE_NAMES.slice(0, RUNE_NAMES.indexOf("Fal") + 1).map((name) => name.toLowerCase()),
);
const MID_RUNE_SET = new Set<string>(
  RUNE_NAMES.slice(RUNE_NAMES.indexOf("Lem"), RUNE_NAMES.indexOf("Gul") + 1).map((name) => name.toLowerCase()),
);
const HIGH_RUNE_SET = new Set<string>(
  RUNE_NAMES.slice(RUNE_NAMES.indexOf("Vex")).map((name) => name.toLowerCase()),
);

export const DROP_TRACKER_CATEGORIES = [
  { key: "unique", label: "Unique" },
  { key: "hellforged", label: "Hellforged" },
  { key: "sets", label: "Set Item" },
  { key: "rare", label: "Rare" },
  { key: "magic", label: "Magic" },
  { key: "lowRune", label: "Low Rune" },
  { key: "midRune", label: "Mid Rune" },
  { key: "highRune", label: "High Rune" },
  { key: "charm", label: "Charm" },
  { key: "jewel", label: "Jewel" },
  { key: "perfectGem", label: "Perfect Gem" },
  { key: "token", label: "Token" },
] as const;

export type DropTrackerCategoryKey =
  (typeof DROP_TRACKER_CATEGORIES)[number]["key"];
export type DropTrackerCategorySettings = Record<DropTrackerCategoryKey, boolean>;
export type DropTrackerCounts = Partial<Record<DropTrackerCategoryKey, number>>;

export interface DropTrackerItemLike {
  name?: string | null;
  base_name?: string | null;
  baseName?: string | null;
  canonical_name?: string | null;
  canonicalName?: string | null;
  category?: string | null;
  quality?: string | null;
  is_hellforged?: boolean | null;
  isHellforged?: boolean | null;
  is_relic?: boolean | null;
  isRelic?: boolean | null;
}

function haystackFor(item: DropTrackerItemLike): string {
  return [item.canonical_name, item.canonicalName, item.name, item.base_name, item.baseName, item.category]
    .filter(Boolean)
    .join(" ")
    .toLowerCase();
}

function has(haystack: string, text: string): boolean {
  return haystack.includes(text.toLowerCase());
}

function cleanRuneCandidate(value: unknown): string {
  return String(value ?? "")
    .replace(/\brune\b/gi, "")
    .replace(/[^a-z]/gi, " ")
    .trim()
    .replace(/\s+/g, " ");
}

function cleanDropName(value: unknown): string {
  let name = String(value ?? "").trim();
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

export function runeNameFromDrop(item: DropTrackerItemLike): RuneName | null {
  const candidates = [
    item.canonical_name,
    item.canonicalName,
    item.name,
    item.base_name,
    item.baseName,
    item.category,
  ];

  for (const candidate of candidates) {
    const cleaned = cleanRuneCandidate(candidate);
    if (!cleaned) continue;
    const exact = RUNE_NAMES.find((name) => name.toLowerCase() === cleaned.toLowerCase());
    if (exact) return exact;
  }

  const haystack = haystackFor(item);
  for (const rune of RUNE_NAMES) {
    const pattern = new RegExp(`\\b${rune}\\s+rune\\b`, "i");
    if (pattern.test(haystack)) return rune;
  }

  return null;
}

export function runeCategory(rune: RuneName): Extract<DropTrackerCategoryKey, "lowRune" | "midRune" | "highRune"> {
  const key = rune.toLowerCase();
  if (LOW_RUNE_SET.has(key)) return "lowRune";
  if (MID_RUNE_SET.has(key)) return "midRune";
  if (HIGH_RUNE_SET.has(key)) return "highRune";
  return "lowRune";
}

export function defaultRuneTrackerCounts(): RuneTrackerCounts {
  return {};
}

export function defaultRuneTrackerVisibility(): RuneTrackerVisibility {
  return Object.fromEntries(RUNE_NAMES.map((name) => [name, true])) as RuneTrackerVisibility;
}

export function normalizeRuneTrackerCounts(value: Partial<Record<string, number>> | undefined | null): RuneTrackerCounts {
  const out: RuneTrackerCounts = {};
  for (const rune of RUNE_NAMES) {
    const raw = value?.[rune];
    if (typeof raw === "number" && Number.isFinite(raw) && raw > 0) {
      out[rune] = Math.floor(raw);
    }
  }
  return out;
}

export function normalizeRuneTrackerVisibility(
  value: Partial<Record<string, boolean>> | undefined | null,
): RuneTrackerVisibility {
  const defaults = defaultRuneTrackerVisibility();
  return Object.fromEntries(
    RUNE_NAMES.map((rune) => [rune, value?.[rune] ?? defaults[rune]]),
  ) as RuneTrackerVisibility;
}

export function categoryLabel(key: DropTrackerCategoryKey): string {
  return DROP_TRACKER_CATEGORIES.find((c) => c.key === key)?.label ?? key;
}

export function defaultDropsTrackerCategories(): DropTrackerCategorySettings {
  return Object.fromEntries(
    DROP_TRACKER_CATEGORIES.map((category) => [
      category.key,
      category.key === "unique" ||
        category.key === "hellforged" ||
        category.key === "sets",
    ]),
  ) as DropTrackerCategorySettings;
}

export function defaultTotalDropsTrackerCategories(): DropTrackerCategorySettings {
  return Object.fromEntries(
    DROP_TRACKER_CATEGORIES.map((category) => [category.key, true]),
  ) as DropTrackerCategorySettings;
}

export function normalizeCategorySettings(
  value: Partial<Record<string, boolean>> | undefined | null,
  defaults: DropTrackerCategorySettings,
): DropTrackerCategorySettings {
  return Object.fromEntries(
    DROP_TRACKER_CATEGORIES.map((category) => [
      category.key,
      value?.[category.key] ?? defaults[category.key],
    ]),
  ) as DropTrackerCategorySettings;
}

export function emptyDropTrackerCounts(): DropTrackerCounts {
  return {};
}

export function normalizeCounts(
  value: Partial<Record<string, number>> | undefined | null,
): DropTrackerCounts {
  const out: DropTrackerCounts = {};
  for (const category of DROP_TRACKER_CATEGORIES) {
    const raw = value?.[category.key];
    if (typeof raw === "number" && Number.isFinite(raw) && raw > 0) {
      out[category.key] = Math.floor(raw);
    }
  }
  return out;
}

export function countTotal(
  counts: DropTrackerCounts,
  enabled: DropTrackerCategorySettings,
): number {
  return DROP_TRACKER_CATEGORIES.reduce((sum, category) => {
    if (!enabled[category.key]) return sum;
    return sum + (counts[category.key] ?? 0);
  }, 0);
}

export function categorizeDrop(
  item: DropTrackerItemLike,
): DropTrackerCategoryKey[] {
  const categories: DropTrackerCategoryKey[] = [];
  const haystack = haystackFor(item);
  const quality = (item.quality ?? "").toLowerCase();

  const name = cleanDropName(item.name ?? "");
  if (item.is_hellforged === true || item.isHellforged === true || /\bhellforged\b/i.test(name)) {
    categories.push("hellforged");
  } else if (quality === "unique") {
    categories.push("unique");
  }

  if (quality === "set") categories.push("sets");
  if (quality === "rare") categories.push("rare");
  if (quality === "magic") categories.push("magic");

  const rune = runeNameFromDrop(item);
  if (rune) categories.push(runeCategory(rune));

  if (has(haystack, "charm") || has(haystack, "annihilus") || has(haystack, "hellfire torch")) categories.push("charm");
  if (has(haystack, "jewel")) categories.push("jewel");
  if (has(haystack, "perfect") && has(haystack, "gem")) categories.push("perfectGem");
  if (has(haystack, "token")) categories.push("token");

  return Array.from(new Set(categories));
}

export function incrementCounts(
  counts: DropTrackerCounts,
  categories: DropTrackerCategoryKey[],
  enabled: DropTrackerCategorySettings,
): DropTrackerCounts {
  const next: DropTrackerCounts = { ...counts };
  for (const category of categories) {
    if (!enabled[category]) continue;
    next[category] = (next[category] ?? 0) + 1;
  }
  return next;
}

export function decrementCounts(
  counts: DropTrackerCounts,
  categories: DropTrackerCategoryKey[],
): DropTrackerCounts {
  const next: DropTrackerCounts = { ...counts };
  for (const category of categories) {
    const current = next[category] ?? 0;
    if (current <= 1) {
      delete next[category];
    } else {
      next[category] = current - 1;
    }
  }
  return normalizeCounts(next);
}
