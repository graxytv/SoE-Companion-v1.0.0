import { RUNE_NAMES, type RuneName, runeCategory } from './drop-tracker-categories';
import {
  holyGrailCategoryProgress,
  holyGrailProgress,
  normalizeHolyGrailKey,
  type HolyGrailCategoryKey,
  type HolyGrailFoundEntry,
  type HolyGrailItem,
} from './holy-grail';

export type AchievementTier = 'Bronze' | 'Silver' | 'Gold' | 'Legendary';
export type AchievementCategory =
  | 'Unique Finds'
  | 'Kills'
  | 'Bossing'
  | 'Levels'
  | 'Grail'
  | 'Runes'
  | 'Fate Cards'
  | 'Materials';

export interface AchievementUnlockEntry {
  id: string;
  name: string;
  category: AchievementCategory;
  tier: AchievementTier;
  unlockedAt: string;
  detail?: string | null;
}

export interface AchievementCharacterLevel {
  name: string;
  className: string;
  level: number;
}

export interface AchievementStats {
  uniqueItemsFound: number;
  firstEliteUniqueName: string | null;
  totalKills: number;
  bossKills: Record<string, number>;
  materialFinds: Record<string, number>;
  fateCardsFound: number;
  tier0FateCardsFound: number;
  characterLevels: Record<string, AchievementCharacterLevel>;
  corruptions: number;
  bricks: number;
  currentBrickStreak: number;
  bestBrickStreak: number;
  unlocked: Record<string, string>;
  history: AchievementUnlockEntry[];
}

export interface AchievementSettings {
  overlayEnabled: boolean;
  overlayDuration: number;
  overlayFontSize: number;
  overlayOpacity: number;
  soundEnabled: boolean;
  soundSlot: number | null;
  soundVolume: number;
}

export interface AchievementContext {
  stats: AchievementStats;
  holyGrailFound: Record<string, HolyGrailFoundEntry>;
  holyGrailItems: HolyGrailItem[];
  runeTrackerCounts: Partial<Record<RuneName, number>>;
}

export interface AchievementDefinition {
  id: string;
  name: string;
  category: AchievementCategory;
  tier: AchievementTier;
  target: number;
  progress: (ctx: AchievementContext) => number;
  description?: string;
  manual?: boolean;
  hiddenUntilUnlocked?: boolean;
}

export interface AchievementProgress extends AchievementDefinition {
  value: number;
  percent: number;
  complete: boolean;
  unlockedAt: string | null;
}

export const ELITE_UNIQUE_NAMES = new Set([
  "Akarat's Devotion",
  'Arachnid Mesh',
  'Askaris Zephyr',
  'Azurewrath',
  'Brimstone Rain',
  'Crackleshot',
  "Death's Fathom",
  "Fara's Reminiscence",
  "Fara's Remorse",
  "Fara's Resentment",
  'Hailstorm',
  'Halo of the Justicar',
  'Headhunter',
  'Kashyas Sorrow',
  'Kratos Fury',
  'Mageblood',
  'Martyrdom',
  "Nihlathak's Malice",
  'Occultist',
  'Odium',
  'Purgatory',
  "Raekor's Virtue",
  "Stalker's Cull",
  'The Squire',
  'The Veinpiercer',
  "Tyrael's Might",
  'Wake of Destruction',
  'Whispering Skull',
  "Zerae's Resolve",
]);

export const DEFAULT_ACHIEVEMENT_SETTINGS: AchievementSettings = {
  overlayEnabled: true,
  overlayDuration: 6000,
  overlayFontSize: 15,
  overlayOpacity: 0.94,
  soundEnabled: true,
  soundSlot: null,
  soundVolume: 1,
};

export function defaultAchievementStats(): AchievementStats {
  return {
    uniqueItemsFound: 0,
    firstEliteUniqueName: null,
    totalKills: 0,
    bossKills: {},
    materialFinds: {},
    fateCardsFound: 0,
    tier0FateCardsFound: 0,
    characterLevels: {},
    corruptions: 0,
    bricks: 0,
    currentBrickStreak: 0,
    bestBrickStreak: 0,
    unlocked: {},
    history: [],
  };
}

