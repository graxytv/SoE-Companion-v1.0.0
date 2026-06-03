import type { ItemsDictionary } from "../stores/items-dictionary.svelte";
import {
  KNOWN_UNIQUE_JEWELS,
} from "./holy-grail";
import {
  STATIC_SET_ITEMS,
  STATIC_HELLFORGED_UNIQUE_ITEMS,
  STATIC_UNIQUE_ITEMS,
} from "./holy-grail-static";
import { STATIC_TU_ITEMS } from "./gsf-tu-static";

export const GSF_CATEGORIES = [
  { key: "tu", label: "TU" },
  { key: "su", label: "SU" },
  { key: "ssu", label: "SSU" },
  { key: "sssu", label: "SSSU" },
  { key: "set", label: "Set" },
  { key: "uniqueJewel", label: "Unique Jewel" },
] as const;

export type GsfItemCategory = (typeof GSF_CATEGORIES)[number]["key"];

export const GSF_SLOTS = [
  { key: "any", label: "Any" },
  { key: "chest", label: "Chest" },
  { key: "helm", label: "Helm" },
  { key: "weapon", label: "Weapon" },
  { key: "offhand", label: "Off-hand" },
  { key: "shield", label: "Shield" },
  { key: "gloves", label: "Gloves" },
  { key: "boots", label: "Boots" },
  { key: "belt", label: "Belt" },
  { key: "ring", label: "Ring" },
  { key: "amulet", label: "Amulet" },
  { key: "jewel", label: "Jewel" },
  { key: "other", label: "Other" },
] as const;

export type GsfItemSlot = (typeof GSF_SLOTS)[number]["key"];

export interface GsfCatalogOption {
  name: string;
  category: GsfItemCategory;
  source: "live" | "static";
}

export type GsfCatalog = Record<GsfItemCategory, GsfCatalogOption[]>;

export function normalizeGsfItemName(value: unknown): string {
  return String(value ?? "")
    .trim()
    .toLowerCase()
    .replace(/\s+/g, " ");
}

function addOptions(
  map: Map<string, GsfCatalogOption>,
  names: readonly string[] | string[] | undefined,
  category: GsfItemCategory,
  source: GsfCatalogOption["source"],
): void {
  for (const rawName of names ?? []) {
    const name = String(rawName ?? "").trim();
    if (!name) continue;
    const key = normalizeGsfItemName(name);
    if (!map.has(key) || source === "live") {
      map.set(key, { name, category, source });
    }
  }
}

function sortedOptions(map: Map<string, GsfCatalogOption>): GsfCatalogOption[] {
  return Array.from(map.values()).sort((a, b) => a.name.localeCompare(b.name));
}

export function buildGsfCatalog(dict?: ItemsDictionary | null): GsfCatalog {
  const tu = new Map<string, GsfCatalogOption>();
  const su = new Map<string, GsfCatalogOption>();
  const ssu = new Map<string, GsfCatalogOption>();
  const sssu = new Map<string, GsfCatalogOption>();
  const set = new Map<string, GsfCatalogOption>();
  const uniqueJewel = new Map<string, GsfCatalogOption>();

  addOptions(tu, STATIC_TU_ITEMS, "tu", "static");
  addOptions(su, STATIC_UNIQUE_ITEMS, "su", "static");
  addOptions(ssu, STATIC_HELLFORGED_UNIQUE_ITEMS, "ssu", "static");
  addOptions(set, STATIC_SET_ITEMS, "set", "static");
  addOptions(uniqueJewel, KNOWN_UNIQUE_JEWELS, "uniqueJewel", "static");

  addOptions(tu, dict?.uniques_tu, "tu", "live");
  addOptions(su, dict?.uniques_su, "su", "live");
  addOptions(ssu, dict?.uniques_ssu, "ssu", "live");
  addOptions(sssu, dict?.uniques_sssu, "sssu", "live");
  addOptions(set, dict?.set_items, "set", "live");

  return {
    tu: sortedOptions(tu),
    su: sortedOptions(su),
    ssu: sortedOptions(ssu),
    sssu: sortedOptions(sssu),
    set: sortedOptions(set),
    uniqueJewel: sortedOptions(uniqueJewel),
  };
}

export function gsfAutocompleteOptions(
  dict: ItemsDictionary | null | undefined,
  category: GsfItemCategory,
  query: string,
  limit = 50,
): GsfCatalogOption[] {
  const catalog = buildGsfCatalog(dict);
  const needle = normalizeGsfItemName(query);
  const options = catalog[category] ?? [];
  if (!needle) return options.slice(0, limit);
  return options
    .filter((option) => normalizeGsfItemName(option.name).includes(needle))
    .slice(0, limit);
}

export function resolveGsfItemImage(
  category: GsfItemCategory,
  itemName: string,
): string | null {
  const name = itemName.trim();
  if (!name) return null;
  const encoded = encodeURIComponent(name).replace(/%2F/gi, "/");
  if (category === "tu") {
    return `/gsf-item-images/tiered_uniques/${encoded}.png`;
  }
  if (category === "su" || category === "ssu" || category === "sssu") {
    return `/gsf-item-images/sacred_uniques/${encoded}.png`;
  }
  if (category === "set") {
    return `/gsf-item-images/sets/${encoded}.png`;
  }
  return null;
}

export function gsfCategoryLabel(category: GsfItemCategory): string {
  return GSF_CATEGORIES.find((item) => item.key === category)?.label ?? category;
}

export function gsfSlotLabel(slot: GsfItemSlot): string {
  return GSF_SLOTS.find((item) => item.key === slot)?.label ?? slot;
}
