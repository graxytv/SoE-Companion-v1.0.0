export const SOE_13_UNIQUE_ITEMS = [
  'Tabula Rasa',
  'Svalinn',
  'Rathpith Globe',
  'Screams of the Dessicated',
] as const;

export const SOE_13_HATRED_ORB_ITEMS = [
  'Hatred Orb of Equipment',
  'Hatred Orb of Accessories',
  'Hatred Orb of Essences',
  'Hatred Orb of Fate Cards',
  'Hatred Orb of Runes',
  'Hatred Orb of Currency',
] as const;

export const SOE_13_FATE_CARD_ITEMS = [
  'The Apothecary',
  'The Eye of Terror',
  'The Doctor',
  'The Sephiroth',
  'The Immortal',
  'Wealth and Power',
  'House of Mirrors',
  'Unrequited Love',
  'Seraphic Favor',
  'Alluring Bounty',
  'A Fate Worse Than Death',
  'A Stone Perfected',
  'Lonely Warrior',
  'The Polymath',
  'Iridescent Dream',
  'The Web',
  'I see Brothers',
  'The Reaper',
  'The Shieldbearer',
  'The Avenger',
  "Gemcutter's Promise",
  'Chaotic Disposition',
  'The Unexpected Prize',
  'Further Invention',
  "Gemcutter's Mercy",
  'Lost Worlds',
  'Boundless Realms',
  'The Journey',
  "Cartographer's Delight",
  'The Cartographer',
  'More is Never Enough',
  'Ambush',
  'The Artist',
  'Misery in Darkness',
  "Bowyer's Dream",
  'Dormant Allure',
  'The Assegai',
  'The Rite of Elements',
  'Call of the First Ones',
  'Therianthropy',
  'Cursed Words',
  'The Dark Mage',
  'The Lich',
  'The Celestial Justicar',
  'The Crusader',
  'The Bulwark',
  'A Chilling Wind',
  'The Blazing Fire',
  'The Coming Storm',
  'The Warlord',
  'The Battle Born',
  'The Skirmisher',
  'The Undaunted',
  "Gemcutter's Gift",
  "The Archmage's Right Hand",
  'The Survivalist',
  'Abandoned Wealth',
  'The Scout',
  'Glimmer of Hope',
  'The Escape',
  'Humility',
  "The Saint's Treasure",
  'The Inventor',
] as const;

export const SOE_13_FATE_CARD_TIERS = [0, 1, 2, 3, 4, 5] as const;

export interface Soe13FateCardInfo {
  name: string;
  tier: number;
  reward: string;
  dropLocation: string;
  amountRequired: number;
}

export interface Soe13FateCardTrackerRow {
  key: string;
  label: string;
  tier: number;
  count: number;
  kind: 'tier' | 'card';
}