export const MATERIAL_ACHIEVEMENT_ITEMS = [
  { name: 'Black Soulstone', aliases: ['Black Soulstones'] },
  { name: 'Demonic Insignia' },
  { name: 'Flesh of Malic' },
  { name: "Lilith's Mirror" },
  { name: 'Key of Terror', aliases: ['Pandemonium Key 1'] },
  { name: 'Key of Hate', aliases: ['Pandemonium Key 2'] },
  { name: 'Key of Destruction', aliases: ['Pandemonium Key 3'] },
  { name: 'Prime Evil Soul' },
  { name: 'Pure Demonic Essence' },
  { name: 'Sigil of Korlic' },
  { name: 'Sigil of Madawc' },
  { name: 'Sigil of Talic' },
  { name: 'Sigil Fragment of Apostasy' },
  { name: 'Sigil Fragment of Betrayal' },
  { name: 'Sigil Fragment of Damnation' },
  { name: 'Sigil Fragment of Resurrection' },
  { name: 'Splinter of the Void' },
  { name: 'Talisman of Transgression' },
  { name: 'Trang-Oul Jawbone' },
  { name: 'Worldstone Shard', aliases: ['Worldstone Shards'] },
  {
    name: 'Tainted Worldstone Shard',
    aliases: ['Tainted World Stone Shard', 'Tainted Worldstone Shards', 'Tainted WSS', 'Corrupted Worldstone Shard', 'cwss'],
  },
  { name: 'Crystallised Cindersoul' },
  { name: 'Demonic Cube' },
  { name: 'Larzuk Puzzlebox', aliases: ["Larzuk's Puzzlebox"] },
  { name: 'Desecration Orb' },
  { name: 'Mythic Orb' },
  { name: 'Divine Orb' },
  { name: 'Sacred Orb' },
  { name: 'Eternal Orb' },
  { name: 'Exalted Orb' },
  { name: 'Orb of Extraction' },
  { name: 'Horadrim Scarab' },
  { name: "Cartographer's Chisel" },
  { name: "Cartographer's Chisel of Avarice", aliases: ['Chisel Avarice', 'Chisel-Avarice', 'Chisel Averice', 'Chisel-Averice'] },
  { name: "Cartographer's Chisel of Procurement", aliases: ['Chisel Procurement', 'Chisel-Procurement'] },
  { name: 'Glyph of Nemeses', aliases: ['Glyph of Nemesis'] },
  { name: 'Glyph of Corruption' },
  { name: 'Glyph of Adversaries', aliases: ['Glyph of Adverseries'] },
  { name: 'Glyph of Moo' },
  { name: 'Glyph of Lineage' },
  { name: 'Terror of Resplendence' },
  { name: 'Terror of Opulence' },
  { name: 'Terror of Ethereal' },
  { name: 'Terror of Absolute' },
] as const;

function normalizeMaterialText(value: string | null | undefined): string {
  return String(value ?? '')
    .replace(/[\u2018\u2019]/g, "'")
    .replace(/\s+\[[^\]]{1,32}\]\s*$/g, '')
    .replace(/\s+\([^)]{1,32}\)\s*$/g, '')
    .replace(/[^a-z0-9]+/gi, ' ')
    .trim()
    .replace(/\s+/g, ' ')
    .toLowerCase();
}

export function materialAchievementKey(name: string): string {
  return normalizeMaterialText(name).replace(/\s+/g, '-');
}

export function materialAchievementNameFromDrop(name: string | null | undefined): string | null {
  const normalized = normalizeMaterialText(name);
  if (!normalized) return null;
  for (const item of MATERIAL_ACHIEVEMENT_ITEMS) {
    const names = [item.name, ...('aliases' in item ? item.aliases : [])];
    if (names.some((candidate) => normalizeMaterialText(candidate) === normalized)) {
      return item.name;
    }
  }
  return null;
}

