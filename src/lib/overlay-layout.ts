export const OVERLAY_LAYOUT_WINDOWS = [
  { kind: 'notifications', label: 'Notifications', windowLabel: 'notification-card-overlay' },
  { kind: 'drops', label: 'Drops Tracker', windowLabel: 'drops-card-overlay' },
  { kind: 'total', label: 'Total Drops', windowLabel: 'total-card-overlay' },
  { kind: 'grail', label: 'Grail Progress', windowLabel: 'grail-card-overlay' },
  { kind: 'materials', label: 'Mats Tracker', windowLabel: 'mats-card-overlay' },
  { kind: 'runes', label: 'Rune Tracker', windowLabel: 'runes-card-overlay' },
  { kind: 'achievements', label: 'Achievement Progress', windowLabel: 'achievement-card-overlay' },
  { kind: 'achievement-popup', label: 'Achievement Popup', windowLabel: 'achievement-popup-overlay' },
  { kind: 'kills', label: 'Monster Kills', windowLabel: 'kills-card-overlay' },
  { kind: 'muling', label: 'Muling Indicator', windowLabel: 'muling-card-overlay' },
] as const;

export type OverlayLayoutWindowDefinition = typeof OVERLAY_LAYOUT_WINDOWS[number];
export type OverlayLayoutKind = OverlayLayoutWindowDefinition['kind'];
export type OverlayLayoutWindowLabel = OverlayLayoutWindowDefinition['windowLabel'];

export const OVERLAY_LAYOUT_WINDOW_LABELS = OVERLAY_LAYOUT_WINDOWS.map((window) => window.windowLabel);

export function overlayLayoutKindFromWindowLabel(label: string): OverlayLayoutKind {
  return OVERLAY_LAYOUT_WINDOWS.find((window) => window.windowLabel === label)?.kind ?? 'runes';
}

export function isOverlayLayoutWindowLabel(label: string): label is OverlayLayoutWindowLabel {
  return OVERLAY_LAYOUT_WINDOW_LABELS.includes(label as OverlayLayoutWindowLabel);
}
