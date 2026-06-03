import { MATERIAL_NAMES, materialNameFromDrop } from './item-sounds';
import { normalizeHolyGrailKey } from './holy-grail';
import type { DropTrackerItemLike } from './drop-tracker-categories';

export const MATERIAL_TRACKER_NAMES = MATERIAL_NAMES;
export type MaterialTrackerName = (typeof MATERIAL_TRACKER_NAMES)[number];
export type MaterialTrackerCounts = Partial<Record<MaterialTrackerName, number>>;
export type MaterialTrackerVisibility = Record<MaterialTrackerName, boolean>;

export function materialTrackerKey(name: string): string {
  return normalizeHolyGrailKey(name);
}

export function materialTrackerNameFromDrop(item: DropTrackerItemLike): MaterialTrackerName | null {
  const material = materialNameFromDrop(item);
  return MATERIAL_TRACKER_NAMES.includes(material as MaterialTrackerName)
    ? material as MaterialTrackerName
    : null;
}

export function defaultMaterialTrackerCounts(): MaterialTrackerCounts {
  return {};
}

export function defaultMaterialTrackerVisibility(): MaterialTrackerVisibility {
  return Object.fromEntries(
    MATERIAL_TRACKER_NAMES.map((name) => [name, true]),
  ) as MaterialTrackerVisibility;
}

export function normalizeMaterialTrackerCounts(
  value: Partial<Record<string, number>> | undefined | null,
): MaterialTrackerCounts {
  const out: MaterialTrackerCounts = {};
  for (const material of MATERIAL_TRACKER_NAMES) {
    const raw = value?.[material];
    if (typeof raw === 'number' && Number.isFinite(raw) && raw > 0) {
      out[material] = Math.floor(raw);
    }
  }
  return out;
}

export function normalizeMaterialTrackerVisibility(
  value: Partial<Record<string, boolean>> | undefined | null,
): MaterialTrackerVisibility {
  const defaults = defaultMaterialTrackerVisibility();
  return Object.fromEntries(
    MATERIAL_TRACKER_NAMES.map((material) => [material, value?.[material] ?? defaults[material]]),
  ) as MaterialTrackerVisibility;
}
