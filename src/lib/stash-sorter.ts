import { DROP_TRACKER_CATEGORIES } from './drop-tracker-categories';

export type StashSorterMatchType = 'category';

export interface StashSorterRule {
  id: string;
  enabled: boolean;
  matchType: StashSorterMatchType;
  matchValue: string;
  targetTab: number;
}

export interface StashSorterLastRun {
  timestampMs: number | null;
  moved: number;
  skipped: number;
  errors: number;
  message: string;
}

export interface StashSorterCalibration {
  enabled: boolean;
  left: number;
  top: number;
  cellWidth: number;
  cellHeight: number;
}

export interface StashSorterProtectedCell {
  x: number;
  y: number;
}

export const STASH_SORTER_SHARED_TAB_COUNT = 9;
export const STASH_SORTER_GRID_COLUMNS = 10;
export const STASH_SORTER_GRID_ROWS = 8;
export const STASH_SORTER_DEFAULT_INVENTORY_ROWS = 4;
export const STASH_SORTER_MIN_SPEED = 50;
export const STASH_SORTER_MAX_SPEED = 250;
export const STASH_SORTER_DEFAULT_SPEED = 100;

export const STASH_SORTER_CATEGORIES = [
  { key: 'fate-cards', label: 'Fate Cards' },
  { key: 'essences', label: 'Essences' },
  { key: 'glyphs', label: 'Glyphs' },
  { key: 'chisels', label: 'Chisels' },
  { key: 'maps', label: 'Maps' },
  { key: 'uniques', label: 'Uniques' },
  { key: 'sets', label: 'Sets' },
  { key: 'runewords', label: 'Runewords' },
  { key: 'normal-items', label: 'Normal Base Items' },
  { key: 'magic-items', label: 'Magic Items' },
  { key: 'rare-items', label: 'Rare Items' },
  { key: 'charms', label: 'Charms' },
  { key: 'jewels', label: 'Jewels' },
  { key: 'hatred-orbs', label: 'Hatred Orbs' },
] as const;

const CATEGORY_KEYS = new Set<string>(STASH_SORTER_CATEGORIES.map((category) => category.key));

export function defaultStashSorterLastRun(): StashSorterLastRun {
  return {
    timestampMs: null,
    moved: 0,
    skipped: 0,
    errors: 0,
    message: 'No sort run has been logged yet.',
  };
}

export function defaultStashSorterBlacklist(): string[] {
  return ['Horadric Cube', 'Tome of Town Portal', 'Tome of Identify'];
}

export function defaultStashSorterCalibration(): StashSorterCalibration {
  return {
    enabled: false,
    left: 417,
    top: 219,
    cellWidth: 29,
    cellHeight: 29,
  };
}

export function defaultStashSorterProtectedCells(): StashSorterProtectedCell[] {
  return [];
}

export function createStashSorterRule(
  patch: Partial<StashSorterRule> = {},
): StashSorterRule {
  const matchType: StashSorterMatchType = 'category';
  const fallbackMatchValue = STASH_SORTER_CATEGORIES[0]?.key ?? 'fate-cards';
  const rawMatchValue = patch.matchValue ?? fallbackMatchValue;
  const matchValue = normalizeStashSorterMatchValue(
    matchType,
    rawMatchValue,
  );
  return {
    id: typeof patch.id === 'string' && patch.id ? patch.id : createStashSorterId(),
    enabled: patch.enabled ?? true,
    matchType,
    matchValue,
    targetTab: normalizeStashSorterTab(patch.targetTab),
  };
}