function bossKey(name: string, tier: string): string {
  return `${name}:${tier}`;
}

function bossAny(ctx: AchievementContext, name: string): number {
  const explicitTotal = ctx.stats.bossKills[bossKey(name, 'Any')];
  if (Number.isFinite(explicitTotal)) return Math.max(0, explicitTotal ?? 0);
  return Object.entries(ctx.stats.bossKills)
    .filter(([key]) => key.startsWith(`${name}:`) && key !== bossKey(name, 'Any'))
    .reduce((sum, [, count]) => sum + Math.max(0, count ?? 0), 0);
}

function bossTier(ctx: AchievementContext, name: string, tier: string): number {
  return Math.max(0, ctx.stats.bossKills[bossKey(name, tier)] ?? 0);
}

function highRuneTotal(ctx: AchievementContext): number {
  return RUNE_NAMES
    .filter((rune) => runeCategory(rune) === 'highRune')
    .reduce((sum, rune) => sum + (ctx.runeTrackerCounts[rune] ?? 0), 0);
}

function runeTotal(ctx: AchievementContext): number {
  return RUNE_NAMES.reduce((sum, rune) => sum + (ctx.runeTrackerCounts[rune] ?? 0), 0);
}

function fateCardTotal(ctx: AchievementContext): number {
  return ctx.stats.fateCardsFound ?? 0;
}

function tier0FateCardTotal(ctx: AchievementContext): number {
  return ctx.stats.tier0FateCardsFound ?? 0;
}

function characterLevels(ctx: AchievementContext): AchievementCharacterLevel[] {
  return Object.values(ctx.stats.characterLevels).filter((char) => Number.isFinite(char.level));
}

function charactersAtLeast(ctx: AchievementContext, level: number): number {
  return characterLevels(ctx).filter((char) => char.level >= level).length;
}

function allClassesLevel99(ctx: AchievementContext): number {
  const classes = new Set(
    characterLevels(ctx)
      .filter((char) => char.level >= 99)
      .map((char) => char.className.trim().toLowerCase())
      .filter(Boolean),
  );
  return classes.size;
}

function grailPercent(ctx: AchievementContext): number {
  return holyGrailProgress(ctx.holyGrailItems, ctx.holyGrailFound).percent;
}

function grailCategoryComplete(ctx: AchievementContext, category: HolyGrailCategoryKey): number {
  const categoryProgress = holyGrailCategoryProgress(ctx.holyGrailItems, ctx.holyGrailFound, category);
  return categoryProgress.total > 0 && categoryProgress.found >= categoryProgress.total ? 1 : 0;
}

function runeGrailComplete(ctx: AchievementContext): number {
  return RUNE_NAMES.every((rune) => (ctx.runeTrackerCounts[rune] ?? 0) > 0) ? 1 : 0;
}

function grailFindByName(
  ctx: AchievementContext,
  name: string,
  aliases: readonly string[] = [],
): number {
  const wanted = new Set([name, ...aliases].map(normalizeHolyGrailKey));
  return Object.values(ctx.holyGrailFound).some((entry) => wanted.has(normalizeHolyGrailKey(entry.name))) ? 1 : 0;
}

