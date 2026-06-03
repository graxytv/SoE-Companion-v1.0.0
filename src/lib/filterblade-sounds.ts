export interface FilterBladeSoundClip {
  fileName: string;
  label: string;
}

export interface FilterBladeSoundPack {
  name: string;
  label: string;
  clips: FilterBladeSoundClip[];
}

export const FILTERBLADE_SOUND_BASE_URL = 'https://www.filterblade.xyz/assets/communitySounds';

function encodeUrlSegment(segment: string): string {
  return segment
    .split('')
    .map((char) => {
      if (/^[A-Za-z0-9_.-]$/.test(char)) return char;
      return Array.from(new TextEncoder().encode(char))
        .map((byte) => `%${byte.toString(16).toUpperCase().padStart(2, '0')}`)
        .join('');
    })
    .join('');
}

export function filterBladeSoundUrl(pack: string, fileName: string): string {
  return `${FILTERBLADE_SOUND_BASE_URL}/${encodeUrlSegment(pack)}/${encodeUrlSegment(fileName)}`;
}

const STANDARD_CLIPS: FilterBladeSoundClip[] = [
  { fileName: '1maybevaluable.mp3', label: 'May be valuable' },
  { fileName: '2currency.mp3', label: 'Currency' },
  { fileName: '3uniques.mp3', label: 'Uniques' },
  { fileName: '4maps.mp3', label: 'Maps' },
  { fileName: '5highmaps.mp3', label: 'High maps' },
  { fileName: '6veryvaluable.mp3', label: 'Very valuable' },
  { fileName: '7chancing.mp3', label: 'Chancing' },
  { fileName: '12leveling.mp3', label: 'Leveling' },
  { fileName: 'placeholder.mp3', label: 'Placeholder' },
];

