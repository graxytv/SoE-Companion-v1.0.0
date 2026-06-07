import { RUNE_NAMES, runeNameFromDrop, type DropTrackerItemLike } from './drop-tracker-categories';
import {
  buildHolyGrailItems,
  canonicalHolyGrailItem,
  cleanTrackedItemName,
  holyGrailItemFromDrop,
  normalizeHolyGrailKey,
  type HolyGrailCategoryKey,
  type HolyGrailItem,
} from './holy-grail';
import { uniqueQualityLabel } from './unique-quality-levels';
import {
  SOE_13_ESSENCE_ITEMS,
  SOE_13_FATE_CARD_ITEMS,
  SOE_13_ITEM_ALIASES,
  SOE_13_MATERIAL_ITEMS,
} from './soe-13-items';

export type ItemSoundCategory = 'unique' | 'hellforged' | 'set' | 'runeword' | 'rune' | 'fateCard' | 'essence' | 'material' | 'custom';

export interface ItemSoundRule {
  itemName: string;
  category: ItemSoundCategory;
  soundSlot: number | null;
  volume: number;
}

export type ItemSoundRules = Record<string, ItemSoundRule>;

export interface ItemSoundCatalogItem {
  key: string;
  itemName: string;
  category: ItemSoundCategory;
  label: string;
  searchText: string;
}

interface ItemSoundDropLike extends DropTrackerItemLike {
  is_runeword?: boolean | null;
  isRuneword?: boolean | null;
}

export const MATERIAL_NAMES = [
  'Mythic Orb',
  'Divine Orb',
  'Sacred Orb',
  'Eternal Orb',
  'Exalted Orb',
  'Orb of Extraction',
  'Desecration Orb',
  'Infused Horadrim Orb',
  'Horadrim Scarab',
  "Jeweller's Prism",
  'Larzuk Puzzlebox',
  "Lilith's Mirror",
  'Demonic Cube',
  'Eternal Coin',
  'Jewel Fragment',
  'Worldstone Shard',
  'Tainted Worldstone Shard',
  'Crystallised Cindersoul',
  'Black Soulstone',
  'Pure Demonic Essence',
  'Prime Evil Soul',
  'Demonic Insignia',
  'Talisman of Transgression',
  'Flesh of Malic',
  'Key of Terror',
  'Key of Hate',
  'Key of Destruction',
  'Trang-Oul Jawbone',
  'Splinter of the Void',
  'Hellfire Ashes',
  'Sigil of Madawc',
  'Sigil of Talic',
  'Sigil of Korlic',
  'Sigil Fragment of Apostasy',
  'Sigil Fragment of Betrayal',
  'Sigil Fragment of Damnation',
  'Sigil Fragment of Resurrection',
  'Blood Crafting Infusion',
  'Caster Crafting Infusion',
  'Safety Crafting Infusion',
  'Hitpower Crafting Infusion',
  'Vampiric Crafting Infusion',
  'Bountiful Crafting Infusion',
  'Brilliant Crafting Infusion',
  'Perfect Ruby',
  'Perfect Amethyst',
  'Perfect Emerald',
  'Perfect Sapphire',
  'Perfect Topaz',
  'Perfect Diamond',
  'Perfect Skull',
  'Token of Absolution',
  "Cartographer's Chisel",
  "Cartographer's Chisel of Avarice",
  "Cartographer's Chisel of Procurement",
  'Glyph of Nemeses',
  'Glyph of Corruption',
  'Glyph of Adversaries',
  'Glyph of Moo',
  'Glyph of Lineage',
  'Terror of Resplendence',
  'Terror of Opulence',
  'Terror of Ethereal',
  'Terror of Absolute',
  'Twisted Essence of Suffering',
  'Charged Essence of Hatred',
  'Burning Essence of Terror',
  'Festering Essence of Destruction',
  ...SOE_13_MATERIAL_ITEMS,
] as const;

const BOSS_ESSENCE_NAMES = [
  'Twisted Essence of Suffering',
  'Charged Essence of Hatred',
  'Burning Essence of Terror',
  'Festering Essence of Destruction',
] as const;

const ITEM_SOUND_MATERIAL_EXCLUDED_KEYS = new Set(
  [...SOE_13_FATE_CARD_ITEMS, ...SOE_13_ESSENCE_ITEMS, ...BOSS_ESSENCE_NAMES].map(normalizeHolyGrailKey),
);

