<script lang="ts">
  import { Toggle } from '../components';
  import { settingsStore } from '../stores';

  let overlayEnabled = $derived(settingsStore.settings.notificationOverlayEnabled);
  let duration = $derived(settingsStore.settings.notificationDuration);
  let fontSize = $derived(settingsStore.settings.notificationFontSize);
  let opacity = $derived(settingsStore.settings.notificationOpacity);

  function setDuration(value: number) {
    const clamped = Math.max(1000, Math.min(30000, value));
    settingsStore.set('notificationDuration', clamped);
  }

  function setFontSize(value: number) {
    const clamped = Math.max(10, Math.min(24, value));
    settingsStore.set('notificationFontSize', clamped);
  }

  function setOpacity(value: number) {
    const clamped = Math.max(0, Math.min(1, value));
    settingsStore.set('notificationOpacity', clamped);
  }
</script>

<section class="tab-content">
  <div class="settings-section">
    <h2 class="section-title">Notification Settings</h2>
    <p class="section-description">
      Customize how item drop notifications appear in the overlay.
    </p>

    <div class="settings-grid">
      <div class="setting-row">
        <div class="setting-info">
          <label class="setting-label" for="notification-overlay">Notification Overlay</label>
          <span class="setting-hint">Show or hide visual item popups. Sounds, loot history, and trackers keep working.</span>
        </div>
        <div class="setting-control">
          <Toggle
            id="notification-overlay"
            checked={overlayEnabled}
            onchange={(enabled) => settingsStore.setNotificationOverlayEnabled(enabled)}
          />
        </div>
      </div>

      <div class="setting-row">
        <div class="setting-info">
          <label class="setting-label" for="duration-slider">Display Duration</label>
          <span class="setting-hint">How long notifications stay visible (1-30 seconds)</span>
        </div>
        <div class="setting-control">
          <input
            type="range"
            id="duration-slider"
            min="1000"
            max="30000"
            step="500"
            value={duration}
            oninput={(e) => setDuration(parseInt(e.currentTarget.value))}
            class="slider"
          />
          <span class="setting-value">{(duration / 1000).toFixed(1)}s</span>
        </div>
      </div>

      <div class="setting-row">
        <div class="setting-info">
          <label class="setting-label" for="font-size-slider">Size</label>
          <span class="setting-hint">Scales the whole notification (10-24 px)</span>
        </div>
        <div class="setting-control">
          <input
            type="range"
            id="font-size-slider"
            min="10"
            max="24"
            step="1"
            value={fontSize}
            oninput={(e) => setFontSize(parseInt(e.currentTarget.value))}
            class="slider"
          />
          <span class="setting-value">{fontSize}px</span>
        </div>
      </div>

      <div class="setting-row">
        <div class="setting-info">
          <label class="setting-label" for="opacity-slider">Background Opacity</label>
          <span class="setting-hint">Transparency of notification background (0-100%)</span>
        </div>
        <div class="setting-control">
          <input
            type="range"
            id="opacity-slider"
            min="0"
            max="1"
            step="0.05"
            value={opacity}
            oninput={(e) => setOpacity(parseFloat(e.currentTarget.value))}
            class="slider"
          />
          <span class="setting-value">{Math.round(opacity * 100)}%</span>
        </div>
      </div>
    </div>
  </div>
</section>

<style>
  .tab-content {
    padding: var(--space-4);
    display: flex;
    flex-direction: column;
    gap: var(--space-4);
    overflow: auto;
  }

  .settings-section {
    display: flex;
    flex-direction: column;
    gap: var(--space-4);
    max-width: 980px;
  }

  .section-title {
    font-size: var(--text-lg);
    font-weight: 600;
    color: var(--text-primary);
    margin: 0;
  }

  .section-description {
    font-size: var(--text-sm);
    color: var(--text-muted);
    margin: var(--space-1) 0 0;
  }

  .settings-grid {
    display: flex;
    flex-direction: column;
    gap: var(--space-4);
  }

  .setting-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-4);
    padding: var(--space-3);
    background: var(--bg-secondary);
    border-radius: var(--radius-md);
  }

  .setting-info {
    display: flex;
    flex-direction: column;
    gap: var(--space-1);
  }

  .setting-label {
    font-size: var(--text-sm);
    font-weight: 500;
    color: var(--text-primary);
  }

  .setting-hint {
    font-size: var(--text-xs);
    color: var(--text-muted);
  }

  .setting-control {
    display: flex;
    align-items: center;
    gap: var(--space-3);
  }

  .slider {
    width: 160px;
    height: 6px;
    appearance: none;
    background: var(--bg-tertiary);
    border-radius: var(--radius-full);
    cursor: pointer;
  }

  .slider::-webkit-slider-thumb {
    appearance: none;
    width: 16px;
    height: 16px;
    background: var(--accent-primary);
    border-radius: var(--radius-full);
    cursor: pointer;
    transition: transform 0.1s ease;
  }

  .slider::-webkit-slider-thumb:hover {
    transform: scale(1.1);
  }

  .slider::-moz-range-thumb {
    width: 16px;
    height: 16px;
    background: var(--accent-primary);
    border: none;
    border-radius: var(--radius-full);
    cursor: pointer;
  }

  .setting-value {
    font-family: var(--font-mono);
    font-size: var(--text-sm);
    color: var(--text-primary);
    min-width: 50px;
    text-align: right;
  }
</style>
