<script lang="ts">
  import { onMount } from 'svelte';
  import { invoke } from '@tauri-apps/api/core';
  import { itemsDictionaryStore, settingsStore } from '../stores';
  import { SetupWizard } from '../components';
  import { buildHolyGrailItems, holyGrailProgress } from '../lib/holy-grail';
  import { evaluateAchievements, unlockedAchievementCount } from '../lib/achievements';
  import soeLogoUrl from '../assets/home/soe-logo.png';
  import hellUnleashed01 from '../assets/home/slideshow/hell-unleashed-01.webp';
  import hellUnleashed02 from '../assets/home/slideshow/hell-unleashed-02.png';
  import hellUnleashed03 from '../assets/home/slideshow/hell-unleashed-03.jpg';

  interface Props {
    gameStatus: 'unknown' | 'ingame' | 'menu';
    scannerStatus: 'stopped' | 'starting' | 'running' | 'stopping' | 'error';
    onNavigate?: (tab: string) => void;
  }

  let { gameStatus, scannerStatus, onNavigate }: Props = $props();
  let launchStatus = $state('');
  let showChangelog = $state(false);
  let showSetupWizard = $state(false);
  let changelogHtml = $state('');
  let currentHeroSlide = $state(0);
  const heroSlides = [hellUnleashed01, hellUnleashed02, hellUnleashed03];
  let grailItems = $derived(buildHolyGrailItems(itemsDictionaryStore.dict));
  let grail = $derived(holyGrailProgress(grailItems, settingsStore.settings.holyGrailFound));
  let achievementProgress = $derived(evaluateAchievements({
    stats: settingsStore.settings.achievementStats,
    holyGrailFound: settingsStore.settings.holyGrailFound,
    holyGrailItems: grailItems,
    runeTrackerCounts: settingsStore.settings.runeTrackerCounts,
  }));
  let achievementUnlocked = $derived(unlockedAchievementCount(achievementProgress));
  let totalKills = $derived(settingsStore.settings.achievementStats.totalKills ?? 0);

  async function play(): Promise<void> {
    launchStatus = 'Opening SoE launcher...';
    try {
      await invoke('launch_soe_launcher', { path: settingsStore.settings.soeLauncherPath });
      launchStatus = 'SoE launcher opened.';
    } catch (err) {
      const message = String(err);
      const selectedPath = window.prompt(
        `${message}\n\nEnter the full path to pd2-soe-launcher.exe:`,
        settingsStore.settings.soeLauncherPath ?? 'C:\\Program Files\\PD2 Sanctuary of Exile\\pd2-soe-launcher.exe',
      );
      const path = selectedPath?.trim();
      if (!path) {
        launchStatus = message;
        return;
      }
      settingsStore.setSoeLauncherPath(path);
      try {
        await invoke('launch_soe_launcher', { path });
        launchStatus = 'SoE launcher path saved and opened.';
      } catch (retryError) {
        launchStatus = `Could not open selected launcher: ${retryError}`;
      }
    }
  }

  async function openChangelog(): Promise<void> {
    try {
      const md: string = await invoke('get_changelog');
      changelogHtml = renderChangelog(md);
      showChangelog = true;
    } catch (err) {
      launchStatus = `Could not load changelog: ${err}`;
    }
  }

  function renderChangelog(md: string): string {
    const lines = md.split('\n');
    const out: string[] = [];
    let skipSection = false;
    let inVersion = false;

    for (const line of lines) {
      if (line.startsWith('# ') && !line.startsWith('## ')) continue;

      if (line.startsWith('## ')) {
        if (inVersion) out.push('</section>');
        inVersion = true;
        skipSection = false;
        out.push('<section class="cl-version">');
        out.push(`<h2>${line.slice(3)}</h2>`);
        continue;
      }

      if (line.startsWith('### ')) {
        const heading = line.slice(4);
        skipSection = heading === 'Other';
        if (!skipSection) out.push(`<h3>${heading}</h3>`);
        continue;
      }

      if (skipSection) continue;

      if (line.startsWith('- ')) {
        out.push(`<div class="cl-entry">${formatEntry(line.slice(2))}</div>`);
      }
    }
    if (inVersion) out.push('</section>');
    return out.join('\n');
  }

  function formatEntry(text: string): string {
    text = text.replace(/^(?:Feat|Fix|Refactor|Perf|Chore|Docs|Style|Build|Ci|Test)(\([^)]+\)):\s*/i, (_, scope) => {
      return `<span class="cl-scope">${scope.slice(1, -1)}</span>`;
    });
    text = text.replace(/\(([0-9a-f]{7})\)$/, '<span class="cl-hash">$1</span>');
    return text;
  }

  async function openSetupWizard(): Promise<void> {
    showSetupWizard = true;
  }

  const emberStyles = Array.from({ length: 24 }, (_, i) => {
    const left = (3 + i * 17) % 96;
    const size = 2 + (i % 4);
    const duration = 5.5 + (i % 7) * 0.55;
    const delay = (i % 12) * -0.62;
    const drift = i % 3 === 0 ? -34 : i % 3 === 1 ? 24 : 7;
    return `--left:${left}%;--size:${size}px;--dur:${duration}s;--delay:${delay}s;--drift:${drift}px;`;
  });

  onMount(() => {
    if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) return;
    const timer = window.setInterval(() => {
      currentHeroSlide = (currentHeroSlide + 1) % heroSlides.length;
    }, 7200);
    return () => window.clearInterval(timer);
  });
