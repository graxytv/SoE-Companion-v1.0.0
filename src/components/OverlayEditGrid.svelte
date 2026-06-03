<script module lang="ts">
  export interface OverlayEditMarker {
    id: string;
    label: string;
    x: number;
    y: number;
    width: number;
    height: number;
    unit: 'percent' | 'px';
  }
</script>

<script lang="ts">
  interface Props {
    /** Anchor x position as percentage of overlay width (0-100). */
    x?: number;
    /** Anchor y position as percentage of overlay height (0-100). */
    y?: number;
    onchange?: (x: number, y: number) => void;
    markers?: OverlayEditMarker[];
    onmarkerchange?: (id: string, x: number, y: number) => void;
  }

  let { x = 0, y = 0, onchange, markers = [], onmarkerchange }: Props = $props();

  const MARKER_WIDTH_PX = 300;
  const MARKER_HEIGHT_PX = 80;

  let dragging = $state<null | {
    id: string;
    unit: 'percent' | 'px';
    width: number;
    height: number;
  }>(null);
  let dragOffsetX = 0;
  let dragOffsetY = 0;
  const editMarkers = $derived(
    markers.length > 0
      ? markers
      : [{
          id: 'notifications',
          label: 'Notifications appear here - drag to move',
          x,
          y,
          width: MARKER_WIDTH_PX,
          height: MARKER_HEIGHT_PX,
          unit: 'percent' as const,
        }],
  );

  function clamp(v: number, lo: number, hi: number): number {
    return Math.min(Math.max(v, lo), hi);
  }

  function markerStyle(marker: OverlayEditMarker): string {
    const top = marker.unit === 'percent' ? `${marker.y}%` : `${marker.y}px`;
    const left = marker.unit === 'percent' ? `${marker.x}%` : `${marker.x}px`;
    return `top: ${top}; left: ${left}; width: ${marker.width}px; height: ${marker.height}px;`;
  }

  function onMarkerMouseDown(e: MouseEvent, marker: OverlayEditMarker) {
    e.preventDefault();
    e.stopPropagation();
    const rect = (e.currentTarget as HTMLElement).getBoundingClientRect();
    dragOffsetX = e.clientX - rect.left;
    dragOffsetY = e.clientY - rect.top;
    dragging = {
      id: marker.id,
      unit: marker.unit,
      width: marker.width,
      height: marker.height,
    };
  }

  function onWindowMouseMove(e: MouseEvent) {
    if (!dragging) return;
    const w = window.innerWidth;
    const h = window.innerHeight;
    if (w === 0 || h === 0) return;

    const pxX = e.clientX - dragOffsetX;
    const pxY = e.clientY - dragOffsetY;

    if (dragging.unit === 'px') {
      const nx = clamp(pxX, 0, Math.max(0, w - dragging.width));
      const ny = clamp(pxY, 0, Math.max(0, h - dragging.height));
      onmarkerchange?.(dragging.id, nx, ny);
      return;
    }

    const maxXPct = 100 - (dragging.width / w) * 100;
    const maxYPct = 100 - (dragging.height / h) * 100;
    const nx = clamp((pxX / w) * 100, 0, Math.max(0, maxXPct));
    const ny = clamp((pxY / h) * 100, 0, Math.max(0, maxYPct));

    if (dragging.id === 'notifications') {
      onchange?.(nx, ny);
    }
    onmarkerchange?.(dragging.id, nx, ny);
  }

  function onWindowMouseUp() {
    dragging = null;
  }
</script>

<svelte:window onmousemove={onWindowMouseMove} onmouseup={onWindowMouseUp} />

<div class="edit-grid" class:dragging={!!dragging}>
  {#each editMarkers as marker (marker.id)}
    <div
      class="marker"
      style={markerStyle(marker)}
      onmousedown={(event) => onMarkerMouseDown(event, marker)}
      role="button"
      tabindex="-1"
      aria-label="Drag to reposition {marker.label}"
    >
      <span class="marker-label">{marker.label}</span>
    </div>
  {/each}
</div>

<style>
  .edit-grid {
    position: fixed;
    inset: 0;
    pointer-events: auto;
    background-image:
      linear-gradient(to right, color-mix(in srgb, var(--accent-primary, #6aa3ff) 22%, transparent) 1px, transparent 1px),
      linear-gradient(to bottom, color-mix(in srgb, var(--accent-primary, #6aa3ff) 22%, transparent) 1px, transparent 1px),
      linear-gradient(to right, color-mix(in srgb, var(--accent-primary, #6aa3ff) 36%, transparent) 1px, transparent 1px),
      linear-gradient(to bottom, color-mix(in srgb, var(--accent-primary, #6aa3ff) 36%, transparent) 1px, transparent 1px);
    background-size:
      25px 25px,
      25px 25px,
      100px 100px,
      100px 100px;
    background-color: rgba(0, 0, 0, 0.25);
    z-index: 10000;
    cursor: crosshair;
  }

  .edit-grid.dragging {
    cursor: grabbing;
  }

  .marker {
    position: absolute;
    box-sizing: border-box;
    border: 2px dashed var(--accent-primary, #6aa3ff);
    background: color-mix(in srgb, var(--accent-primary, #6aa3ff) 16%, transparent);
    border-radius: 4px;
    display: flex;
    align-items: center;
    justify-content: center;
    color: var(--text-primary, #e0e0e0);
    font-family: var(--font-mono, monospace);
    font-size: 13px;
    font-weight: 700;
    text-align: center;
    padding: var(--space-2, 8px);
    cursor: grab;
    user-select: none;
    pointer-events: auto;
    transition: background 120ms ease;
  }

  .marker:hover {
    background: color-mix(in srgb, var(--accent-primary, #6aa3ff) 26%, transparent);
  }

  .edit-grid.dragging .marker {
    cursor: grabbing;
    background: color-mix(in srgb, var(--accent-primary, #6aa3ff) 34%, transparent);
  }

  .marker-label {
    pointer-events: none;
  }
</style>