const uniqueAchievements: AchievementDefinition[] = [
  { id: 'unique-100', name: 'Find 100 Unique Items', category: 'Unique Finds', tier: 'Bronze', target: 100, progress: (ctx) => ctx.stats.uniqueItemsFound },
  { id: 'unique-500', name: 'Find 500 Unique Items', category: 'Unique Finds', tier: 'Silver', target: 500, progress: (ctx) => ctx.stats.uniqueItemsFound },
  { id: 'unique-1000', name: 'Find 1000 Unique Items', category: 'Unique Finds', tier: 'Gold', target: 1000, progress: (ctx) => ctx.stats.uniqueItemsFound },
  { id: 'unique-5000', name: 'Find 5000 Unique Items', category: 'Unique Finds', tier: 'Legendary', target: 5000, progress: (ctx) => ctx.stats.uniqueItemsFound },
  { id: 'first-elite-unique', name: 'Find Your First Elite Unique', category: 'Unique Finds', tier: 'Legendary', target: 1, progress: (ctx) => ctx.stats.firstEliteUniqueName ? 1 : 0 },
  { id: 'unique-horadrim-navigator', name: 'Find Horadrim Navigator', category: 'Unique Finds', tier: 'Legendary', target: 1, progress: (ctx) => grailFindByName(ctx, 'Horadrim Navigator', ['Horadric Navigator']) },
  { id: 'unique-horadrim-almanac', name: 'Find Horadrim Almanac', category: 'Unique Finds', tier: 'Legendary', target: 1, progress: (ctx) => grailFindByName(ctx, 'Horadrim Almanac', ['Horadric Almanac']) },
  { id: 'unique-skeleton-key', name: 'Find Skeleton Key', category: 'Unique Finds', tier: 'Legendary', target: 1, progress: (ctx) => grailFindByName(ctx, 'Skeleton Key') },
];

const killAchievements: AchievementDefinition[] = [
  { id: 'kills-10000', name: '10000 Total Kills', category: 'Kills', tier: 'Bronze', target: 10000, progress: (ctx) => ctx.stats.totalKills, manual: true },
  { id: 'kills-100000', name: '100000 Total Kills', category: 'Kills', tier: 'Silver', target: 100000, progress: (ctx) => ctx.stats.totalKills, manual: true },
  { id: 'kills-1000000', name: '1000000 Total Kills', category: 'Kills', tier: 'Gold', target: 1000000, progress: (ctx) => ctx.stats.totalKills, manual: true },
  { id: 'kills-10000000', name: '10000000 Total Kills', category: 'Kills', tier: 'Legendary', target: 10000000, progress: (ctx) => ctx.stats.totalKills, manual: true },
];

const bossAchievements: AchievementDefinition[] = [
  ...[
    ['DClone', ['T2']],
    ['Rathma', ['T2']],
    ['Lucion', ['T2']],
    ['Kiln', ['T2']],
  ].flatMap(([boss, tiers]) => [
    { id: `boss-${String(boss).toLowerCase()}-any`, name: `Kill ${boss} (any tier)`, category: 'Bossing' as const, tier: 'Bronze' as const, target: 1, progress: (ctx: AchievementContext) => bossAny(ctx, String(boss)) },
    ...((tiers as string[]).map((tier) => ({ id: `boss-${String(boss).toLowerCase()}-${tier.toLowerCase()}`, name: `Kill ${boss} (${tier})`, category: 'Bossing' as const, tier: 'Silver' as const, target: 1, progress: (ctx: AchievementContext) => bossTier(ctx, String(boss), tier) }))),
    { id: `boss-${String(boss).toLowerCase()}-100`, name: `Kill ${boss} 100 Times (any tier)`, category: 'Bossing' as const, tier: 'Legendary' as const, target: 100, progress: (ctx: AchievementContext) => bossAny(ctx, String(boss)) },
  ]),
  ...[
    'Dungeon Boss',
    'Map Boss',
    'Uber Ancients',
    'Uber Tristram',
    'Andariel',
    'Duriel',
    'Mephisto',
    'Diablo',
    'Baal',
  ].flatMap((boss) => [
    { id: `boss-${boss.toLowerCase().replace(/\s+/g, '-')}-first`, name: `Kill ${boss}`, category: 'Bossing' as const, tier: 'Bronze' as const, target: 1, progress: (ctx: AchievementContext) => bossAny(ctx, boss) },
    { id: `boss-${boss.toLowerCase().replace(/\s+/g, '-')}-100`, name: `Kill ${boss} 100 Times`, category: 'Bossing' as const, tier: boss.includes('Uber') ? 'Legendary' as const : 'Gold' as const, target: 100, progress: (ctx: AchievementContext) => bossAny(ctx, boss) },
  ]),
];