</script>

<section class="home-tab">
  <div class="home-hero">
    <div class="home-slideshow" aria-hidden="true">
      {#each heroSlides as slide, index}
        <div
          class:active={index === currentHeroSlide}
          class="home-slide"
          style={`background-image: url("${slide}")`}
        ></div>
      {/each}
    </div>
    <div class="home-embers" aria-hidden="true">
      {#each emberStyles as emberStyle}
        <span style={emberStyle}></span>
      {/each}
    </div>
    <div class="hero-content">
      <img class="soe-logo" src={soeLogoUrl} alt="Sanctuary of Exile" />
      <button class="play-button" type="button" onclick={play}>Play</button>
      <button class="setup-link" type="button" onclick={openSetupWizard}>SoE Companion Setup Wizard</button>
      {#if launchStatus}
        <div class="launch-status">{launchStatus}</div>
      {/if}
    </div>
  </div>

  <div class="home-dashboard">
    <button class="dash-card" type="button" onclick={() => onNavigate?.('achievements')}>
      <span>Total Kills</span>
      <strong>{totalKills.toLocaleString()}</strong>
    </button>
    <button class="dash-card achievement-dash" type="button" onclick={() => onNavigate?.('achievements')}>
      <span>Achievements</span>
      <strong>{achievementUnlocked}/{achievementProgress.length}</strong>
    </button>
    <button class="dash-card" type="button" onclick={() => onNavigate?.('holy-grail')}>
      <span>Holy Grail</span>
      <strong>{grail.found}/{grail.total}</strong>
    </button>
    <button class="dash-card" type="button" onclick={openChangelog}>
      <span>Current Version</span>
      <strong>v{__APP_VERSION__}</strong>
    </button>
  </div>
</section>

{#if showChangelog}
  <div class="changelog-backdrop" role="dialog" aria-modal="true" onkeydown={(e) => e.key === 'Escape' && (showChangelog = false)} onclick={() => (showChangelog = false)}>
    <div class="changelog-modal" onclick={(e) => e.stopPropagation()}>
      <div class="changelog-header">
        <h2 class="changelog-title">Changelog</h2>
        <button type="button" class="changelog-close" onclick={() => (showChangelog = false)}>&times;</button>
      </div>
      <div class="changelog-body" onclick={(e) => {
        const a = (e.target as HTMLElement).closest('a.cl-hash');
        if (a) { e.preventDefault(); invoke('open_external_url', { url: (a as HTMLAnchorElement).href }); }
      }}>
        {@html changelogHtml}
      </div>
    </div>
  </div>
{/if}

{#if showSetupWizard}
  <SetupWizard
    onClose={() => (showSetupWizard = false)}
    onNavigate={(tab) => {
      showSetupWizard = false;
      onNavigate?.(tab);
    }}
  />
{/if}

<style>
  .home-tab {
    display: flex;
    flex-direction: column;
    gap: 12px;
    min-height: 0;
  }

  .home-hero {
    position: relative;
    min-height: min(58vh, 520px);
    overflow: hidden;
    border: 1px solid rgba(216, 167, 67, 0.24);
    border-radius: var(--radius-md);
    background: #050202;
    box-shadow: inset 0 0 0 1px rgba(255, 210, 90, 0.08), 0 14px 34px rgba(0, 0, 0, 0.35);
  }

  .home-slideshow,
  .home-slide,
  .home-slideshow::after {
    position: absolute;
    inset: 0;
  }

  .home-slideshow {
    z-index: 0;
    overflow: hidden;
  }

  .home-slideshow::after {
    content: '';
    z-index: 2;
    background:
      linear-gradient(90deg, rgba(0, 0, 0, 0.66), transparent 38%, rgba(0, 0, 0, 0.16)),
      linear-gradient(180deg, rgba(0, 0, 0, 0.02), rgba(0, 0, 0, 0.76));
    pointer-events: none;
  }

  .home-slide {
    z-index: 1;
    background-position: center 38%;
    background-repeat: no-repeat;
    background-size: cover;
    opacity: 0;
    transform: scale(1.02);
    transition: opacity 1700ms ease-in-out;
    will-change: opacity, transform;
  }

  .home-slide.active {
    opacity: 1;
    animation: hero-slide-drift 14s ease-in-out infinite alternate;
  }

  .home-embers {
    position: absolute;
    inset: 0;
    z-index: 0;
    overflow: hidden;
    pointer-events: none;
  }

  .home-embers span {
    position: absolute;
    left: var(--left);
    bottom: -18px;
    width: var(--size);
    height: var(--size);
    border-radius: 999px;
    background: radial-gradient(circle, #fff5bf 0%, #ffb13b 42%, rgba(255, 75, 20, 0.08) 72%, transparent 100%);
    box-shadow:
      0 0 7px rgba(255, 172, 56, 0.86),
      0 0 18px rgba(226, 67, 22, 0.42);
    opacity: 0;
    transform: translate3d(0, 0, 0);
    animation: home-ember-rise var(--dur) linear infinite;
    animation-delay: var(--delay);
  }

  .home-embers span:nth-child(4n) {
    filter: blur(0.6px);
  }

  .hero-content {
    position: absolute;
    right: 4.8%;
    bottom: 5.5%;
    z-index: 1;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 12px;
    text-align: center;
  }

  .soe-logo {
    width: min(360px, 30vw);
    min-width: 260px;
    height: auto;
    filter: drop-shadow(0 8px 18px rgba(0, 0, 0, 0.72));
  }

  .play-button {
    min-width: 210px;
    padding: 12px 42px;
    border: 1px solid rgba(255, 219, 132, 0.38);
    background: rgba(0, 0, 0, 0.56);
    color: #fff2d0;
    font-family: 'Diablo', Georgia, 'Times New Roman', serif;
    font-size: 30px;
    font-weight: 400;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    box-shadow: inset 0 0 24px rgba(255, 120, 20, 0.08);
  }

  .play-button:hover {
    border-color: var(--accent-primary);
    background: rgba(75, 18, 8, 0.68);
  }

  .setup-link,
  .launch-status {
    color: #fff2d0;
    font-family: var(--font-mono);
    font-size: 12px;
    text-shadow: 0 1px 6px rgba(0,0,0,0.8);
  }

  .setup-link {
    padding: 0;
    border: none;
    background: transparent;
    color: #ffd66c;
    cursor: pointer;
    text-decoration: underline;
    text-underline-offset: 3px;
  }

  .setup-link:hover {
    color: #fff2d0;
  }

  .launch-status {
    max-width: 320px;
    color: var(--text-secondary);
  }

  .home-dashboard {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
    gap: 8px;
  }

  .dash-card {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: 6px;
    padding: 12px 14px;
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-md);
    background: var(--panel-bg);
    color: var(--text-secondary);
    text-align: left;
  }

  .dash-card span {
    font-size: 11px;
    color: var(--text-muted);
  }

  .dash-card strong {
    max-width: 100%;
    overflow: hidden;
    color: var(--text-primary);
    font-size: 14px;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .dash-card:hover {
    border-color: var(--accent-primary);
  }

  .changelog-backdrop {
    position: fixed;
    inset: 0;
    z-index: 1000;
    display: flex;
    align-items: center;
    justify-content: center;
    background: rgba(0, 0, 0, 0.6);
  }

  .changelog-modal {
    display: flex;
    flex-direction: column;
    width: 92%;
    max-width: 640px;
    max-height: 85vh;
    background: var(--bg-secondary);
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-lg);
    box-shadow: var(--shadow-lg);
  }

  .changelog-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: var(--space-3) var(--space-4);
    border-bottom: 1px solid var(--border-primary);
  }

  .changelog-title {
    margin: 0;
    color: var(--text-primary);
    font-size: var(--text-lg);
    font-weight: 600;
  }

  .changelog-close {
    padding: 0;
    background: none;
    border: none;
    color: var(--text-muted);
    cursor: pointer;
    font-size: var(--text-2xl);
    line-height: 1;
  }

  .changelog-close:hover {
    color: var(--text-primary);
  }

  .changelog-body {
    padding: var(--space-3) var(--space-4);
    overflow-y: auto;
    color: var(--text-secondary);
    font-size: var(--text-sm);
    line-height: 1.6;
  }

  .changelog-body :global(.cl-version) {
    padding-bottom: var(--space-3);
    margin-bottom: var(--space-3);
    border-bottom: 1px solid var(--border-primary);
  }

  .changelog-body :global(.cl-version:last-child) {
    border-bottom: none;
    margin-bottom: 0;
  }

  .changelog-body :global(h2) {
    margin: 0 0 var(--space-2);
    color: var(--accent-primary);
    font-size: var(--text-lg);
    font-weight: 600;
  }

  .changelog-body :global(h3) {
    margin: var(--space-2) 0 var(--space-1);
    color: var(--text-primary);
    font-size: var(--text-xs);
    font-weight: 600;
    letter-spacing: 0.5px;
    text-transform: uppercase;
  }

  .changelog-body :global(.cl-entry) {
    padding: 1px 0 1px var(--space-3);
    color: var(--text-primary);
  }

  .changelog-body :global(.cl-scope) {
    color: var(--text-secondary);
    font-family: var(--font-mono);
    font-size: 0.9em;
    opacity: 0.85;
  }

  .changelog-body :global(.cl-scope::after) {
    content: ':  ';
  }

  .changelog-body :global(.cl-hash) {
    margin-left: var(--space-1);
    color: var(--text-muted);
    cursor: pointer;
    font-family: var(--font-mono);
    font-size: 0.85em;
    opacity: 0.5;
    text-decoration: underline;
  }

  .changelog-body :global(.cl-hash:hover) {
    opacity: 1;
  }

  @keyframes hero-slide-drift {
    from {
      background-position: center 36%;
      transform: scale(1.02);
    }
    to {
      background-position: center 42%;
      transform: scale(1.06);
    }
  }

  @keyframes home-ember-rise {
    0% {
      opacity: 0;
      transform: translate3d(0, 0, 0) scale(0.72);
    }
    12% {
      opacity: 0.86;
    }
    78% {
      opacity: 0.58;
    }
    100% {
      opacity: 0;
      transform: translate3d(var(--drift), -62vh, 0) scale(0.18);
    }
  }

  @media (prefers-reduced-motion: reduce) {
    .home-slide.active,
    .home-embers span {
      animation: none;
    }

    .home-slide {
      transition: none;
    }

    .home-embers {
      display: none;
    }
  }
</style>
