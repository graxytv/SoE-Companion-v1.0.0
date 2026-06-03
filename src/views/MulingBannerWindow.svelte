<script lang="ts">
  import { invoke } from '@tauri-apps/api/core';
  import { settingsStore } from '../stores';

  function disable() {
    settingsStore.setDropsTrackerMulingMode(false);
    invoke('set_muling_banner_window_visible', { visible: false }).catch(() => {});
  }

  let mulingHotkeyLabel = $derived(
    (settingsStore.settings.mulingModeHotkey?.keyCode ?? 0) === 0 &&
    (settingsStore.settings.mulingModeHotkey?.modifiers ?? 0) === 0
      ? 'None'
      : (settingsStore.settings.mulingModeHotkey?.display ?? 'None'),
  );
</script>

<section class="muling-banner" aria-label="Muling Mode active">
  <div class="banner-copy">
    <span class="banner-kicker">Muling Mode Active</span>
    <strong>Tracking Paused</strong>
    <span class="banner-detail">{mulingHotkeyLabel} toggles muling mode</span>
  </div>
  <button type="button" class="disable-button" onclick={disable}>Turn Off</button>
</section>

<style>
  :global(html),
  :global(body) {
    width: 100%;
    height: 100%;
    margin: 0;
    overflow: hidden;
    background: transparent !important;
  }

  .muling-banner {
    width: 100vw;
    height: 100vh;
    box-sizing: border-box;
    display: grid;
    grid-template-columns: minmax(0, 1fr) auto;
    align-items: center;
    gap: 12px;
    padding: 8px 10px 8px 12px;
    background:
      linear-gradient(135deg, rgba(65, 12, 8, 0.96), rgba(14, 4, 3, 0.94)),
      var(--overlay-panel-bg, rgba(0, 0, 0, 0.9));
    border: 1px solid var(--overlay-panel-border, rgba(255, 174, 56, 0.75));
    border-radius: 7px;
    box-shadow:
      inset 0 0 0 1px rgba(255, 221, 129, 0.08),
      0 8px 22px rgba(0, 0, 0, 0.38),
      0 0 18px rgba(230, 83, 28, 0.18);
    color: var(--text-primary, #fff);
    font-family: var(--font-mono, monospace);
    user-select: none;
  }

  .banner-copy {
    display: grid;
    min-width: 0;
    gap: 1px;
    text-align: center;
  }

  .banner-kicker {
    color: var(--accent-primary, #ffc34f);
    font-family: var(--font-display, var(--font-mono, monospace));
    font-size: 13px;
    line-height: 1;
    text-transform: uppercase;
  }

  strong {
    color: #fff4cf;
    font-size: 13px;
    line-height: 1.1;
    text-transform: uppercase;
  }

  .banner-detail {
    min-width: 0;
    overflow: hidden;
    color: var(--text-muted, rgba(255, 225, 178, 0.72));
    font-size: 10px;
    line-height: 1.2;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .disable-button {
    min-width: 74px;
    height: 28px;
    border: 1px solid rgba(255, 193, 91, 0.76);
    border-radius: 4px;
    background: linear-gradient(180deg, rgba(255, 178, 61, 0.96), rgba(177, 70, 12, 0.96));
    color: #160704;
    cursor: pointer;
    font-family: var(--font-mono, monospace);
    font-size: 11px;
    font-weight: 900;
    line-height: 1;
    text-transform: uppercase;
  }

  .disable-button:hover {
    filter: brightness(1.08);
  }
</style>
