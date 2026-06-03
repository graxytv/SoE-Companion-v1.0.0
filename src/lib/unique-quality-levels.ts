import uniquesJson from '../../public/soe-wiki-cache/data/Uniques.json';
import { normalizeHolyGrailKey } from './holy-grail';

interface WikiUniqueItem {
  displayName?: string | null;
  index?: string | null;
  level?: number | string | null;
}

const qualityLevels = new Map<string, number>();

for (const item of uniquesJson as WikiUniqueItem[]) {
  const level = Number(item.level);
  if (!Number.isFinite(level) || level <= 0) continue;
  for (const name of [item.displayName, item.index]) {
    const key = normalizeHolyGrailKey(String(name ?? ''));
    if (key) qualityLevels.set(key, Math.floor(level));
  }
}

export function uniqueQualityLevel(name: string | undefined | null): number | null {
  const key = normalizeHolyGrailKey(String(name ?? ''));
  if (!key) return null;
  return qualityLevels.get(key) ?? null;
}

export function uniqueQualityLabel(name: string | undefined | null): string {
  const level = uniqueQualityLevel(name);
  return level == null ? 'QL -' : `QL ${level}`;
}