const levelAchievements: AchievementDefinition[] = [
  ...[50, 70, 80, 90, 95, 96, 97, 98, 99].map((level, index) => ({
    id: `level-${level}`,
    name: `Level ${level}`,
    category: 'Levels' as const,
    tier: (level >= 98 ? 'Legendary' : level >= 95 ? 'Gold' : level >= 90 ? 'Silver' : 'Bronze') as AchievementTier,
    target: level,
    progress: (ctx: AchievementContext) => Math.max(0, ...characterLevels(ctx).map((char) => char.level), index === -1 ? 0 : 0),
    manual: true,
  })),
  { id: 'chars-2-level-90', name: '2 Characters Above Level 90', category: 'Levels', tier: 'Silver', target: 2, progress: (ctx) => charactersAtLeast(ctx, 90), manual: true },
  { id: 'chars-3-level-95', name: '3 Characters Above Level 95', category: 'Levels', tier: 'Gold', target: 3, progress: (ctx) => charactersAtLeast(ctx, 95), manual: true },
  { id: 'chars-4-level-96', name: '4 Characters Above Level 96', category: 'Levels', tier: 'Gold', target: 4, progress: (ctx) => charactersAtLeast(ctx, 96), manual: true },
  { id: 'chars-5-level-97', name: '5 Characters Above Level 97', category: 'Levels', tier: 'Legendary', target: 5, progress: (ctx) => charactersAtLeast(ctx, 97), manual: true },
  { id: 'chars-6-level-98', name: '6 Characters Above Level 98', category: 'Levels', tier: 'Legendary', target: 6, progress: (ctx) => charactersAtLeast(ctx, 98), manual: true },
  { id: 'all-classes-99', name: 'All Classes Level 99', category: 'Levels', tier: 'Legendary', target: 7, progress: allClassesLevel99, manual: true },
];

const grailAchievements: AchievementDefinition[] = [
  ...[10, 25, 50, 75, 90, 100].map((target) => ({
    id: `grail-${target}`,
    name: `Reach ${target}% Grail Completion`,
    category: 'Grail' as const,
    tier: (target >= 90 ? 'Legendary' : target >= 50 ? 'Gold' : target >= 25 ? 'Silver' : 'Bronze') as AchievementTier,
    target,
    progress: grailPercent,
  })),
  { id: 'unique-grail-complete', name: 'Complete the Unique Grail Category', category: 'Grail', tier: 'Legendary', target: 1, progress: (ctx) => grailCategoryComplete(ctx, 'su') },
  { id: 'hellforged-grail-complete', name: 'Complete the Hellforged Grail Category', category: 'Grail', tier: 'Legendary', target: 1, progress: (ctx) => grailCategoryComplete(ctx, 'ssu') },
  { id: 'set-grail-complete', name: 'Complete the Set Items Grail Category', category: 'Grail', tier: 'Legendary', target: 1, progress: (ctx) => grailCategoryComplete(ctx, 'sets') },
  { id: 'rune-grail-complete', name: 'Complete the Rune Grail Category', category: 'Grail', tier: 'Legendary', target: 1, progress: runeGrailComplete },
  { id: 'runeword-grail-complete', name: 'Complete the Runeword Grail Category', category: 'Grail', tier: 'Legendary', target: 1, progress: (ctx) => grailCategoryComplete(ctx, 'runewords') },
  { id: 'fate-card-grail-complete', name: 'Complete the Fate Card Grail Category', category: 'Grail', tier: 'Legendary', target: 1, progress: (ctx) => grailCategoryComplete(ctx, 'fateCards') },
  { id: 'hatred-orb-grail-complete', name: 'Complete the Hatred Orb Grail Category', category: 'Grail', tier: 'Legendary', target: 1, progress: (ctx) => grailCategoryComplete(ctx, 'hatredOrbs') },
  { id: 'essence-grail-complete', name: 'Complete the Essence Grail Category', category: 'Grail', tier: 'Legendary', target: 1, progress: (ctx) => grailCategoryComplete(ctx, 'essences') },
  { id: 'ascendancy-grail-complete', name: 'Complete the Ascendancy Grail Category', category: 'Grail', tier: 'Legendary', target: 1, progress: (ctx) => grailCategoryComplete(ctx, 'ascendancy') },
];

