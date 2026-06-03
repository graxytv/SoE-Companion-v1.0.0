const fs = require('fs');

const unique = JSON.parse(fs.readFileSync('public/soe-wiki-cache/data/Uniques.json', 'utf8'));
const old = fs.readFileSync('src/lib/holy-grail-static.ts', 'utf8');

function oldArray(name) {
  const match = old.match(new RegExp(`export const ${name} = \\[([\\s\\S]*?)\\] as const;`));
  if (!match) return [];
  return [...match[1].matchAll(/"([^"]+)"/g)].map((entry) => entry[1]);
}

function uniqueSorted(items) {
  return [...new Set(items.filter(Boolean))].sort((a, b) => a.localeCompare(b));
}

function emit(name, items) {
  let out = `export const ${name} = [\n`;
  for (let i = 0; i < items.length; i += 4) {
    out += `  ${items.slice(i, i + 4).map((item) => JSON.stringify(item)).join(', ')}${i + 4 < items.length ? ',' : ''}\n`;
  }
  return `${out}] as const;\n\n`;
}

const standardUniques = uniqueSorted(
  unique.filter((item) => !item.hellforged).map((item) => item.displayName),
);
const hellforgedUniques = uniqueSorted(
  unique.filter((item) => item.hellforged).map((item) => item.displayName),
);
const setItems = [...new Set(oldArray('STATIC_SET_ITEMS'))];

const header = [
  '// SoE Companion item lists for Holy Grail tracking',
  '// Refreshed from The Archivist SoE wiki Uniques.json on 2026-05-14.',
  '// STATIC_SU_ITEMS contains non-Hellforged unique items.',
  '// STATIC_SSU_ITEMS contains Hellforged unique variants.',
  '// STATIC_SET_ITEMS contains set item piece names from the existing SoE data source.',
  '',
].join('\n');

fs.writeFileSync(
  'src/lib/holy-grail-static.ts',
  header +
    emit('STATIC_SU_ITEMS', standardUniques) +
    emit('STATIC_SSU_ITEMS', hellforgedUniques) +
    'export const STATIC_SSSU_ITEMS = [] as const;\n\n' +
    emit('STATIC_SET_ITEMS', setItems) +
    'export const STATIC_RELIC_ITEMS = [] as const;\n',
);

console.log(JSON.stringify({
  standardUniques: standardUniques.length,
  hellforgedUniques: hellforgedUniques.length,
  setItems: setItems.length,
  totalUniques: standardUniques.length + hellforgedUniques.length,
}));
