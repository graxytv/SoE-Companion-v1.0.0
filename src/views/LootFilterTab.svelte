<script lang="ts">
  import { onMount } from "svelte";
  import { invoke } from "@tauri-apps/api/core";
  import { RulesEditor } from "../editor";
  import { Button } from "../components";
  import { settingsStore } from "../stores";

  type SaveState = "loading" | "saved" | "unsaved" | "saving" | "error";

  interface SoeFilterProfile {
    name: string;
    modified?: string | null;
  }

  const CREATE_NEW_VALUE = "__create_new__";
  const DEFAULT_FILTER_NAME = "Hiim_SOE";

  let filterText = $state("");
  let saveState = $state<SaveState>("loading");
  let statusMessage = $state("Loading filters...");
  let saveTimer: ReturnType<typeof setTimeout> | null = null;
  let lastSavedText = "";
  let loading = $state(true);
  let profiles = $state<SoeFilterProfile[]>([]);
  let selectedFilterName = $state("");
  let showInstallHelp = $state(false);

  function profileExists(name: string): boolean {
    return profiles.some((profile) => profile.name === name);
  }

  async function refreshProfiles(): Promise<void> {
    profiles = await invoke<SoeFilterProfile[]>("list_soe_filter_profiles");
    if (selectedFilterName === "Kryzard SoE" || selectedFilterName === "Kryzard_SoE") {
      selectedFilterName = "";
      settingsStore.set("activeProfile", null);
    }
  }

  async function loadProfile(name: string): Promise<void> {
    if (!name) return;
    loading = true;
    saveState = "loading";
    statusMessage = `Loading ${name}...`;
    try {
      const text = await invoke<string>("load_soe_filter_profile", { name });
      filterText = text;
      selectedFilterName = name;
      settingsStore.set("activeProfile", name);
      lastSavedText = text;

      saveState = "saved";
      statusMessage = "Saved.";
    } catch (error) {
      saveState = "error";
      statusMessage = `Could not load ${name}: ${error}`;
    } finally {
      loading = false;
    }
  }

  onMount(async () => {
    try {
      await refreshProfiles();

      const savedProfile = settingsStore.settings.activeProfile;
      const preferred = savedProfile && savedProfile !== "Kryzard SoE" && savedProfile !== "Kryzard_SoE" && profileExists(savedProfile)
        ? settingsStore.settings.activeProfile
        : profileExists(DEFAULT_FILTER_NAME)
          ? DEFAULT_FILTER_NAME
          : profiles[0]?.name;

      if (preferred) {
        await loadProfile(preferred);
      } else {
        filterText = await invoke<string>("get_bundled_soe_filter");
        lastSavedText = filterText;
        saveState = "saved";
        statusMessage = "Saved.";
      }
    } catch (error) {
      saveState = "error";
      statusMessage = `Could not load SoE filters: ${error}`;
    } finally {
      loading = false;
    }
  });

  async function saveCurrentFilter(text: string): Promise<void> {
    if (!selectedFilterName) return;
    saveState = "saving";
    try {
      await invoke<SoeFilterProfile>("save_soe_filter_profile", {
        name: selectedFilterName,
        text,
      });
      lastSavedText = text;
      saveState = "saved";
      statusMessage = "Saved.";
      void refreshProfiles();
    } catch (error) {
      saveState = "error";
      statusMessage = `Could not save filter preset: ${error}`;
    }
  }

  function scheduleAutosave(): void {
    if (!selectedFilterName) return;
    if (filterText === lastSavedText) {
      saveState = "saved";
      statusMessage = "Saved.";
      return;
    }

    saveState = "unsaved";
    statusMessage = "Unsaved changes.";
    if (saveTimer) clearTimeout(saveTimer);
    saveTimer = setTimeout(() => {
      saveTimer = null;
      void saveCurrentFilter(filterText);
    }, 700);
  }

  function handleChange(value: string): void {
    filterText = value;
    scheduleAutosave();
  }

  async function handleSave(value: string): Promise<void> {
    filterText = value;
    if (saveTimer) {
      clearTimeout(saveTimer);
      saveTimer = null;
    }
    await saveCurrentFilter(filterText);
  }

  async function createNewFilter(): Promise<void> {
    const name = window.prompt("New filter name");
    const trimmed = name?.trim();
    if (!trimmed) return;

    if (saveTimer) {
      clearTimeout(saveTimer);
      saveTimer = null;
    }

    saveState = "saving";
    try {
      await invoke<SoeFilterProfile>("create_soe_filter_profile", {
        name: trimmed,
        text: filterText,
      });
      await refreshProfiles();
      await loadProfile(trimmed);
    } catch (error) {
      saveState = "error";
      statusMessage = `Could not create filter: ${error}`;
    }
  }

  async function renameCurrentFilter(): Promise<void> {
    if (!selectedFilterName) return;
    const name = window.prompt("Rename filter preset", selectedFilterName);
    const trimmed = name?.trim();
    if (!trimmed || trimmed === selectedFilterName) return;

    if (saveTimer) {
      clearTimeout(saveTimer);
      saveTimer = null;
      await saveCurrentFilter(filterText);
    }

    loading = true;
    saveState = "saving";
    statusMessage = `Renaming ${selectedFilterName}...`;
    try {
      const renamed = await invoke<SoeFilterProfile>("rename_soe_filter_profile", {
        oldName: selectedFilterName,
        newName: trimmed,
      });
      await refreshProfiles();
      await loadProfile(renamed.name);
      statusMessage = `Renamed to ${renamed.name}.`;
    } catch (error) {
      saveState = "error";
      statusMessage = `Could not rename filter: ${error}`;
    } finally {
      loading = false;
    }
  }

  async function deleteCurrentFilter(): Promise<void> {
    if (!selectedFilterName) return;
    const confirmed = window.confirm(`Delete filter preset "${selectedFilterName}"? This cannot be undone.`);
    if (!confirmed) return;

    if (saveTimer) {
      clearTimeout(saveTimer);
      saveTimer = null;
    }

    loading = true;
    saveState = "saving";
    statusMessage = `Deleting ${selectedFilterName}...`;
    try {
      await invoke("delete_soe_filter_profile", { name: selectedFilterName });
      settingsStore.set("activeProfile", null);
      selectedFilterName = "";
      await refreshProfiles();
      const next = profileExists(DEFAULT_FILTER_NAME)
        ? DEFAULT_FILTER_NAME
        : profiles[0]?.name;
      if (next) {
        await loadProfile(next);
      } else {
        filterText = await invoke<string>("get_bundled_soe_filter");
        lastSavedText = filterText;
        saveState = "saved";
        statusMessage = "Deleted preset. Loaded bundled filter text.";
      }
    } catch (error) {
      saveState = "error";
      statusMessage = `Could not delete filter: ${error}`;
    } finally {
      loading = false;
    }
  }

  async function handleProfileSelect(event: Event): Promise<void> {
    const value = (event.currentTarget as HTMLSelectElement).value;
    if (value === CREATE_NEW_VALUE) {
      (event.currentTarget as HTMLSelectElement).value = selectedFilterName;
      await createNewFilter();
      return;
    }

    if (value && value !== selectedFilterName) {
      if (saveTimer) {
        clearTimeout(saveTimer);
        saveTimer = null;
        await saveCurrentFilter(filterText);
      }
      await loadProfile(value);
    }
  }

  async function copyFilterText(): Promise<void> {
    try {
      await navigator.clipboard.writeText(filterText);
      statusMessage = "Filter text copied.";
    } catch (error) {
      saveState = "error";
      statusMessage = `Could not copy filter text: ${error}`;
    }
  }
