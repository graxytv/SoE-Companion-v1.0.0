<script lang="ts">
  import { settingsStore, type AppTheme } from '../stores';

  const themes: Array<{ id: AppTheme; label: string }> = [
    { id: 'sanctuary', label: 'Sanctuary Gothic' },
    { id: 'hellfire', label: 'Hellfire' },
    { id: 'horadric', label: 'Horadric Parchment' },
    { id: 'dark', label: 'Classic Dark' },
    { id: 'light', label: 'Classic Light' },
  ];

  let theme = $derived(settingsStore.settings.theme);

  function setTheme(event: Event) {
    settingsStore.setTheme((event.currentTarget as HTMLSelectElement).value as AppTheme);
  }
</script>

<label class="theme-selector" title="Theme">
  <span>Theme</span>
  <select value={theme} onchange={setTheme} aria-label="Theme">
    {#each themes as option}
      <option value={option.id}>{option.label}</option>
    {/each}
  </select>
</label>

<style>
  .theme-selector {
    display: flex;
    align-items: center;
    gap: 8px;
    height: 36px;
    padding: 0 10px;
    background: var(--bg-tertiary);
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-md);
    color: var(--text-secondary);
    transition: all var(--transition-fast);
    font-size: 11px;
    font-family: var(--font-mono);
    text-transform: uppercase;
  }

  .theme-selector:hover {
    background: var(--bg-elevated);
    border-color: var(--accent-primary);
    color: var(--text-primary);
  }

  .theme-selector select {
    max-width: 170px;
    padding: 4px 20px 4px 4px;
    border: none;
    outline: none;
    background: var(--bg-tertiary);
    color: var(--text-primary);
    font: inherit;
    text-transform: none;
    cursor: pointer;
  }

  .theme-selector select:focus {
    background: var(--bg-elevated);
  }

  .theme-selector option {
    background: #16100b;
    color: #f4e3bf;
  }

  .theme-selector option:checked {
    background: #6f6256;
    color: #ffffff;
  }
</style>