export const MATERIAL_ALIASES: Record<string, readonly string[]> = {
  ...SOE_13_ITEM_ALIASES,
  [normalizeHolyGrailKey("Cartographer's Chisel of Avarice")]: [
    'Chisel Avarice',
    'Chisel-Avarice',
    'Chisel Averice',
    'Chisel-Averice',
  ],
  [normalizeHolyGrailKey("Cartographer's Chisel of Procurement")]: [
    'Chisel Procurement',
    'Chisel-Procurement',
  ],
  [normalizeHolyGrailKey('Glyph of Nemeses')]: ['Glyph of Nemesis'],
  [normalizeHolyGrailKey('Glyph of Adversaries')]: ['Glyph of Adverseries'],
  [normalizeHolyGrailKey('Larzuk Puzzlebox')]: ["Larzuk's Puzzlebox"],
  [normalizeHolyGrailKey('Worldstone Shard')]: ['Worldstone Shards', 'WSS'],
  [normalizeHolyGrailKey('Tainted Worldstone Shard')]: [
    'Tainted World Stone Shard',
    'Tainted Worldstone Shards',
    'Tainted WSS',
    'Corrupted Worldstone Shard',
    'cwss',
  ],
  [normalizeHolyGrailKey('Key of Terror')]: ['Pandemonium Key 1'],
  [normalizeHolyGrailKey('Key of Hate')]: ['Pandemonium Key 2'],
  [normalizeHolyGrailKey('Key of Destruction')]: ['Pandemonium Key 3'],
};

export const ITEM_SOUND_CATEGORY_LABELS: Record<ItemSoundCategory, string> = {
  unique: 'Uniques',
  hellforged: 'Hellforged',
  set: 'Sets',
  runeword: 'Runewords',
  rune: 'Runes',
  fateCard: 'Fate Cards',
  essence: 'Essences',
  material: 'Materials',
  custom: 'Custom',
};

function categoryForGrailItem(item: HolyGrailItem): ItemSoundCategory {
  if (item.category === 'ssu') return 'hellforged';
  if (item.category === 'sets') return 'set';
  if (item.category === 'runes') return 'rune';
  if (item.category === 'runewords') return 'runeword';
  if (item.category === 'fateCards') return 'fateCard';
  if (item.category === 'essences') return 'essence';
  if (item.category === 'hatredOrbs' || item.category === 'ascendancy') return 'material';
  return 'unique';
}

export function itemSoundKey(category: ItemSoundCategory, itemName: string): string {
  return `${category}:${normalizeHolyGrailKey(itemName)}`;
}

export function normalizeItemSoundRule(value: unknown): ItemSoundRule | null {
  if (!value || typeof value !== 'object') return null;
  const raw = value as Partial<ItemSoundRule>;
  const itemName = typeof raw.itemName === 'string' ? cleanTrackedItemName(raw.itemName) : '';
  if (!itemName) return null;
  const validCategories = Object.keys(ITEM_SOUND_CATEGORY_LABELS) as ItemSoundCategory[];
  const category = validCategories.includes(raw.category as ItemSoundCategory)
    ? raw.category as ItemSoundCategory
    : 'custom';
  const soundSlot = typeof raw.soundSlot === 'number' && Number.isFinite(raw.soundSlot) && raw.soundSlot > 0
    ? Math.floor(raw.soundSlot)
    : null;
  const volume = typeof raw.volume === 'number' && Number.isFinite(raw.volume)
    ? Math.max(0, Math.min(1, raw.volume))
    : 1;
  return { itemName, category, soundSlot, volume };
}

export function normalizeItemSoundRules(value: unknown): ItemSoundRules {
  if (!value || typeof value !== 'object') return {};
  const out: ItemSoundRules = {};
  for (const [rawKey, rawRule] of Object.entries(value as Record<string, unknown>)) {
    const rule = normalizeItemSoundRule(rawRule);
    if (!rule) continue;
    const key = rawKey.includes(':') ? rawKey : itemSoundKey(rule.category, rule.itemName);
    out[key] = rule;
  }
  return out;
}

export function buildItemSoundCatalog(): ItemSoundCatalogItem[] {
  const seen = new Set<string>();
  const out: ItemSoundCatalogItem[] = [];
  const add = (category: ItemSoundCategory, itemName: string, key = itemSoundKey(category, itemName)) => {
    const cleanName = cleanTrackedItemName(itemName);
    if (!cleanName || seen.has(key)) return;
    const isUnique = category === 'unique' || category === 'hellforged';
    const label = cleanName;
    seen.add(key);
    out.push({
      key,
      itemName: cleanName,
      category,
      label,
      searchText: `${cleanName} ${ITEM_SOUND_CATEGORY_LABELS[category]} ${isUnique ? `quality level ${uniqueQualityLabel(cleanName)}` : ''}`.toLowerCase(),
    });
  };

  for (const item of buildHolyGrailItems()) {
    add(categoryForGrailItem(item), item.name, item.key);
  }
  for (const rune of RUNE_NAMES) {
    add('rune', rune, itemSoundKey('rune', rune));
  }
  for (const essence of BOSS_ESSENCE_NAMES) {
    add('essence', essence, itemSoundKey('essence', essence));
  }
  for (const material of MATERIAL_NAMES) {
    if (ITEM_SOUND_MATERIAL_EXCLUDED_KEYS.has(normalizeHolyGrailKey(material))) continue;
    add('material', material, itemSoundKey('material', material));
  }

  return out.sort((a, b) =>
    a.category === b.category
      ? a.itemName.localeCompare(b.itemName)
      : ITEM_SOUND_CATEGORY_LABELS[a.category].localeCompare(ITEM_SOUND_CATEGORY_LABELS[b.category]),
  );
}