const runeAchievements: AchievementDefinition[] = [
  { id: 'runes-100', name: 'Find 100 Total Runes', category: 'Runes', tier: 'Bronze', target: 100, progress: runeTotal },
  { id: 'runes-500', name: 'Find 500 Total Runes', category: 'Runes', tier: 'Silver', target: 500, progress: runeTotal },
  { id: 'runes-1000', name: 'Find 1000 Total Runes', category: 'Runes', tier: 'Gold', target: 1000, progress: runeTotal },
  { id: 'first-high-rune', name: 'Find Your First High Rune', category: 'Runes', tier: 'Gold', target: 1, progress: highRuneTotal },
  { id: 'first-zod', name: 'Find Your First Zod', category: 'Runes', tier: 'Legendary', target: 1, progress: (ctx) => ctx.runeTrackerCounts.Zod ?? 0 },
  { id: 'two-high-runes-one-run', name: 'Find 2 High Runes In One Run', category: 'Runes', tier: 'Legendary', target: 2, progress: (ctx) => highRuneTotal(ctx), description: 'Currently uses tracked high-rune progress until per-run high-rune history is expanded.' },
];

const fateCardAchievements: AchievementDefinition[] = [
  ...[1, 100, 1000, 10000].map((target) => ({
    id: `fate-cards-${target}`,
    name: `Find ${target.toLocaleString()} Fate Card${target === 1 ? '' : 's'}`,
    category: 'Fate Cards' as const,
    tier: (target >= 10000 ? 'Legendary' : target >= 1000 ? 'Gold' : target >= 100 ? 'Silver' : 'Bronze') as AchievementTier,
    target,
    progress: fateCardTotal,
  })),
  ...[1, 25, 50, 75, 100, 250, 500, 1000].map((target) => ({
    id: `tier-0-fate-cards-${target}`,
    name: `Find ${target.toLocaleString()} Tier 0 Fate Card${target === 1 ? '' : 's'}`,
    category: 'Fate Cards' as const,
    tier: (target >= 1000 ? 'Legendary' : target >= 250 ? 'Gold' : target >= 75 ? 'Silver' : 'Bronze') as AchievementTier,
    target,
    progress: tier0FateCardTotal,
  })),
];

const materialAchievements: AchievementDefinition[] = MATERIAL_ACHIEVEMENT_ITEMS.map((item) => ({
  id: `material-${materialAchievementKey(item.name)}`,
  name: `Find ${item.name}`,
  category: 'Materials' as const,
  tier: 'Bronze' as const,
  target: 1,
  progress: (ctx: AchievementContext) => ctx.stats.materialFinds[materialAchievementKey(item.name)] ?? 0,
}));

export const ACHIEVEMENTS: AchievementDefinition[] = [
  ...uniqueAchievements,
  ...killAchievements,
  ...bossAchievements,
  ...levelAchievements,
  ...grailAchievements,
  ...runeAchievements,
  ...fateCardAchievements,
  ...materialAchievements,
];

export const ACHIEVEMENT_CATEGORIES: AchievementCategory[] = [
  'Unique Finds',
  'Kills',
  'Bossing',
  'Levels',
  'Grail',
  'Runes',
  'Fate Cards',
  'Materials',
];