</script>

<section class="loot-filter-tab">
  <header class="tab-header">
    <div class="header-left">
      <span
        class="status-badge"
        class:saved={saveState === "saved"}
        class:error={saveState === "error"}
        class:saving={saveState === "saving" || saveState === "unsaved" || saveState === "loading"}
        title={statusMessage}
      >
        {#if saveState === "loading"}
          Loading
        {:else if saveState === "saving"}
          Saving
        {:else if saveState === "unsaved"}
          Unsaved
        {:else if saveState === "error"}
          Save failed
        {:else}
          Saved
        {/if}
      </span>
    </div>

    <div class="header-actions">
      <Button variant="secondary" size="sm" onclick={() => (showInstallHelp = true)}>How to install filter</Button>
      <Button variant="primary" size="sm" disabled={loading} onclick={copyFilterText}>Copy Filter Text</Button>
      <Button variant="secondary" size="sm" disabled={loading || !selectedFilterName} onclick={renameCurrentFilter}>Rename</Button>
      <Button variant="danger" size="sm" disabled={loading || !selectedFilterName} onclick={deleteCurrentFilter}>Delete</Button>
      <select class="filter-select" value={selectedFilterName} disabled={loading} onchange={handleProfileSelect} aria-label="Filter">
        {#each profiles as profile (profile.name)}
          <option value={profile.name}>{profile.name}</option>
        {/each}
        <option value={CREATE_NEW_VALUE}>Create new filter...</option>
      </select>
    </div>
  </header>

  <div class="status-line" class:error={saveState === "error"}>
    {statusMessage}
  </div>

  <div class="editor-container">
    <RulesEditor
      bind:value={filterText}
      validate={false}
      onchange={handleChange}
      onsave={handleSave}
    />
  </div>
</section>

{#if showInstallHelp}
  <div class="install-help-backdrop" role="presentation" onclick={() => (showInstallHelp = false)}>
    <div class="install-help-modal" role="dialog" aria-modal="true" aria-label="How to install filter" onclick={(event) => event.stopPropagation()}>
      <div class="install-help-header">
        <h2>How to install filter</h2>
        <button type="button" aria-label="Close" onclick={() => (showInstallHelp = false)}>&times;</button>
      </div>

      <ol class="install-steps">
        <li>
          <strong>Reload your loot filter in game.</strong>
          <span>Open <code>Options &gt; Configure Hotkeys</code> and use your Reload Loot Filter keybind. It is close to the bottom of the hotkey list.</span>
        </li>
        <li>
          <strong>Find the active filter path.</strong>
          <span>After a few seconds, white text appears in the top-left of the game. Look for <code>Reloaded Filter</code>, not <code>Reloaded Config</code>. The end of that line shows the filter file Diablo II is reading, often <code>default.filter</code>.</span>
        </li>
        <li>
          <strong>Open that exact filter file.</strong>
          <span>Follow the path shown by the game, open the filter file in Notepad, delete all existing text, then return to SoE Companion.</span>
        </li>
        <li>
          <strong>Copy and paste this filter.</strong>
          <span>Click <code>Copy Filter Text</code>, paste it into the game's active filter file, then use <code>File &gt; Save</code> in Notepad.</span>
        </li>
        <li>
          <strong>Reload once more in game.</strong>
          <span>Press your Reload Loot Filter keybind again. When the game shows <code>Reloaded Filter</code>, you are good to go.</span>
        </li>
      </ol>

      <div class="install-help-note">
        SoE Companion stores filter presets for editing, but the game decides which filter file is active.
      </div>
    </div>
  </div>
{/if}

<style>
  .loot-filter-tab {
    display: flex;
    flex-direction: column;
    flex: 1;
    min-height: 0;
    gap: var(--space-3, 12px);
    overflow: hidden;
  }

  .tab-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-shrink: 0;
    flex-wrap: wrap;
    gap: var(--space-2, 8px);
  }

  .header-left,
  .header-actions {
    display: flex;
    align-items: center;
    gap: var(--space-2, 8px);
  }

  .status-badge {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-width: 72px;
    padding: 4px 10px;
    border-radius: var(--radius-sm, 4px);
    font-size: var(--text-xs, 12px);
    font-weight: 700;
    background: color-mix(in srgb, var(--text-secondary) 12%, transparent);
    color: var(--text-secondary);
    white-space: nowrap;
  }

  .status-badge.saved {
    background: color-mix(in srgb, var(--status-success-text) 16%, transparent);
    color: var(--status-success-text);
  }

  .status-badge.saving {
    background: color-mix(in srgb, var(--accent-primary) 18%, transparent);
    color: var(--accent-primary);
  }

  .status-badge.error {
    background: color-mix(in srgb, var(--status-error-text) 18%, transparent);
    color: var(--status-error-text);
  }

  .filter-select {
    min-width: 180px;
    padding: 6px 10px;
    border: 1px solid var(--input-border);
    border-radius: var(--radius-sm, 4px);
    background: var(--input-bg);
    color: var(--text-primary);
    font-size: var(--text-xs, 12px);
  }

  .status-line {
    flex-shrink: 0;
    padding: 8px 10px;
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-md, 8px);
    background: var(--bg-secondary);
    color: var(--text-secondary);
    font-size: var(--text-xs, 12px);
    line-height: 1.4;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .status-line.error {
    color: var(--status-error-text);
  }

  .editor-container {
    flex: 1;
    min-height: 0;
    overflow: hidden;
  }

  .install-help-backdrop {
    position: fixed;
    inset: 0;
    z-index: 1000;
    display: grid;
    place-items: center;
    padding: 24px;
    background: rgba(0, 0, 0, 0.62);
  }

  .install-help-modal {
    width: min(720px, 100%);
    max-height: min(760px, calc(100vh - 48px));
    overflow: auto;
    border: 1px solid var(--border-primary);
    border-radius: 10px;
    background: var(--bg-primary);
    box-shadow: 0 18px 48px rgba(0, 0, 0, 0.48);
  }

  .install-help-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
    padding: 16px 18px;
    border-bottom: 1px solid var(--border-primary);
    background: var(--bg-secondary);
  }

  .install-help-header h2 {
    margin: 0;
    color: var(--accent-primary);
    font-family: var(--font-display);
    font-size: 24px;
    font-weight: 400;
  }

  .install-help-header button {
    padding: 0;
    border: none;
    background: transparent;
    color: var(--text-muted);
    cursor: pointer;
    font-size: 28px;
    line-height: 1;
  }

  .install-help-header button:hover {
    color: var(--text-primary);
  }

  .install-steps {
    display: grid;
    gap: 12px;
    margin: 0;
    padding: 18px 22px 12px 42px;
  }

  .install-steps li {
    padding-left: 6px;
    color: var(--text-secondary);
    line-height: 1.5;
  }

  .install-steps strong {
    display: block;
    color: var(--text-primary);
    font-size: 14px;
  }

  .install-steps span {
    display: block;
    margin-top: 2px;
  }

  .install-steps code {
    color: var(--accent-primary);
    font-family: var(--font-mono);
  }

  .install-help-note {
    margin: 0 18px 18px;
    padding: 10px 12px;
    border: 1px solid color-mix(in srgb, var(--accent-primary) 36%, transparent);
    border-radius: 8px;
    background: color-mix(in srgb, var(--accent-primary) 9%, transparent);
    color: var(--text-secondary);
    font-size: 12px;
  }
</style>