function normalizedWordText(value: string | undefined | null): string {
  return normalizeHolyGrailKey(value ?? '').replace(/-/g, ' ');
}

function containsItemName(haystack: string, needleName: string): boolean {
  const needle = normalizedWordText(needleName);
  if (!needle) return false;
  return ` ${haystack} `.includes(` ${needle} `);
}

function materialRuleNames(itemName: string): string[] {
  const aliases = MATERIAL_ALIASES[normalizeHolyGrailKey(itemName)] ?? [];
  return [itemName, ...aliases];
}

function namesForDrop(item: DropTrackerItemLike): string[] {
  return [item.canonical_name, item.canonicalName, item.name, item.base_name, item.baseName, item.category]
    .map((value) => cleanTrackedItemName(value))
    .filter(Boolean);
}

export function materialNameFromDrop(item: DropTrackerItemLike): string | null {
  const dropNames = namesForDrop(item);
  if (dropNames.length === 0) return null;
  const haystack = normalizedWordText(dropNames.join(' '));

  for (const material of MATERIAL_NAMES) {
    const candidates = materialRuleNames(material);
    if (candidates.some((candidate) => dropNames.some((name) => normalizeHolyGrailKey(name) === normalizeHolyGrailKey(candidate)))) {
      return material;
    }
    if (candidates.some((candidate) => containsItemName(haystack, candidate))) {
      return material;
    }
  }

  return null;
}

function grailRuleKeysForDrop(item: DropTrackerItemLike): string[] {
  const drop = item as ItemSoundDropLike;
  const names = namesForDrop(item);
  const keys: string[] = [];
  const codeMatch = holyGrailItemFromDrop(item);
  if (codeMatch) keys.push(codeMatch.key);
  const quality = String(item.quality ?? '').toLowerCase();
  const isHellforged = item.is_hellforged === true || item.isHellforged === true || names.some((name) => /^Hellforged\b/i.test(name));
  const categories: HolyGrailCategoryKey[] = [];

  if (isHellforged) categories.push('ssu');
  if (quality === 'set') categories.push('sets');
  if (drop.is_runeword === true || drop.isRuneword === true) categories.push('runewords');
  if (quality === 'unique' || isHellforged) categories.push('su');
  categories.push('fateCards', 'hatredOrbs', 'essences', 'ascendancy', 'ssu', 'sets', 'runewords', 'su');

  for (const category of Array.from(new Set(categories))) {
    for (const name of names) {
      const match = canonicalHolyGrailItem(category, name);
      if (match) keys.push(match.key);
    }
  }
  return Array.from(new Set(keys));
}

export function findItemSoundRuleForDrop(
  item: DropTrackerItemLike,
  rules: ItemSoundRules,
): ItemSoundRule | null {
  const normalizedRules = normalizeItemSoundRules(rules);
  if (Object.keys(normalizedRules).length === 0) return null;

  for (const key of grailRuleKeysForDrop(item)) {
    const rule = normalizedRules[key];
    if (rule?.soundSlot != null) return rule;
  }

  const rune = runeNameFromDrop(item);
  if (rune) {
    const rule = normalizedRules[itemSoundKey('rune', rune)];
    if (rule?.soundSlot != null) return rule;
  }

  const dropNames = namesForDrop(item);
  const haystack = normalizedWordText(dropNames.join(' '));
  for (const [key, rule] of Object.entries(normalizedRules)) {
    if (rule.soundSlot == null) continue;
    if (rule.category !== 'material' && rule.category !== 'fateCard' && rule.category !== 'essence' && rule.category !== 'custom') continue;
    const candidates = rule.category === 'custom' ? [rule.itemName] : materialRuleNames(rule.itemName);
    if (candidates.some((candidate) => dropNames.some((name) => normalizeHolyGrailKey(name) === normalizeHolyGrailKey(candidate)))) return rule;
    if (candidates.some((candidate) => containsItemName(haystack, candidate))) return rule;
    if (key === itemSoundKey(rule.category, rule.itemName)) continue;
  }

  return null;
}
