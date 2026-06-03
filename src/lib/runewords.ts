import runewordsJson from "./data/soe-runewords.json";

export const RUNE_NAMES = [
  "El", "Eld", "Tir", "Nef", "Eth", "Ith", "Tal", "Ral", "Ort", "Thul",
  "Amn", "Sol", "Shael", "Dol", "Hel", "Io", "Lum", "Ko", "Fal", "Lem",
  "Pul", "Um", "Mal", "Ist", "Gul", "Vex", "Ohm", "Lo", "Sur", "Ber",
  "Jah", "Cham", "Zod",
] as const;

export type RuneName = (typeof RUNE_NAMES)[number];
export type RuneInventory = Record<RuneName, number>;

export interface SoeRuneword {
  key: string;
  name: string;
  runes: RuneName[];
  bases: string[];
  requiredLevel: number | null;
  properties: string[];
}

interface RawRuneword {
  name?: string | null;
  displayName?: string | null;
  runewordName?: string | null;
  firstRuneDisplayName?: string | null;
  secondRuneDisplayName?: string | null;
  thirdRuneDisplayName?: string | null;
  fourthRuneDisplayName?: string | null;
  fifthRuneDisplayName?: string | null;
  sixthRuneDisplayName?: string | null;
  displayItemTypes?: string[] | null;
  displayProperties?: string[] | null;
  requiredlevel?: string | number | null;
}

function runeName(value: unknown): RuneName | null {
  const normalized = String(value ?? "").trim().toLowerCase();
  return RUNE_NAMES.find((rune) => rune.toLowerCase() === normalized) ?? null;
}

function requiredLevel(value: unknown): number | null {
  const numeric = Number(value);
  return Number.isFinite(numeric) && numeric > 0 ? numeric : null;
}

function normalizeKey(value: string): string {
  return value
    .trim()
    .toLowerCase()
    .replace(/[']/g, "'")
    .replace(/[^a-z0-9]+/g, " ")
    .trim()
    .replace(/\s+/g, "-");
}

export const SOE_RUNEWORDS: readonly SoeRuneword[] = (runewordsJson as RawRuneword[])
  .map((item) => {
    const name = String(item.displayName || item.runewordName || "").trim();
    const keyName = String(item.name || item.runewordName || item.displayName || "").trim();
    const runes = [
      item.firstRuneDisplayName,
      item.secondRuneDisplayName,
      item.thirdRuneDisplayName,
      item.fourthRuneDisplayName,
      item.fifthRuneDisplayName,
      item.sixthRuneDisplayName,
    ].map(runeName).filter((rune): rune is RuneName => !!rune);
    const bases = Array.from(new Set((item.displayItemTypes ?? []).map((base) => String(base).trim()).filter(Boolean)));
    const properties = (item.displayProperties ?? []).map((property) => String(property).trim()).filter(Boolean);
    const keyParts = [keyName || name, runes.join("-"), bases.join("-")].filter(Boolean).join("-");
    return {
      key: `runewords:${normalizeKey(keyParts)}`,
      name,
      runes,
      bases,
      requiredLevel: requiredLevel(item.requiredlevel),
      properties,
    };
  })
  .filter((item) => item.name && item.runes.length > 0)
  .sort((a, b) => a.name.localeCompare(b.name));

export function emptyRuneInventory(): RuneInventory {
  return Object.fromEntries(RUNE_NAMES.map((rune) => [rune, 0])) as RuneInventory;
}

export function canMakeRuneword(runeword: SoeRuneword, inventory: RuneInventory): boolean {
  const needed = emptyRuneInventory();
  for (const rune of runeword.runes) needed[rune] += 1;
  return RUNE_NAMES.every((rune) => inventory[rune] >= needed[rune]);
}

export function missingRunesForRuneword(runeword: SoeRuneword, inventory: RuneInventory): string[] {
  const needed = emptyRuneInventory();
  for (const rune of runeword.runes) needed[rune] += 1;
  return RUNE_NAMES
    .filter((rune) => needed[rune] > inventory[rune])
    .map((rune) => needed[rune] - inventory[rune] > 1 ? `${rune} x${needed[rune] - inventory[rune]}` : rune);
}

export function normalizeRuneInventory(value: Partial<Record<string, number>> | null | undefined): RuneInventory {
  const inventory = emptyRuneInventory();
  for (const rune of RUNE_NAMES) {
    const count = Number(value?.[rune] ?? 0);
    inventory[rune] = Number.isFinite(count) && count > 0 ? Math.floor(count) : 0;
  }
  return inventory;
}
