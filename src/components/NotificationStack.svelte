<script lang="ts">
  import Notification from './Notification.svelte';
  
  interface NotificationFilter {
    color?: string | null;
    sound?: number | null;
    display_stats: boolean;
    matched_stat_lines?: number[] | null;
  }

  interface ItemDrop {
    unit_id: number;
    class: number;
    quality: string;
    name: string;
    base_name: string;
    stats: string;
    is_ethereal: boolean;
    is_identified: boolean;
    is_hellforged?: boolean;
    isHellforged?: boolean;
    is_new_grail?: boolean;
    gsf_needed_by?: string[];
    filter?: NotificationFilter | null;
    exiting?: boolean;
  }

  interface Props {
    items: ItemDrop[];
    /** Anchor x position as percentage of overlay width (0-100). */
    x?: number;
    /** Anchor y position as percentage of overlay height (0-100). */
    y?: number;
    maxVisible?: number;
    fontSize?: number;
    opacity?: number;
    width?: number;
    height?: number;
    compactName?: boolean;
    stackDirection?: string;
  }

  let {
    items,
    x = 1,
    y = 1,
    maxVisible = 10,
    fontSize = 14,
    opacity = 0.9,
    width = 320,
    height = 420,
    compactName = false,
    stackDirection = 'up',
  }: Props = $props();

  const newestFirst = $derived(stackDirection !== 'down');
  const visibleItems = $derived(
    newestFirst
      ? items.slice(0, maxVisible)
      : items.slice(0, maxVisible).toReversed(),
  );
</script>

<div
  class="notification-stack"
  class:stack-down={!newestFirst}
  style="top: {y}%; left: {x}%; width: {width}px; max-height: {height}px;"
>
  {#each visibleItems as item (item.unit_id)}
    <Notification
      {item}
      exiting={item.exiting ?? false}
      {fontSize}
      {opacity}
      {compactName}
    />
  {/each}
</div>

<style>
  .notification-stack {
    position: fixed;
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: var(--space-2);
    overflow: hidden;
    pointer-events: none;
    z-index: 9999;
  }

  .notification-stack.stack-down {
    flex-direction: column-reverse;
  }
  
  .notification-stack > :global(*) {
    pointer-events: auto;
  }
</style>