export function normalizeAchievementStats(value: unknown): AchievementStats {
  const defaults = defaultAchievementStats();
  if (!value || typeof value !== 'object') return defaults;
  const input = value as Partial<AchievementStats>;
  const unlocked = input.unlocked && typeof input.unlocked === 'object' ? input.unlocked : {};
  const history = Array.isArray(input.history)
    ? input.history
        .filter((entry): entry is AchievementUnlockEntry => !!entry && typeof entry === 'object' && typeof entry.id === 'string')
        .slice(0, 200)
    : [];
  return {
    uniqueItemsFound: Math.max(0, Math.floor(Number(input.uniqueItemsFound) || 0)),
    firstEliteUniqueName: typeof input.firstEliteUniqueName === 'string' && input.firstEliteUniqueName.trim() ? input.firstEliteUniqueName : null,
    totalKills: Math.max(0, Math.floor(Number(input.totalKills) || 0)),
    bossKills: Object.fromEntries(
      Object.entries(input.bossKills ?? {}).map(([key, count]) => [key, Math.max(0, Math.floor(Number(count) || 0))]),
    ),
    materialFinds: Object.fromEntries(
      Object.entries(input.materialFinds ?? {}).map(([key, count]) => [key, Math.max(0, Math.floor(Number(count) || 0))]),
    ),
    fateCardsFound: Math.max(0, Math.floor(Number(input.fateCardsFound) || 0)),
    tier0FateCardsFound: Math.max(0, Math.floor(Number(input.tier0FateCardsFound) || 0)),
    characterLevels: Object.fromEntries(
      Object.entries(input.characterLevels ?? {}).map(([key, char]) => {
        const c = char as Partial<AchievementCharacterLevel>;
        return [key, {
          name: String(c.name || key),
          className: String(c.className || ''),
          level: Math.max(1, Math.min(99, Math.floor(Number(c.level) || 1))),
        }];
      }),
    ),
    corruptions: Math.max(0, Math.floor(Number(input.corruptions) || 0)),
    bricks: Math.max(0, Math.floor(Number(input.bricks) || 0)),
    currentBrickStreak: Math.max(0, Math.floor(Number(input.currentBrickStreak) || 0)),
    bestBrickStreak: Math.max(0, Math.floor(Number(input.bestBrickStreak) || 0)),
    unlocked: Object.fromEntries(Object.entries(unlocked).filter(([, time]) => typeof time === 'string')),
    history,
  };
}

export function normalizeAchievementSettings(value: unknown): AchievementSettings {
  const defaults = DEFAULT_ACHIEVEMENT_SETTINGS;
  if (!value || typeof value !== 'object') return { ...defaults };
  const input = value as Partial<AchievementSettings>;
  return {
    overlayEnabled: input.overlayEnabled ?? defaults.overlayEnabled,
    overlayDuration: Math.max(1500, Math.min(20000, Math.floor(Number(input.overlayDuration) || defaults.overlayDuration))),
    overlayFontSize: Math.max(11, Math.min(28, Math.floor(Number(input.overlayFontSize) || defaults.overlayFontSize))),
    overlayOpacity: Math.max(0.25, Math.min(1, Number(input.overlayOpacity) || defaults.overlayOpacity)),
    soundEnabled: input.soundEnabled ?? defaults.soundEnabled,
    soundSlot: typeof input.soundSlot === 'number' && input.soundSlot > 0 ? Math.floor(input.soundSlot) : null,
    soundVolume: Math.max(0, Math.min(1, Number(input.soundVolume) || defaults.soundVolume)),
  };
}

export function evaluateAchievements(ctx: AchievementContext): AchievementProgress[] {
  return ACHIEVEMENTS.map((achievement) => {
    const value = Math.max(0, achievement.progress(ctx));
    const percent = achievement.target > 0 ? Math.min(100, (value / achievement.target) * 100) : 0;
    return {
      ...achievement,
      value,
      percent,
      complete: value >= achievement.target,
      unlockedAt: value >= achievement.target ? ctx.stats.unlocked[achievement.id] ?? null : null,
    };
  });
}

export function nextAchievement(progress: AchievementProgress[]): AchievementProgress | null {
  return progress
    .filter((achievement) => !achievement.complete)
    .sort((a, b) => b.percent - a.percent)[0] ?? null;
}

export function unlockedAchievementCount(progress: AchievementProgress[]): number {
  return progress.filter((achievement) => achievement.complete).length;
}

export function achievementDetail(id: string, ctx: AchievementContext): string | null {
  if (id === 'first-elite-unique') return ctx.stats.firstEliteUniqueName;
  return null;
}
