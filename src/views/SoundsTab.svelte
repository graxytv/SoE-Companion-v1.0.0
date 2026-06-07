<script lang="ts">
  import { invoke } from '@tauri-apps/api/core';
  import { Button, SubTabs } from '../components';
  import ItemSoundsPanel from '../components/ItemSoundsPanel.svelte';
  import { FILTERBLADE_SOUND_PACKS, filterBladeSoundUrl } from '../lib/filterblade-sounds';
  import { settingsStore, type SoundSlot, type SoundSource } from '../stores';
  import { playSound } from '../lib/sound-player';

  const ACCEPT = '.mp3,.wav,.ogg,.m4a,.flac';
  const MAX_BYTES = 5 * 1024 * 1024;
  const subTabs = [
    { id: 'sound-slots', label: 'Sound Slots' },
    { id: 'item-sounds', label: 'Item Sounds' },
  ];

  let activeSubTab = $state('sound-slots');
  let masterVolume = $derived(settingsStore.settings.soundVolume);
  let slots = $derived(settingsStore.settings.sounds);

  // Inline error message per slot (1-based index → message). Cleared on
  // any successful import or on next attempt.
  let errors = $state<Record<number, string>>({});
  let filterBladePack = $state(FILTERBLADE_SOUND_PACKS[0]?.name ?? '');
  let filterBladeFile = $state(FILTERBLADE_SOUND_PACKS[0]?.clips[0]?.fileName ?? '');
  let filterBladeSlot = $state(1);
  let filterBladeStatus = $state('');
  let filterBladeBusy = $state(false);
  let filterBladePreviewVolume = $state(1);
  let filterBladePreview: HTMLAudioElement | null = null;

  const selectedFilterBladePack = $derived(
    FILTERBLADE_SOUND_PACKS.find((pack) => pack.name === filterBladePack) ?? FILTERBLADE_SOUND_PACKS[0]
  );
  const selectedFilterBladeClip = $derived(
    selectedFilterBladePack?.clips.find((clip) => clip.fileName === filterBladeFile) ?? selectedFilterBladePack?.clips[0]
  );

  function setError(slot: number, msg: string | null) {
    if (msg === null) {
      const next = { ...errors };
      delete next[slot];
      errors = next;
    } else {
      errors = { ...errors, [slot]: msg };
    }
  }

  function handleMasterVolumeInput(e: Event) {
    const target = e.currentTarget as HTMLInputElement;
    settingsStore.setSoundVolume(parseFloat(target.value));
  }

  function handleSlotVolumeInput(slot: number, e: Event) {
    const target = e.currentTarget as HTMLInputElement;
    settingsStore.updateSoundSlot(slot, { volume: parseFloat(target.value) });
  }

  function handleLabelInput(slot: number, e: Event) {
    const target = e.currentTarget as HTMLInputElement;
    settingsStore.updateSoundSlot(slot, { label: target.value });
  }

  function handlePlay(slot: number) {
    void playSound(slot, masterVolume);
  }

  function handleFilterBladePackChange(e: Event) {
    const target = e.currentTarget as HTMLSelectElement;
    filterBladePack = target.value;
    const pack = FILTERBLADE_SOUND_PACKS.find((entry) => entry.name === filterBladePack);
    filterBladeFile = pack?.clips[0]?.fileName ?? '';
    filterBladeStatus = '';
  }

  function handleFilterBladeFileChange(e: Event) {
    const target = e.currentTarget as HTMLSelectElement;
    filterBladeFile = target.value;
    filterBladeStatus = '';
  }

  function handleFilterBladeSlotChange(e: Event) {
    const target = e.currentTarget as HTMLSelectElement;
    filterBladeSlot = Number(target.value) || 1;
    filterBladeStatus = '';
  }

  function handleFilterBladePreviewVolumeInput(e: Event) {
    const target = e.currentTarget as HTMLInputElement;
    filterBladePreviewVolume = Number(target.value) || 0;
    if (filterBladePreview) {
      filterBladePreview.volume = Math.max(0, Math.min(1, filterBladePreviewVolume));
    }
  }

  async function handleDownloadFilterBladeSound() {
    if (!selectedFilterBladePack || !selectedFilterBladeClip) return;
    filterBladeBusy = true;
    filterBladeStatus = 'Downloading...';
    try {
      const fileName = await invoke<string>('download_filterblade_community_sound', {
        slot: filterBladeSlot,
        pack: selectedFilterBladePack.name,
        fileName: selectedFilterBladeClip.fileName,
      });
      settingsStore.updateSoundSlot(filterBladeSlot, {
        label: `${selectedFilterBladePack.label} - ${selectedFilterBladeClip.label}`,
        source: { kind: 'custom', fileName },
      });
      filterBladeStatus = `Saved to sound slot ${filterBladeSlot}.`;
    } catch (err) {
      filterBladeStatus = String(err);
    } finally {
      filterBladeBusy = false;
    }
  }

  function handlePreviewFilterBladeSound() {
    if (!selectedFilterBladePack || !selectedFilterBladeClip) return;
    const url = filterBladeSoundUrl(selectedFilterBladePack.name, selectedFilterBladeClip.fileName);
    filterBladePreview?.pause();
    filterBladePreview = new Audio(url);
    filterBladePreview.volume = Math.max(0, Math.min(1, filterBladePreviewVolume));
    filterBladeStatus = 'Previewing...';
    filterBladePreview.onended = () => {
      if (filterBladeStatus === 'Previewing...') filterBladeStatus = '';
    };
    filterBladePreview.onerror = () => {
      filterBladeStatus = 'Preview failed. The sound may be unavailable from FilterBlade right now.';
    };
    void filterBladePreview.play().catch((err) => {
      filterBladeStatus = String(err);
    });
  }

  async function handleFilePicked(slot: number, e: Event) {
    const input = e.currentTarget as HTMLInputElement;
    const file = input.files?.[0];
    // Reset the input so re-selecting the same file fires `change` again.
    input.value = '';
    if (!file) return;

    setError(slot, null);
    if (file.size > MAX_BYTES) {
      setError(slot, `File too large (${Math.ceil(file.size / 1024)} KB, max 5 MB)`);
      return;
    }

    try {
      const buf = await file.arrayBuffer();
      const bytes = Array.from(new Uint8Array(buf));
      const fileName = await invoke<string>('import_sound_file', {
        slot,
        fileName: file.name,
        bytes,
      });
      const source: SoundSource = { kind: 'custom', fileName };
      settingsStore.updateSoundSlot(slot, { source });
    } catch (err) {
      setError(slot, String(err));
    }
  }

  async function handleReset(slot: number) {
    setError(slot, null);
    try {
      await invoke('delete_sound_file', { slot });
    } catch (err) {
      setError(slot, String(err));
      return;
    }
    settingsStore.updateSoundSlot(slot, { source: { kind: 'default' } });
  }

  async function handleDelete(slot: number) {
    setError(slot, null);
    try {
      await invoke('delete_sound_file', { slot });
    } catch (err) {
      setError(slot, String(err));
      return;
    }
    settingsStore.updateSoundSlot(slot, { source: { kind: 'empty' } });
  }

  function handleAdd() {
    settingsStore.appendSoundSlot();
  }

  function isBuiltin(slot: number): boolean {
    return slot >= 1 && slot <= 7;
  }

  function isCustom(source: SoundSource): boolean {
    return source.kind === 'custom';
  }

  function isEmpty(source: SoundSource): boolean {
    return source.kind === 'empty';
  }

  function fileInputId(slot: number): string {
    return `sound-file-${slot}`;
  }