export const SOE_13_FATE_CARD_INFO = [
  { name: 'The Apothecary', tier: 0, reward: '4 Flask Mageblood', dropLocation: 'T3 and T4 Map drop', amountRequired: 5 },
  { name: 'The Eye of Terror', tier: 0, reward: '3 Flask Mageblood', dropLocation: 'Diablo Clone', amountRequired: 8 },
  { name: 'The Doctor', tier: 0, reward: 'Headhunter', dropLocation: 'T3 and T4 Map drop', amountRequired: 6 },
  { name: 'The Sephiroth', tier: 0, reward: '10x Divine Orb', dropLocation: 'T3 and T4 Map drop', amountRequired: 8 },
  { name: 'The Immortal', tier: 0, reward: 'Zod Rune', dropLocation: 'Lucion', amountRequired: 2 },
  { name: 'Wealth and Power', tier: 0, reward: 'Vial of Lightsong', dropLocation: 'T3 and T4 Map drop', amountRequired: 4 },
  { name: 'House of Mirrors', tier: 0, reward: "Lilith's Mirror", dropLocation: 'T3 and T4 Map drop', amountRequired: 6 },
  { name: 'Unrequited Love', tier: 0, reward: 'Cham Rune', dropLocation: 'T3 and T4 Map drop', amountRequired: 5 },
  { name: 'Seraphic Favor', tier: 0, reward: "Tyrael's Might", dropLocation: 'T3 and T4 Map drop', amountRequired: 5 },
  { name: 'Alluring Bounty', tier: 0, reward: '2x Ber Rune', dropLocation: 'T3 and T4 Map drop', amountRequired: 8 },
  { name: 'A Fate Worse Than Death', tier: 0, reward: 'Jah Rune', dropLocation: 'Lazarus', amountRequired: 3 },
  { name: 'A Stone Perfected', tier: 0, reward: 'The Stone of Jordan with +1 All Skills Corruption', dropLocation: 'T3 and T4 Map drop', amountRequired: 5 },
  { name: 'Lonely Warrior', tier: 1, reward: 'Defiance of Destiny', dropLocation: 'T3 and T4 Map drop', amountRequired: 5 },
  { name: 'The Polymath', tier: 1, reward: 'Astramentis', dropLocation: 'T3 and T4 Map drop', amountRequired: 5 },
  { name: 'Iridescent Dream', tier: 1, reward: 'Rainbow Facet', dropLocation: 'T3 and T4 Map drop', amountRequired: 3 },
  { name: 'The Web', tier: 1, reward: 'Arachnid Mesh', dropLocation: 'T3 and T4 Map drop', amountRequired: 5 },
  { name: 'I see Brothers', tier: 1, reward: 'Call of the Brotherhood', dropLocation: 'T3 and T4 Map drop', amountRequired: 4 },
  { name: 'The Reaper', tier: 1, reward: "Death's Poise", dropLocation: 'T3 and T4 Map drop', amountRequired: 4 },
  { name: 'The Shieldbearer', tier: 1, reward: 'The Squire', dropLocation: 'T3 and T4 Map drop', amountRequired: 6 },
  { name: 'The Avenger', tier: 1, reward: 'Mjolner', dropLocation: 'T3 and T4 Map drop', amountRequired: 4 },
  { name: "Gemcutter's Promise", tier: 1, reward: 'Unique Mythical Jewel', dropLocation: 'T3 and T4 Map drop', amountRequired: 3 },
  { name: 'Chaotic Disposition', tier: 2, reward: '5x Chaos Orb', dropLocation: 'T3 and T4 Map drop', amountRequired: 1 },
  { name: 'The Unexpected Prize', tier: 2, reward: '75x Crystallised Cindersoul', dropLocation: 'T3 and T4 Map drop', amountRequired: 1 },
  { name: 'Further Invention', tier: 2, reward: 'Item Level 91 Ornate Charm', dropLocation: 'T3 and T4 Map drop', amountRequired: 6 },
  { name: "Gemcutter's Mercy", tier: 2, reward: 'Item Level 91 Rare Mythic Jewel', dropLocation: 'T3 and T4 Map drop', amountRequired: 6 },
  { name: 'Lost Worlds', tier: 2, reward: 'Rare T4 Dungeon - Sanctuary of Sin', dropLocation: 'T3 and T4 Map drop', amountRequired: 5 },
  { name: 'Boundless Realms', tier: 2, reward: 'Rare T4 Dungeon - Plains of Torment', dropLocation: 'T3 and T4 Map drop', amountRequired: 5 },
  { name: 'The Journey', tier: 2, reward: 'Rare T4 Dungeon - Steppes of Daken-Shar', dropLocation: 'T3 and T4 Map drop', amountRequired: 5 },
  { name: "Cartographer's Delight", tier: 2, reward: '5x Chisel of Procurement', dropLocation: 'Map Drop', amountRequired: 4 },
  { name: 'The Cartographer', tier: 2, reward: '5x Chisel of Avarice', dropLocation: 'Map Drop', amountRequired: 4 },
  { name: 'More is Never Enough', tier: 2, reward: '3x Glyph of Nemeses', dropLocation: 'Map Drop', amountRequired: 2 },
  { name: 'Ambush', tier: 3, reward: 'Item level 85 Assassin Trap skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'The Artist', tier: 3, reward: 'Item level 85 Assassin Martial Arts skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'Misery in Darkness', tier: 3, reward: 'Item level 85 Assassin Shadow Disciplines skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: "Bowyer's Dream", tier: 3, reward: 'Item level 85 Amazon Bow and Crossbow skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'Dormant Allure', tier: 3, reward: 'Item level 85 Amazon Passive and Magic skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'The Assegai', tier: 3, reward: 'Item level 85 Amazon Javelin and Spear skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'The Rite of Elements', tier: 3, reward: 'Item level 85 Druid Elemental skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'Call of the First Ones', tier: 3, reward: 'Item level 85 Druid Shape Summoning skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'Therianthropy', tier: 3, reward: 'Item level 85 Druid Shape Shifting skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'Cursed Words', tier: 3, reward: 'Item level 85 Necromancer Curses skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'The Dark Mage', tier: 3, reward: 'Item level 85 Necromancer Poison and Bone skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'The Lich', tier: 3, reward: 'Item level 85 Necromancer Summoning skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'The Celestial Justicar', tier: 3, reward: 'Item level 85 Paladin Combat skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'The Crusader', tier: 3, reward: 'Item level 85 Paladin Offensive Auras skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'The Bulwark', tier: 3, reward: 'Item level 85 Paladin Defensive Auras skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'A Chilling Wind', tier: 3, reward: 'Item level 85 Sorceress Cold Spells skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'The Blazing Fire', tier: 3, reward: 'Item level 85 Sorceress Fire Spells skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'The Coming Storm', tier: 3, reward: 'Item level 85 Sorceress Lightning Spells skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'The Warlord', tier: 3, reward: 'Item level 85 Barbarian Warcries skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'The Battle Born', tier: 3, reward: 'Item level 85 Barbarian Combat skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'The Skirmisher', tier: 3, reward: 'Item level 85 Barbarian Combat Masteries skiller', dropLocation: 'Hell Act 4-5 drop + Map drop', amountRequired: 2 },
  { name: 'The Undaunted', tier: 3, reward: 'Item level 87 Rare or Unique Belt', dropLocation: 'Hell drop + Map drop', amountRequired: 4 },
  { name: "Gemcutter's Gift", tier: 3, reward: 'Item Level 88 Rare Jewel', dropLocation: 'Hell Act 4-5 drop + Map Drop', amountRequired: 4 },
  { name: "The Archmage's Right Hand", tier: 4, reward: 'Manastorm', dropLocation: 'Nightmare and Hell drop + Map Drop', amountRequired: 6 },
  { name: 'The Survivalist', tier: 4, reward: '5x Orb of Alchemy', dropLocation: 'Hell World drop + Map Drop', amountRequired: 3 },
  { name: 'Abandoned Wealth', tier: 4, reward: '3x Mythic Orb', dropLocation: 'Nightmare and Hell drop + Map Drop', amountRequired: 2 },
  { name: 'The Scout', tier: 4, reward: '3x Orb of Horizons', dropLocation: 'Nightmare and Hell drop + Map Drop', amountRequired: 2 },
  { name: 'Glimmer of Hope', tier: 5, reward: 'Goldrim', dropLocation: 'Normal Act 3-5, Nightmare and Hell drop + Map Drop', amountRequired: 5 },
  { name: 'The Escape', tier: 5, reward: 'Seven-League Step', dropLocation: 'Normal Act 3-5, Nightmare and Hell drop + Map Drop', amountRequired: 5 },
  { name: 'Humility', tier: 5, reward: 'Tabula Rasa', dropLocation: 'Normal Act 3-5, Nightmare and Hell drop + Map Drop', amountRequired: 8 },
  { name: "The Saint's Treasure", tier: 5, reward: '3x Exalted Orb', dropLocation: 'Normal Act 3-5, Nightmare and Hell drop + Map Drop', amountRequired: 2 },
  { name: 'The Inventor', tier: 5, reward: '10x World Stone Shard', dropLocation: 'Normal Act 3-5, Nightmare and Hell drop + Map Drop', amountRequired: 8 },
] as const satisfies readonly Soe13FateCardInfo[];

const ESSENCE_BASES = [
  'Enhancement',
  'the Mind',
  'Haste',
  'Insulation',
  'Battle',
  'Thawing',
  'Grounding',
  'Seeking',
  'Antitoxin',
  'Scorn',
  'the Body',
  'Rage',
  'Sorcery',
  'Alacrity',
  'Spite',
  'Ice',
] as const;

export const SOE_13_ESSENCE_ITEMS = [
  ...ESSENCE_BASES.map((name) => `Essence of ${name}`),
  ...ESSENCE_BASES.map((name) => `Greater Essence of ${name}`),
  ...ESSENCE_BASES.map((name) => `Perfect Essence of ${name}`),
  'Essence of Insanity',
] as const;

export const SOE_13_ASCENDANCY_ITEMS = [
  'Ascendancy Cairn',
  'Soulstone of Might',
  'Ascendancy Soulstone Tier 2',
  'Ascendancy Soulstone Tier 3',
  'Ascendancy Soulstone Tier 4',
] as const;

export const SOE_13_GENERAL_MATERIAL_ITEMS = [
  'Chaos Orb',
  'Orb of Alchemy',
  'Orb of Horizons',
  'Cindersoul Cluster',
  'Vial of Lightsong',
] as const;

export const SOE_13_MATERIAL_ITEMS = [
  ...SOE_13_GENERAL_MATERIAL_ITEMS,
  ...SOE_13_HATRED_ORB_ITEMS,
  ...SOE_13_FATE_CARD_ITEMS,
  ...SOE_13_ESSENCE_ITEMS,
  ...SOE_13_ASCENDANCY_ITEMS,
] as const;

export function normalizeSoe13ItemKey(value: string): string {
  return value
    .trim()
    .toLowerCase()
    .replace(/[\u2018\u2019]/g, "'")
    .replace(/[^a-z0-9]+/g, " ")
    .trim()
    .replace(/\s+/g, "-");
}

export const SOE_13_ITEM_ALIASES: Record<string, readonly string[]> = {
  [normalizeSoe13ItemKey('Screams of the Dessicated')]: ['Screams of the Desiccated'],
  [normalizeSoe13ItemKey('Cindersoul Cluster')]: ['Cindersoul Crystal'],
  [normalizeSoe13ItemKey('Soulstone of Might')]: ['Ascendancy Soulstone', 'Ascendancy Tier 1'],
  [normalizeSoe13ItemKey('Ascendancy Soulstone Tier 2')]: ['Ascendancy Tier 2'],
  [normalizeSoe13ItemKey('Ascendancy Soulstone Tier 3')]: ['Ascendancy Tier 3'],
  [normalizeSoe13ItemKey('Ascendancy Soulstone Tier 4')]: ['Ascendancy Tier 4'],
  [normalizeSoe13ItemKey('Worldstone Shard')]: ['World Stone Shard', 'World Stone Shards'],
  [normalizeSoe13ItemKey('Essence of Alacrity')]: ['Essence of Alaclarity'],
  [normalizeSoe13ItemKey('Greater Essence of Alacrity')]: ['Greater Essence of Alaclarity'],
  [normalizeSoe13ItemKey('Perfect Essence of Alacrity')]: ['Perfect Essence of Alaclarity'],
};

function fateCardAliases(name: string): readonly string[] {
  if (!SOE_13_FATE_CARD_ITEMS.some((card) => normalizeSoe13ItemKey(card) === normalizeSoe13ItemKey(name))) return [];
  const aliases = new Set<string>([`${name} Card`]);
  const withoutPossessiveApostrophes = name.replace(/([A-Za-z])'s\b/g, "$1s");
  if (withoutPossessiveApostrophes !== name) {
    aliases.add(withoutPossessiveApostrophes);
    aliases.add(`${withoutPossessiveApostrophes} Card`);
  }
  return [...aliases];
}

export function soe13ItemAliases(name: string): readonly string[] {
  return [
    ...(SOE_13_ITEM_ALIASES[normalizeSoe13ItemKey(name)] ?? []),
    ...fateCardAliases(name),
  ];
}

export function soe13ListContains(list: readonly string[], name: string): boolean {
  const normalized = normalizeSoe13ItemKey(name);
  return list.some((item) => normalizeSoe13ItemKey(item) === normalized || soe13ItemAliases(item).some((alias) => normalizeSoe13ItemKey(alias) === normalized));
}

export function fateCardInfo(name: string | null | undefined): Soe13FateCardInfo | null {
  const normalized = normalizeSoe13ItemKey(name ?? '');
  if (!normalized) return null;
  return SOE_13_FATE_CARD_INFO.find((card) => normalizeSoe13ItemKey(card.name) === normalized) ?? null;
}

export function fateCardTierKey(tier: number): string {
  return `tier-${Math.max(0, Math.floor(tier))}`;
}

export function fateCardTierLabel(tier: number): string {
  return `T${Math.max(0, Math.floor(tier))}`;
}

export function fateCardTierCount(
  counts: Partial<Record<string, number>>,
  tier: number,
): number {
  return SOE_13_FATE_CARD_INFO
    .filter((card) => card.tier === tier)
    .reduce((sum, card) => sum + Math.max(0, Math.floor(counts[card.name] ?? 0)), 0);
}

export function fateCardTrackerRows(
  counts: Partial<Record<string, number>>,
  tierVisibility: Partial<Record<string, boolean>>,
  cardVisibility: Partial<Record<string, boolean>>,
): Soe13FateCardTrackerRow[] {
  const rows: Soe13FateCardTrackerRow[] = [];
  for (const tier of SOE_13_FATE_CARD_TIERS) {
    if (!tierVisibility[fateCardTierKey(tier)]) continue;
    rows.push({
      key: `tier:${tier}`,
      label: `${fateCardTierLabel(tier)} Cards`,
      tier,
      count: fateCardTierCount(counts, tier),
      kind: 'tier',
    });
  }
  for (const card of SOE_13_FATE_CARD_INFO) {
    if (!cardVisibility[card.name]) continue;
    rows.push({
      key: `card:${card.name}`,
      label: card.name,
      tier: card.tier,
      count: Math.max(0, Math.floor(counts[card.name] ?? 0)),
      kind: 'card',
    });
  }
  return rows;
}
