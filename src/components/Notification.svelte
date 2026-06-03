<script lang="ts">
  import { canonicalTrackedItemName, inferHolyGrailCategory } from '../lib/holy-grail';

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
    isNewGrail?: boolean;
    gsf_needed_by?: string[];
    gsfNeededBy?: string[];
    filter?: NotificationFilter | null;
  }

  interface Props {
    item: ItemDrop;
    exiting?: boolean;
    fontSize?: number;
    opacity?: number;
    compactName?: boolean;
  }

  let {
    item,
    exiting = false,
    fontSize = 14,
    opacity = 0.9,
    compactName = false,
  }: Props = $props();

  const qualityColors: Record<string, string> = {
    'Unique': 'var(--quality-unique)',
    'Set': 'var(--quality-set)',
    'Rare': 'var(--quality-rare)',
    'Magic': 'var(--quality-magic)',
    'Crafted': 'var(--quality-crafted)',
    'Honorific': 'var(--quality-crafted)',
    'Superior': 'var(--quality-superior)',
    'Inferior': 'var(--quality-normal)',
    'Normal': 'var(--quality-normal)'
  };

  // Palette for rule-level `color` flag — takes precedence over quality color.
  const notifyColors: Record<string, string> = {
    white:  'var(--notify-white)',
    red:    'var(--notify-red)',
    lime:   'var(--notify-lime)',
    blue:   'var(--notify-blue)',
    gold:   'var(--notify-gold)',
    grey:   'var(--notify-grey)',
    black:  'var(--notify-black)',
    pink:   'var(--notify-pink)',
    orange: 'var(--notify-orange)',
    yellow: 'var(--notify-yellow)',
    green:  'var(--notify-green)',
    purple: 'var(--notify-purple)',
  };

  const nameColor = $derived(
    (item.filter?.color ? notifyColors[item.filter.color] : undefined)
      ?? qualityColors[item.quality]
      ?? 'var(--text-muted)'
  );

  // Items that get the two-line "name + base" treatment.
  const isLargeDrop = $derived(item.quality === 'Set' || item.quality === 'Unique');

  const showStats = $derived(item.filter?.display_stats === true && item.stats.length > 0);
  const statLines = $derived(showStats ? item.stats.split('\n') : []);
  const matchedLineIdxSet = $derived(new Set(item.filter?.matched_stat_lines ?? []));

  // Compact-name yields a single line, but the stat-flag exception keeps
  // the full two-line header so the drop reads cleanly above its stats.
  const showTwoLines = $derived(isLargeDrop && (!compactName || showStats));

  const grailCategory = $derived(inferHolyGrailCategory(item));
  const primary = $derived(canonicalTrackedItemName(showTwoLines ? item.name : item.base_name, grailCategory));
  const secondary = $derived(showTwoLines ? canonicalTrackedItemName(item.base_name, grailCategory) : null);
  const isNewGrail = $derived(item.is_new_grail === true || item.isNewGrail === true);
  const gsfNeededBy = $derived(item.gsf_needed_by ?? item.gsfNeededBy ?? []);
  const gsfNeededText = $derived(
    gsfNeededBy.length > 4
      ? `${gsfNeededBy.slice(0, 4).join(', ')} +${gsfNeededBy.length - 4} more`
      : gsfNeededBy.join(', '),
  );

  const hasBadges = $derived(item.is_ethereal || !item.is_identified);
</script>

<div
  class="notification"
  class:exiting
  style:font-size="{fontSize}px"
  style:background-color="rgba(var(--notification-bg-rgb, 0, 0, 0), {opacity})"
>
  {#if isNewGrail}
    <div class="new-grail-item">**New Item!**</div>
  {/if}
  <div class="item-name" style:color={nameColor}>
    {primary}{#if hasBadges}
      <span class="badges">
        {#if item.is_ethereal}<span class="ethereal">ETH</span>{/if}
        {#if !item.is_identified}<span class="unid">[UNID]</span>{/if}
      </span>
    {/if}
  </div>
  {#if secondary}
    <div class="item-base">{secondary}</div>
  {/if}
  {#if gsfNeededBy.length > 0}
    <div class="gsf-needed">Needed by: {gsfNeededText}</div>
  {/if}
  {#if showStats}
    <div class="item-stats">
      {#each statLines as line, i}
        <div class="stat-line" class:matched={matchedLineIdxSet.has(i)}>{line}</div>
      {/each}
    </div>
  {/if}
</div>

<style>
  .notification {
    width: 100%;
    max-width: 22em;
    box-sizing: border-box;
    font-family: var(--font-mono);
    padding: 0.45em 0.65em;
    border: 1px solid var(--overlay-panel-border, rgba(255, 200, 80, 0.35));
    border-radius: 4px;
    box-shadow: 0 8px 24px rgba(0, 0, 0, 0.36);
  }

  .new-grail-item {
    margin-bottom: 0.18em;
    font-weight: 900;
    line-height: 1.1;
    text-transform: none;
    letter-spacing: 0.02em;
    color: #ffff66;
    text-shadow: 0 0 8px rgba(255, 255, 255, 0.28);
  }

  @supports ((-webkit-background-clip: text) or (background-clip: text)) {
    .new-grail-item {
    background: linear-gradient(90deg, #ff4d4d, #ffb84d, #ffff66, #66ff99, #66ccff, #a366ff, #ff66cc, #ff4d4d);
    background-size: 300% 100%;
    -webkit-background-clip: text;
    background-clip: text;
    color: transparent;
      -webkit-text-fill-color: transparent;
    animation: grail-rainbow 1.7s linear infinite;
    }
  }

  @keyframes grail-rainbow {
    from { background-position: 0% 50%; }
    to { background-position: 300% 50%; }
  }

  .item-name {
    font-weight: 600;
    line-height: 1.3;
  }

  .item-base {
    margin-top: 0.22em;
    font-size: 0.85em;
    color: var(--notif-base);
    line-height: 1.3;
  }

  .gsf-needed {
    margin-top: 0.22em;
    color: var(--notify-gold);
    font-size: 0.86em;
    font-weight: 700;
    line-height: 1.25;
    text-shadow: 0 0 6px rgba(240, 205, 140, 0.28);
  }

  .badges {
    margin-left: 0.45em;
    font-size: 0.85em;
    font-weight: 400;
  }

  .ethereal {
    color: var(--quality-ethereal);
  }

  .unid {
    color: var(--notif-muted);
    margin-left: 0.22em;
  }

  .item-stats {
    margin-top: 0.22em;
    font-size: 0.85em;
    line-height: 1.3;
    overflow-wrap: anywhere;
  }

  .stat-line {
    color: var(--notif-stat);
  }

  .stat-line.matched {
    color: var(--notif-stat-matched);
  }
</style>