export function createStashSorterId(): string {
  return `sort-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}

export function normalizeStashSorterTab(value: unknown): number {
  const number = typeof value === 'number' ? value : Number(value);
  if (!Number.isFinite(number)) return 1;
  return Math.max(1, Math.min(STASH_SORTER_SHARED_TAB_COUNT, Math.floor(number)));
}

export function normalizeStashSorterSpeed(value: unknown): number {
  const number = typeof value === 'number' ? value : Number(value);
  if (!Number.isFinite(number)) return STASH_SORTER_DEFAULT_SPEED;
  return Math.max(
    STASH_SORTER_MIN_SPEED,
    Math.min(STASH_SORTER_MAX_SPEED, Math.round(number)),
  );
}

export function normalizeStashSorterMatchValue(
  _matchType: StashSorterMatchType,
  value: unknown,
): string {
  const text = String(value ?? '').trim();
  return CATEGORY_KEYS.has(text) ? text : '';
}

export function normalizeStashSorterRules(value: unknown): StashSorterRule[] {
  if (!Array.isArray(value)) return [];
  return value
    .filter((rule): rule is Partial<StashSorterRule> => !!rule && typeof rule === 'object')
    .map((rule) => createStashSorterRule(rule))
    .filter((rule) => rule.matchValue.length > 0);
}

export function normalizeStashSorterBlacklist(value: unknown): string[] {
  const source = Array.isArray(value) ? value : defaultStashSorterBlacklist();
  const seen = new Set<string>();
  const out: string[] = [];
  for (const entry of source) {
    const text = String(entry ?? '').trim();
    if (!text) continue;
    const key = text.toLowerCase();
    if (seen.has(key)) continue;
    seen.add(key);
    out.push(text.slice(0, 120));
  }
  return out.length > 0 ? out : defaultStashSorterBlacklist();
}

export function normalizeStashSorterCalibration(value: unknown): StashSorterCalibration {
  const defaults = defaultStashSorterCalibration();
  if (!value || typeof value !== 'object') return defaults;
  const input = value as Partial<StashSorterCalibration>;
  return {
    enabled: Boolean(input.enabled),
    left: normalizeCalibrationNumber(input.left, defaults.left, 0, 800),
    top: normalizeCalibrationNumber(input.top, defaults.top, 0, 600),
    cellWidth: normalizeCalibrationNumber(input.cellWidth, defaults.cellWidth, 8, 120),
    cellHeight: normalizeCalibrationNumber(input.cellHeight, defaults.cellHeight, 8, 120),
  };
}

export function normalizeStashSorterProtectedCells(value: unknown): StashSorterProtectedCell[] {
  const source = Array.isArray(value) ? value : defaultStashSorterProtectedCells();
  const seen = new Set<string>();
  const cells: StashSorterProtectedCell[] = [];
  for (const entry of source) {
    if (!entry || typeof entry !== 'object') continue;
    const maybeCell = entry as Partial<StashSorterProtectedCell>;
    const x = normalizeGridCoordinate(maybeCell.x, STASH_SORTER_GRID_COLUMNS);
    const y = normalizeGridCoordinate(maybeCell.y, STASH_SORTER_GRID_ROWS);
    if (x === null || y === null) continue;
    const key = stashSorterCellKey(x, y);
    if (seen.has(key)) continue;
    seen.add(key);
    cells.push({ x, y });
  }
  return cells;
}

export function stashSorterCellKey(x: number, y: number): string {
  return `${Math.floor(x)},${Math.floor(y)}`;
}

export function normalizeStashSorterLastRun(value: unknown): StashSorterLastRun {
  if (!value || typeof value !== 'object') return defaultStashSorterLastRun();
  const run = value as Partial<StashSorterLastRun>;
  return {
    timestampMs:
      typeof run.timestampMs === 'number' && Number.isFinite(run.timestampMs)
        ? run.timestampMs
        : null,
    moved: normalizeCount(run.moved),
    skipped: normalizeCount(run.skipped),
    errors: normalizeCount(run.errors),
    message:
      typeof run.message === 'string' && run.message.trim()
        ? run.message.trim().slice(0, 240)
        : defaultStashSorterLastRun().message,
  };
}

export function stashSorterCategoryLabel(value: string): string {
  return STASH_SORTER_CATEGORIES.find((category) => category.key === value)?.label ?? value;
}

function normalizeCount(value: unknown): number {
  const number = typeof value === 'number' ? value : Number(value);
  return Number.isFinite(number) ? Math.max(0, Math.floor(number)) : 0;
}

function normalizeCalibrationNumber(
  value: unknown,
  fallback: number,
  min: number,
  max: number,
): number {
  const number = typeof value === 'number' ? value : Number(value);
  if (!Number.isFinite(number)) return fallback;
  return Math.max(min, Math.min(max, Math.round(number * 100) / 100));
}

function normalizeGridCoordinate(value: unknown, limit: number): number | null {
  const number = typeof value === 'number' ? value : Number(value);
  if (!Number.isFinite(number)) return null;
  const int = Math.floor(number);
  if (int < 0 || int >= limit) return null;
  return int;
}

export const STASH_SORTER_DROP_TRACKER_CATEGORY_KEYS = DROP_TRACKER_CATEGORIES.map(
  (category) => category.key,
);