</script>

<section class="tab-content">
  <SubTabs tabs={subTabs} bind:activeTab={activeSubTab} ariaLabel="Sounds sections" />

  {#if activeSubTab === 'sound-slots'}
    <div class="settings-section">
      <div class="setting-row">
        <div class="setting-info">
          <span class="setting-label">Master volume</span>
          <span class="setting-hint">
            Multiplied with each slot's per-sound volume. Set to 0 to silence everything.
          </span>
        </div>
        <div class="setting-control">
          <input
            type="range"
            min="0"
            max="1"
            step="0.05"
            value={masterVolume}
            oninput={handleMasterVolumeInput}
            class="slider"
            aria-label="Master sound volume"
          />
          <span class="setting-value">{Math.round(masterVolume * 100)}%</span>
        </div>
      </div>
    </div>

    <div class="settings-section">
      <h2 class="section-title">Sounds</h2>

      <div class="filterblade-panel">
        <div class="filterblade-copy">
          <h3>FilterBlade Community Sounds</h3>
          <p>
            Optional local download from FilterBlade. SoE Companion does not bundle or redistribute these files;
            choosing one here saves it into your app data and assigns it to a sound slot.
          </p>
        </div>

        <div class="filterblade-controls">
          <label>
            <span>Pack</span>
            <select value={filterBladePack} onchange={handleFilterBladePackChange}>
              {#each FILTERBLADE_SOUND_PACKS as pack}
                <option value={pack.name}>{pack.label}</option>
              {/each}
            </select>
          </label>

          <label>
            <span>Clip</span>
            <select value={filterBladeFile} onchange={handleFilterBladeFileChange}>
              {#each selectedFilterBladePack?.clips ?? [] as clip}
                <option value={clip.fileName}>{clip.label}</option>
              {/each}
            </select>
          </label>

          <label>
            <span>Slot</span>
            <select value={String(filterBladeSlot)} onchange={handleFilterBladeSlotChange}>
              {#each slots as _slot, i}
                <option value={i + 1}>Sound {i + 1}</option>
              {/each}
            </select>
          </label>

          <label class="preview-volume">
            <span>Preview volume</span>
            <div class="preview-volume-control">
              <input
                type="range"
                min="0"
                max="1"
                step="0.05"
                value={filterBladePreviewVolume}
                oninput={handleFilterBladePreviewVolumeInput}
                class="slider"
                aria-label="FilterBlade preview volume"
              />
              <span class="setting-value">{Math.round(filterBladePreviewVolume * 100)}%</span>
            </div>
          </label>

          <Button
            variant="primary"
            size="sm"
            disabled={filterBladeBusy}
            onclick={handleDownloadFilterBladeSound}
          >
            {filterBladeBusy ? 'Downloading...' : 'Download to Slot'}
          </Button>

          <Button
            variant="secondary"
            size="sm"
            disabled={filterBladeBusy}
            onclick={handlePreviewFilterBladeSound}
          >
            Preview
          </Button>
        </div>

        {#if filterBladeStatus}
          <div class="filterblade-status">{filterBladeStatus}</div>
        {/if}
      </div>

      {#each slots as slot, i (i)}
        {@const slotIndex = i + 1}
        {@const empty = isEmpty(slot.source)}
        <div class="slot-row">
          <div class="slot-num" class:slot-num-empty={empty}>{slotIndex}</div>

          <input
            class="slot-label"
            class:slot-label-empty={empty}
            type="text"
            value={slot.label}
            placeholder={`Sound ${slotIndex}`}
            oninput={(e) => handleLabelInput(slotIndex, e)}
            aria-label={`Label for sound ${slotIndex}`}
          />

          <div class="slot-volume" class:slot-volume-empty={empty}>
            <input
              type="range"
              min="0"
              max="1"
              step="0.05"
              value={slot.volume}
              disabled={empty}
              oninput={(e) => handleSlotVolumeInput(slotIndex, e)}
              class="slider"
              aria-label={`Volume for sound ${slotIndex}`}
            />
            <span class="setting-value">{Math.round(slot.volume * 100)}%</span>
          </div>

          <div class="slot-actions">
            {#if !empty}
              <Button
                variant="secondary"
                size="sm"
                onclick={() => handlePlay(slotIndex)}
              >
                Play
              </Button>
            {/if}

            <Button variant="secondary" size="sm" onclick={() => {
              document.getElementById(fileInputId(slotIndex))?.click();
            }}>
              {empty ? 'Upload' : 'Replace'}
            </Button>
            <input
              id={fileInputId(slotIndex)}
              type="file"
              accept={ACCEPT}
              class="file-input-hidden"
              onchange={(e) => handleFilePicked(slotIndex, e)}
            />

            {#if isBuiltin(slotIndex) && isCustom(slot.source)}
              <Button variant="secondary" size="sm" onclick={() => handleReset(slotIndex)}>
                Reset
              </Button>
            {/if}

            {#if !isBuiltin(slotIndex) && isCustom(slot.source)}
              <Button variant="secondary" size="sm" onclick={() => handleDelete(slotIndex)}>
                Delete
              </Button>
            {/if}
          </div>

          {#if errors[slotIndex]}
            <div class="slot-error">{errors[slotIndex]}</div>
          {/if}
        </div>
      {/each}

      <div class="add-row">
        <Button variant="secondary" size="sm" onclick={handleAdd}>+ Add sound</Button>
      </div>
    </div>
  {:else if activeSubTab === 'item-sounds'}
    <ItemSoundsPanel />
  {/if}
</section>

<style>
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

  .filterblade-panel {
    display: grid;
    gap: var(--space-3);
    padding: var(--space-3);
    margin-bottom: var(--space-4);
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-md);
    background: var(--bg-secondary);
  }

  .filterblade-copy h3 {
    margin: 0 0 var(--space-1);
    color: var(--accent-primary);
    font-size: var(--text-base);
  }

  .filterblade-copy p {
    margin: 0;
    color: var(--text-secondary);
    font-size: var(--text-sm);
    line-height: 1.45;
  }

  .filterblade-controls {
    display: grid;
    grid-template-columns: minmax(140px, 180px) minmax(220px, 1fr) minmax(100px, 130px) minmax(190px, 230px) auto auto;
    align-items: end;
    gap: var(--space-3);
  }

  .filterblade-controls label {
    display: grid;
    gap: var(--space-1);
    color: var(--text-secondary);
    font-size: var(--text-sm);
  }

  .filterblade-controls select {
    width: 100%;
    padding: var(--space-2);
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-sm);
    background: var(--bg-tertiary);
    color: var(--text-primary);
    font: inherit;
  }

  .preview-volume-control {
    display: flex;
    align-items: center;
    gap: var(--space-2);
  }

  .preview-volume-control .slider {
    width: 120px;
  }

  .filterblade-status {
    color: var(--text-secondary);
    font-size: var(--text-sm);
  }

  .slot-row {
    display: grid;
    grid-template-columns: 32px 180px 220px 1fr;
    align-items: center;
    gap: var(--space-3);
    padding: var(--space-2) 0;
    border-bottom: 1px solid var(--border-primary);
  }

  .slot-num {
    font-family: var(--font-mono);
    font-size: var(--text-sm);
    color: var(--text-muted);
    text-align: right;
  }

  .slot-num-empty,
  .slot-label-empty,
  .slot-volume-empty {
    opacity: 0.6;
  }

  .slot-label {
    padding: var(--space-1) var(--space-2);
    background: var(--bg-tertiary);
    border: 1px solid var(--border-primary);
    border-radius: var(--radius-sm);
    color: var(--text-primary);
    font: inherit;
  }

  .slot-volume {
    display: flex;
    align-items: center;
    gap: var(--space-2);
  }

  .slot-actions {
    display: flex;
    align-items: center;
    gap: var(--space-2);
    justify-self: end;
  }

  .file-input-hidden {
    position: absolute;
    width: 1px;
    height: 1px;
    overflow: hidden;
    clip: rect(0 0 0 0);
    white-space: nowrap;
  }

  .slot-error {
    grid-column: 2 / -1;
    color: var(--status-error-text);
    font-size: var(--text-sm);
  }

  .add-row {
    margin-top: var(--space-3);
  }

  @media (max-width: 820px) {
    .filterblade-controls {
      grid-template-columns: 1fr;
    }
  }
</style>