const MAVEN_CLIPS: FilterBladeSoundClip[] = [
  ['ah1.ogg', 'Ah 1'], ['ah2.ogg', 'Ah 2'], ['allthespecialones.ogg', 'All the special ones'],
  ['amazing.ogg', 'Amazing'], ['atlasdelights.ogg', 'Atlas delights'], ['beautiful.ogg', 'Beautiful 1'],
  ['beautiful1.ogg', 'Beautiful 2'], ['beautiful2.ogg', 'Beautiful 3'], ['captivating.ogg', 'Captivating'],
  ['chaos1.ogg', 'Chaos 1'], ['chaos2.ogg', 'Chaos 2'], ['chaos3.ogg', 'Chaos 3'],
  ['colleciton.ogg', 'Collection'], ['collectanother.ogg', 'Collect another'], ['collectiongrows.ogg', 'Collection grows'],
  ['delicious.ogg', 'Delicious'], ['divine1.ogg', 'Divine 1'], ['divine2.ogg', 'Divine 2'],
  ['divine3.ogg', 'Divine 3'], ['entertaining.ogg', 'Entertaining'], ['entertainingdirty.ogg', 'Entertaining dirty'],
  ['entertainmemoan.ogg', 'Entertain me moan'], ['ex.ogg', 'Ex'], ['excitement.ogg', 'Excitement'],
  ['exoticgifts.ogg', 'Exotic gifts'], ['exquisite.ogg', 'Exquisite'], ['fascinating.ogg', 'Fascinating'],
  ['favoriteoutcome.ogg', 'Favorite outcome 1'], ['favoriteoutcome1.ogg', 'Favorite outcome 2'],
  ['ferocious.ogg', 'Ferocious'], ['flashofpleasure.ogg', 'Flash of pleasure'], ['flashy.ogg', 'Flashy 1'],
  ['flashy1.ogg', 'Flashy 2'], ['giftofstrength.ogg', 'Gift of strength'], ['glorious1.ogg', 'Glorious 1'],
  ['glorious2.ogg', 'Glorious 2'], ['gloriousspectacle1.ogg', 'Glorious spectacle 1'],
  ['gloriousspectacle2.ogg', 'Glorious spectacle 2'], ['good1.ogg', 'Good 1'], ['good2.ogg', 'Good 2'],
  ['good3.ogg', 'Good 3'], ['happy1.ogg', 'Happy'], ['hiding1.ogg', 'Hiding'], ['hilarious.ogg', 'Hilarious'],
  ['ineedmore.ogg', 'I need more'], ['invitation.ogg', 'Invitation'], ['iwantmore.ogg', 'I want more'],
  ['joy.ogg', 'Joy'], ['joygift.ogg', 'Joy gift'], ['magnificent1.ogg', 'Magnificent 1'],
  ['magnificent2.ogg', 'Magnificent 2'], ['massmurder.ogg', 'Mass murder'], ['mine1.ogg', 'Mine'],
  ['minemine1.ogg', 'Mine mine'], ['minenow.ogg', 'Mine now'], ['moan1.ogg', 'Moan 1'], ['moan2.ogg', 'Moan 2'],
  ['moan3.ogg', 'Moan 3'], ['moan4.ogg', 'Moan 4'], ['moan5.ogg', 'Moan 5'], ['moan6.ogg', 'Moan 6'],
  ['moan7.ogg', 'Moan 7'], ['moan8.ogg', 'Moan 8'], ['moan9.ogg', 'Moan 9'], ['morepower.ogg', 'More power'],
  ['mysteriouspresence.ogg', 'Mysterious presence'], ['nevertired.ogg', 'Never tired'], ['newtoy.ogg', 'New toy'],
  ['notgood.ogg', 'Not good'], ['oh1.ogg', 'Oh 1'], ['oh2.ogg', 'Oh 2'], ['onemore1.ogg', 'One more 1'],
  ['onemore2.ogg', 'One more 2'], ['partofcollection.ogg', 'Part of collection'], ['plaything.ogg', 'Plaything'],
  ['powerisfun.ogg', 'Power is fun'], ['raresight.ogg', 'Rare sight'], ['raretreat.ogg', 'Rare treat'],
  ['rush1.ogg', 'Rush 1'], ['rush2.ogg', 'Rush 2'], ['satisfactory.ogg', 'Satisfactory'],
  ['savoureddelight.ogg', 'Savoured delight'], ['song.ogg', 'Song'], ['special1.ogg', 'Special 1'],
  ['special2.ogg', 'Special 2'], ['spectacular1.ogg', 'Spectacular 1'], ['spectacular2.ogg', 'Spectacular 2'],
  ['splashandsplatter.ogg', 'Splash and splatter'], ['suchpleasure.ogg', 'Such pleasure'],
  ['surprising.ogg', 'Surprising'], ['sweetsweet1.ogg', 'Sweet sweet 1'], ['sweetsweet2.ogg', 'Sweet sweet 2'],
  ['therewillbedeath.ogg', 'There will be death'], ['uncommonmight.ogg', 'Uncommon might'],
  ['unexpectednotunwelcome.ogg', 'Unexpected not unwelcome'], ['vibrant.ogg', 'Vibrant'],
  ['victoriousplaything.ogg', 'Victorious plaything'], ['victorygift.ogg', 'Victory gift'],
  ['wintribute.ogg', 'Win tribute'], ['withoutcompare1.ogg', 'Without compare 1'],
  ['withoutcompare2.ogg', 'Without compare 2'], ['worthit1.ogg', 'Worth it 1'], ['worthit2.ogg', 'Worth it 2'],
  ['yes1.ogg', 'Yes 1'], ['yes2.ogg', 'Yes 2'], ['yes3.ogg', 'Yes 3'], ['yesmine.ogg', 'Yes mine'],
].map(([fileName, label]) => ({ fileName, label }));

const STANDARD_PACKS = [
  'Asuzara',
  'BexBloopers',
  'Brittleknee',
  'Chistor',
  'GhazzyTV',
  'Golaya',
  'Gollum (Zizaran)',
  'Holly',
  'Kermit (Zizaran)',
  'Lolcohol',
  'Mathil',
  'Mathil-vulgarity',
  'SlipperyJim',
  'Stefan Gold',
  'Gilbertamie',
  'Zizaran',
];

export const FILTERBLADE_SOUND_PACKS: FilterBladeSoundPack[] = [
  { name: 'Maven', label: 'Maven', clips: MAVEN_CLIPS },
  { name: 'Veskara', label: 'Default PoE Sounds', clips: STANDARD_CLIPS },
  ...STANDARD_PACKS.map((name) => ({ name, label: name, clips: STANDARD_CLIPS })),
  { name: 'ItsYoji', label: 'ItsYoji', clips: [...STANDARD_CLIPS, { fileName: 'Slurp.mp3', label: 'Slurp' }] },
];
