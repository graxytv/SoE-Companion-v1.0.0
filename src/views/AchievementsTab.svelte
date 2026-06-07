<script lang="ts">
  import { invoke } from '@tauri-apps/api/core';
  import { emit } from '@tauri-apps/api/event';
  import { Button, SubTabs, Toggle } from '../components';
  import { settingsStore, itemsDictionaryStore } from '../stores';
  import {
    ACHIEVEMENT_CATEGORIES,
    evaluateAchievements,
    normalizeAchievementStats,
    unlockedAchievementCount,
    type AchievementCategory,
    type AchievementProgress,
    type AchievementStats,
  } from '../lib/achievements';
  import { buildHolyGrailItems } from '../lib/holy-grail';
  import { playSound } from '../lib/sound-player';

  type AchievementView = AchievementCategory | 'Manual Editing' | 'Achievement Pop-up Settings' | 'Back-up';

  interface BackupStatus {
    backupExists: boolean;
    backupPath: string;
    exportedAt: string | null;
  }

  const bossKeys = [
    ['DClone:Any', 'DClone Any Tier'],
    ['DClone:T2', 'DClone T2'],
    ['Rathma:Any', 'Rathma Any Tier'],
    ['Rathma:T2', 'Rathma T2'],
    ['Lucion:Any', 'Lucion Any Tier'],
    ['Lucion:T2', 'Lucion T2'],
    ['Kiln:Any', 'Kiln Any Tier'],
    ['Kiln:T2', 'Kiln T2'],
    ['Uber Tristram:Any', 'Uber Tristram'],
    ['Uber Ancients:Any', 'Uber Ancients'],
    ['Map Boss:Any', 'Map Boss'],
    ['Dungeon Boss:Any', 'Dungeon Boss'],
    ['Andariel:Any', 'Andariel'],
    ['Duriel:Any', 'Duriel'],
    ['Mephisto:Any', 'Mephisto'],
    ['Diablo:Any', 'Diablo'],
    ['Baal:Any', 'Baal'],
  ] as const;

  const achievementTabs: Array<{ id: AchievementView; label: string }> = [
    ...ACHIEVEMENT_CATEGORIES.map((category) => ({ id: category, label: category })),
    { id: 'Manual Editing', label: 'Manual Editing' },
    { id: 'Achievement Pop-up Settings', label: 'Achievement Pop-up Settings' },
    { id: 'Back-up', label: 'Back-up' },
  ];

  interface BossAchievementTab {
    id: string;
    label: string;
    achievementIds: string[];
  }

  const bossAchievementTabs: BossAchievementTab[] = [
    { id: 'dclone', label: 'DClone', achievementIds: ['boss-dclone-any', 'boss-dclone-t2', 'boss-dclone-100'] },
    { id: 'rathma', label: 'Rathma', achievementIds: ['boss-rathma-any', 'boss-rathma-t2', 'boss-rathma-100'] },
    { id: 'lucion', label: 'Lucion', achievementIds: ['boss-lucion-any', 'boss-lucion-t2', 'boss-lucion-100'] },
    { id: 'kiln', label: 'Kiln', achievementIds: ['boss-kiln-any', 'boss-kiln-t2', 'boss-kiln-100'] },
    { id: 'uber-tristram', label: 'Uber Tristram', achievementIds: ['boss-uber-tristram-first', 'boss-uber-tristram-100'] },
    { id: 'uber-ancients', label: 'Uber Ancients', achievementIds: ['boss-uber-ancients-first', 'boss-uber-ancients-100'] },
    { id: 'map-boss', label: 'Map Boss', achievementIds: ['boss-map-boss-first', 'boss-map-boss-100'] },
    { id: 'dungeon-boss', label: 'Dungeon Boss', achievementIds: ['boss-dungeon-boss-first', 'boss-dungeon-boss-100'] },
    { id: 'andariel', label: 'Andariel', achievementIds: ['boss-andariel-first', 'boss-andariel-100'] },
    { id: 'duriel', label: 'Duriel', achievementIds: ['boss-duriel-first', 'boss-duriel-100'] },
    { id: 'mephisto', label: 'Mephisto', achievementIds: ['boss-mephisto-first', 'boss-mephisto-100'] },
    { id: 'diablo', label: 'Diablo', achievementIds: ['boss-diablo-first', 'boss-diablo-100'] },
    { id: 'baal', label: 'Baal', achievementIds: ['boss-baal-first', 'boss-baal-100'] },
  ];

  const bossAchievementIdsByTab: Record<string, Set<string>> = Object.fromEntries(
    bossAchievementTabs.map((tab) => [tab.id, new Set<string>(tab.achievementIds)]),
  );

  let selectedView = $state<AchievementView>('Unique Finds');
  let selectedBossView = $state(bossAchievementTabs[0]?.id ?? '');
  let backupStatus = $state<BackupStatus | null>(null);
  let backupMessage = $state('');
  let confirmReset = $state(false);
  let newCharacterName = $state('');
  let newCharacterClass = $state('Amazon');
  let newCharacterLevel = $state(90);

  let stats = $derived(settingsStore.settings.achievementStats);
  let achievementSettings = $derived(settingsStore.settings.achievementSettings);
  let achievementProgressOverlayEnabled = $derived(settingsStore.settings.achievementProgressOverlayEnabled);
  let monsterKillsOverlayEnabled = $derived(settingsStore.settings.monsterKillsOverlayEnabled);
  let holyGrailItems = $derived(buildHolyGrailItems(itemsDictionaryStore.dict));
  let progress = $derived(evaluateAchievements({
    stats,
    holyGrailFound: settingsStore.settings.holyGrailFound,
    holyGrailItems,
    runeTrackerCounts: settingsStore.settings.runeTrackerCounts,
  }));
  let visibleProgress = $derived(
    selectedView === 'Back-up'
      ? []
      : selectedView === 'Achievement Pop-up Settings'
      ? []
      : selectedView === 'Manual Editing'
      ? []
      : progress.filter((achievement) => achievement.category === selectedView),
  );
  let visibleCategoryProgress = $derived(
    selectedView === 'Bossing'
      ? visibleProgress.filter((achievement) => bossAchievementIdsByTab[selectedBossView]?.has(achievement.id))
      : visibleProgress,
  );
  let unlockedCount = $derived(unlockedAchievementCount(progress));
  let totalCount = $derived(progress.length);
  let unlockedPercent = $derived(totalCount > 0 ? (unlockedCount / totalCount) * 100 : 0);

  $effect(() => {
    settingsStore.evaluateAchievementUnlocks({
      holyGrailFound: settingsStore.settings.holyGrailFound,
      holyGrailItems,
      runeTrackerCounts: settingsStore.settings.runeTrackerCounts,
    });
  });

  function setStat<K extends keyof AchievementStats>(key: K, value: AchievementStats[K]) {
    settingsStore.updateAchievementStats({ [key]: value } as Partial<AchievementStats>);
  }

  function numericInput(value: unknown): number {
    return Math.max(0, Math.floor(Number(value) || 0));
  }

  function setBossCount(key: string, value: unknown) {
    settingsStore.setAchievementBossKills(key, numericInput(value));
  }

  function addCharacter() {
    if (!newCharacterName.trim()) return;
    settingsStore.setAchievementCharacterLevel({
      name: newCharacterName.trim(),
      className: newCharacterClass.trim(),
      level: Math.max(1, Math.min(99, Math.floor(newCharacterLevel || 1))),
    });
    newCharacterName = '';
    newCharacterLevel = 90;
  }

  function completeLabel(achievement: AchievementProgress): string {
    if (achievement.complete) return 'Complete';
    return `${Math.floor(achievement.value).toLocaleString()} / ${achievement.target.toLocaleString()}`;
  }

  function formatDate(value: string | null | undefined): string {
    if (!value) return 'Not unlocked';
    return new Date(value).toLocaleString();
  }

  function tierClass(tier: string): string {
    return tier.toLowerCase();
  }

  async function refreshBackupStatus() {
    try {
      backupStatus = await invoke<BackupStatus>('get_achievement_backup_status');
    } catch (error) {
      backupMessage = String(error);
    }
  }

  async function backupNow() {
    try {
      backupStatus = await invoke<BackupStatus>('backup_achievement_stats', { stats });
      backupMessage = 'All achievements backed up.';
    } catch (error) {
      backupMessage = String(error);
    }
  }

  async function backupCategory(category: AchievementCategory) {
    try {
      backupStatus = await invoke<BackupStatus>('backup_achievement_category', { category, stats });
      backupMessage = `${category} achievements backed up.`;
    } catch (error) {
      backupMessage = String(error);
    }
  }

  async function restoreBackup() {
    try {
      const restored = await invoke<unknown>('restore_achievement_backup');
      settingsStore.setAchievementStats(normalizeAchievementStats(restored));
      backupMessage = 'Backup imported.';
      await refreshBackupStatus();
    } catch (error) {
      backupMessage = String(error);
    }
  }

  async function openBackupFolder() {
    try {
      await invoke('open_achievement_backup_folder');
    } catch (error) {
      backupMessage = String(error);
    }
  }

  function testAchievementPopup() {
    const entry = {
      id: 'test-achievement',
      name: 'Achievement Test',
      category: 'Unique Finds' as const,
      tier: 'Legendary' as const,
      unlockedAt: new Date().toISOString(),
      detail: 'Overlay and sound preview',
    };
    void emit('achievement-unlocked', entry);
    const slot = achievementSettings.soundSlot;
    if (achievementSettings.soundEnabled && slot != null) {
      void playSound(slot, settingsStore.settings.soundVolume * achievementSettings.soundVolume);
    }
  }

  function testAchievementSound() {
    const slot = achievementSettings.soundSlot;
    if (slot != null) {
      void playSound(slot, settingsStore.settings.soundVolume * achievementSettings.soundVolume);
    }
  }

  function resetAchievements() {
    settingsStore.resetAchievementProgress();
    confirmReset = false;
  }

  refreshBackupStatus();
</script>

<section class="tab-content achievements-tab">
  <div class="achievement-hero">
    <div>
      <h2 class="section-title">Achievements</h2>
      <p class="section-description">Account-wide progress, manual tracking, and local JSON backups. Use the header Sync button to refresh stats and character levels.</p>
    </div>
    <div class="achievement-hero-side">
      <div class="achievement-hero-actions">
        <Button variant="danger" size="sm" onclick={() => (confirmReset = true)}>Reset Achievements</Button>
      </div>
      <div class="achievement-summary-card">
        <span class="summary-percent">{unlockedPercent.toFixed(1)}%</span>
        <span class="summary-label">{unlockedCount} / {totalCount} Unlocked</span>
      </div>
    </div>
  </div>

  {#if confirmReset}
    <div class="confirm-backdrop" role="presentation">
      <div class="confirm-dialog" role="dialog" aria-modal="true" aria-label="Reset achievements">
        <h3>Reset Achievements?</h3>
        <p>This cannot be undone. All achievement progress, unlocks, manual totals, history, rune counts, and grail completion will be cleared.</p>
        <div class="confirm-actions">
          <Button variant="secondary" size="sm" onclick={() => (confirmReset = false)}>Cancel</Button>
          <Button variant="danger" size="sm" onclick={resetAchievements}>Reset</Button>
        </div>
      </div>
    </div>
  {/if}

  <div class="achievement-layout">
    <SubTabs tabs={achievementTabs} bind:activeTab={selectedView} />
    <div class="achievement-main">
      {#if selectedView === 'Back-up'}
        <div class="settings-section backup-tab">
          <h2 class="section-title">Achievement Backups</h2>
          <p class="section-description">Create one full achievement backup, or save a category snapshot for safekeeping.</p>

          <div class="backup-primary">
            <Button variant="primary" size="md" onclick={backupNow}>Back Up All</Button>
            <Button variant="secondary" size="md" onclick={restoreBackup}>Import Latest Full Backup</Button>
            <Button variant="secondary" size="md" onclick={openBackupFolder}>Open Backup Folder</Button>
          </div>

          <div class="category-backup-grid">
            {#each ACHIEVEMENT_CATEGORIES as category}
              <div class="category-backup-card">
                <div>
                  <strong>{category}</strong>
                  <span>{progress.filter((a) => a.category === category && a.complete).length}/{progress.filter((a) => a.category === category).length} complete</span>
                </div>
                <Button variant="secondary" size="sm" onclick={() => backupCategory(category)}>Back Up</Button>
              </div>
            {/each}
          </div>

          <p class="backup-status">
            {#if backupStatus?.backupExists}
              Latest backup: {formatDate(backupStatus.exportedAt)}
            {:else}
              No achievement backup yet.
            {/if}
            {#if backupMessage} {backupMessage}{/if}
          </p>
        </div>
      {:else if selectedView === 'Achievement Pop-up Settings'}
        <div class="settings-section achievement-controls">
          <h2 class="section-title">Achievement Unlock Popups</h2>
          <div class="setting-row">
            <div class="setting-info">
              <span class="setting-label">Achievement Unlock Popups</span>
              <span class="setting-hint">Progress updates silently. Popups only appear when an achievement reaches 100% for the first time.</span>
            </div>
            <Toggle checked={achievementSettings.overlayEnabled} onchange={(enabled) => settingsStore.setAchievementSettings({ overlayEnabled: enabled })} />
          </div>
          <div class="achievement-settings-grid">
            <label>
              Duration
              <input type="number" min="1500" max="20000" step="500" value={achievementSettings.overlayDuration} oninput={(e) => settingsStore.setAchievementSettings({ overlayDuration: numericInput((e.currentTarget as HTMLInputElement).value) })} />
            </label>
            <label>
              Font Size
              <input type="number" min="11" max="28" value={achievementSettings.overlayFontSize} oninput={(e) => settingsStore.setAchievementSettings({ overlayFontSize: numericInput((e.currentTarget as HTMLInputElement).value) })} />
            </label>
            <label>
              Opacity
              <input type="range" min="0.25" max="1" step="0.05" value={achievementSettings.overlayOpacity} oninput={(e) => settingsStore.setAchievementSettings({ overlayOpacity: Number((e.currentTarget as HTMLInputElement).value) })} />
            </label>
            <label class="sound-slot-field">
              Sound Slot
              <div class="sound-slot-row">
                <select value={achievementSettings.soundSlot ?? ''} onchange={(e) => settingsStore.setAchievementSettings({ soundSlot: (e.currentTarget as HTMLSelectElement).value ? Number((e.currentTarget as HTMLSelectElement).value) : null })}>
                  <option value="">No sound</option>
                  {#each settingsStore.settings.sounds as sound, i}
                    <option value={i + 1}>{i + 1}. {sound.label}</option>
                  {/each}
                </select>
                <Button variant="secondary" size="sm" onclick={testAchievementSound}>Test Sound</Button>
              </div>
            </label>
            <label>
              Sound Volume
              <input type="range" min="0" max="1" step="0.05" value={achievementSettings.soundVolume} oninput={(e) => settingsStore.setAchievementSettings({ soundVolume: Number((e.currentTarget as HTMLInputElement).value) })} />
            </label>
            <Button variant="secondary" size="sm" onclick={testAchievementPopup}>Test Popup</Button>
          </div>

          <div class="overlay-settings-panel">
            <div class="overlay-settings-heading">
              <div>
                <h3>Achievement Progress Overlay</h3>
                <p>Shows unlocked achievements and completion percentage.</p>
              </div>
              <Toggle checked={achievementProgressOverlayEnabled} onchange={(enabled) => settingsStore.setAchievementProgressOverlayEnabled(enabled)} />
            </div>
          </div>

          <div class="overlay-settings-panel">
            <div class="overlay-settings-heading">
              <div>
                <h3>Total Monster Kills Overlay</h3>
                <p>Shows synced/live account-wide monster kills.</p>
              </div>
              <Toggle checked={monsterKillsOverlayEnabled} onchange={(enabled) => settingsStore.setMonsterKillsOverlayEnabled(enabled)} />
            </div>
          </div>
        </div>
      {:else if selectedView === 'Manual Editing'}
        <div class="settings-section achievement-controls">
          <h2 class="section-title">Manual Editing</h2>
          <p class="section-description">Use this for kills, boss kills, and fallback character-level edits.</p>
          <div class="manual-grid">
            <label>Total Kills <input type="number" min="0" value={stats.totalKills} oninput={(e) => setStat('totalKills', numericInput((e.currentTarget as HTMLInputElement).value))} /></label>
            <label>Unique Items Found <input type="number" min="0" value={stats.uniqueItemsFound} oninput={(e) => setStat('uniqueItemsFound', numericInput((e.currentTarget as HTMLInputElement).value))} /></label>
            <label>Fate Cards Found <input type="number" min="0" value={stats.fateCardsFound} oninput={(e) => setStat('fateCardsFound', numericInput((e.currentTarget as HTMLInputElement).value))} /></label>
            <label>Tier 0 Fate Cards Found <input type="number" min="0" value={stats.tier0FateCardsFound} oninput={(e) => setStat('tier0FateCardsFound', numericInput((e.currentTarget as HTMLInputElement).value))} /></label>
            <label>First Elite Unique <input type="text" value={stats.firstEliteUniqueName ?? ''} oninput={(e) => setStat('firstEliteUniqueName', (e.currentTarget as HTMLInputElement).value.trim() || null)} /></label>
          </div>

          <div class="manual-panel">
            <h3>Boss Kills</h3>
            <div class="boss-grid">
              {#each bossKeys as [key, label]}
                <label>{label}<input type="number" min="0" value={stats.bossKills[key] ?? 0} oninput={(e) => setBossCount(key, (e.currentTarget as HTMLInputElement).value)} /></label>
              {/each}
            </div>
          </div>

          <div class="manual-panel">
            <h3>Character Levels</h3>
            <div class="character-add-row">
              <input type="text" placeholder="Character name" bind:value={newCharacterName} />
              <select bind:value={newCharacterClass}>
                {#each ['Amazon', 'Assassin', 'Barbarian', 'Druid', 'Necromancer', 'Paladin', 'Sorceress'] as className}
                  <option value={className}>{className}</option>
                {/each}
              </select>
              <input type="number" min="1" max="99" bind:value={newCharacterLevel} />
              <Button variant="secondary" size="sm" onclick={addCharacter}>Add</Button>
            </div>
            <div class="character-list">
              {#each Object.values(stats.characterLevels) as character (character.name)}
                <div class="character-row">
                  <span>{character.name}</span>
                  <span>{character.className}</span>
                  <input type="number" min="1" max="99" value={character.level} oninput={(e) => settingsStore.setAchievementCharacterLevel({ ...character, level: numericInput((e.currentTarget as HTMLInputElement).value) })} />
                  <Button variant="ghost" size="sm" onclick={() => settingsStore.removeAchievementCharacterLevel(character.name)}>Remove</Button>
                </div>
              {/each}
            </div>
          </div>
        </div>
      {:else}
      {#if selectedView === 'Bossing'}
        <div class="boss-achievement-tabs">
          <SubTabs tabs={bossAchievementTabs} bind:activeTab={selectedBossView} ariaLabel="Boss achievement groups" />
        </div>
      {/if}
      {#if selectedView === 'Levels'}
        <div class="level-sync-panel">
          <div>
            <h3>Character Levels</h3>
            <p>{Object.values(stats.characterLevels).length} tracked. Use the header Sync button to refresh saves.</p>
          </div>
        </div>
        {#if Object.values(stats.characterLevels).length > 0}
          <div class="level-character-grid">
            {#each Object.values(stats.characterLevels) as character (character.name)}
              <div class="level-character-card">
                <span>{character.name}</span>
                <span>{character.className || 'Unknown'}</span>
                <strong>{character.level}</strong>
              </div>
            {/each}
          </div>
        {/if}
      {/if}
      <div class="achievement-list">
        {#each visibleCategoryProgress as achievement (achievement.id)}
          {#if !achievement.hiddenUntilUnlocked || achievement.complete}
            <article class="achievement-card tier-{tierClass(achievement.tier)}" class:complete={achievement.complete}>
              <div class="achievement-card-top">
                <div>
                  <span class="achievement-tier">{achievement.tier}</span>
                  <h3>{achievement.name}</h3>
                </div>
                <span class="achievement-status">{completeLabel(achievement)}</span>
              </div>
              <div class="achievement-bar" aria-label={`${achievement.name} progress`}>
                <span style={`width: ${achievement.percent}%`}></span>
              </div>
              <div class="achievement-meta">
                <span>{Math.floor(achievement.percent)}%</span>
                <span>{formatDate(achievement.unlockedAt)}</span>
              </div>
              {#if achievement.description}
                <p>{achievement.description}</p>
              {/if}
            </article>
          {/if}
        {/each}
      </div>

      {/if}
    </div>
  </div>
</section>

<style>
  .achievements-tab {
    display: flex;
    flex-direction: column;
    gap: var(--space-4);
  }

  .achievement-hero,
  .achievement-layout,
  .achievement-card,
  .level-sync-panel,
  .manual-panel {
    border: 1px solid var(--border-primary);
    background: var(--bg-secondary);
    box-shadow: var(--shadow-sm);
  }

  .achievement-hero {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-4);
    padding: var(--space-5);
  }

  .achievement-hero-side {
    display: flex;
    align-items: center;
    gap: var(--space-4);
  }

  .achievement-summary-card {
    min-width: 180px;
    padding: 18px;
    border: 1px solid color-mix(in srgb, var(--accent-primary) 58%, transparent);
    border-radius: 8px;
    background: color-mix(in srgb, var(--bg-primary) 82%, transparent);
    display: grid;
    gap: 8px;
    text-align: right;
  }

  .achievement-hero-actions {
    display: grid;
    gap: var(--space-2);
    min-width: 150px;
  }

  .summary-percent {
    font-family: var(--font-display);
    font-size: 32px;
    color: var(--accent-primary);
  }

  .summary-label {
    color: var(--text-primary);
    font-size: 12px;
  }

  .achievement-layout {
    display: flex;
    flex-direction: column;
    min-height: 520px;
  }

  .achievement-main {
    display: flex;
    flex-direction: column;
    gap: var(--space-4);
    padding: var(--space-4);
  }

  .achievement-settings-grid,
  .manual-grid,
  .boss-grid,
  .character-add-row {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: var(--space-3);
  }

  .achievement-settings-grid label,
  .manual-grid label,
  .boss-grid label {
    display: grid;
    gap: 5px;
    color: var(--text-secondary);
    font-size: 12px;
  }

  .accountstats-sync-panel {
    display: grid;
    grid-template-columns: minmax(0, 1fr) auto;
    gap: var(--space-3);
    align-items: center;
  }

  .overlay-settings-panel {
    display: grid;
    gap: var(--space-3);
    padding: var(--space-4);
    border: 1px solid var(--border-primary);
    background: var(--bg-secondary);
  }

  .overlay-settings-heading {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-4);
    padding-bottom: var(--space-2);
    border-bottom: 1px solid var(--border-primary);
  }

  .overlay-settings-heading h3,
  .overlay-settings-heading p {
    margin: 0;
  }

  .overlay-settings-heading h3 {
    color: var(--text-primary);
    font-size: 14px;
  }

  .overlay-settings-heading p {
    margin-top: 4px;
    color: var(--text-secondary);
    font-size: 12px;
  }

  .accountstats-sync-actions {
    display: flex;
    align-items: center;
    gap: var(--space-3);
  }

  .accountstats-sync-total {
    display: grid;
    gap: 2px;
    text-align: right;
  }

  .accountstats-sync-total span {
    font-family: var(--font-mono);
    color: var(--accent-primary);
  }

  .accountstats-sync-message {
    grid-column: 1 / -1;
    margin: 0;
    color: var(--text-secondary);
    font-size: 12px;
  }

  .sound-slot-field {
    grid-column: span 2;
  }

  .sound-slot-row {
    display: grid;
    grid-template-columns: minmax(0, 1fr) auto;
    gap: var(--space-2);
    align-items: center;
  }

  input,
  select {
    width: 100%;
    padding: 8px 10px;
    border: 1px solid var(--border-primary);
    background: var(--bg-primary);
    color: var(--text-primary);
  }

  .manual-panel {
    padding: var(--space-4);
    display: grid;
    gap: var(--space-3);
  }

  .manual-panel h3 {
    margin: 0;
    color: var(--accent-primary);
  }

  .character-list {
    display: grid;
    gap: 6px;
  }

  .character-row {
    display: grid;
    grid-template-columns: 1fr 120px 90px auto;
    gap: var(--space-2);
    align-items: center;
    padding: 7px;
    background: var(--bg-tertiary);
  }

  .achievement-list {
    display: grid;
    gap: var(--space-3);
  }

  .boss-achievement-tabs {
    margin-bottom: var(--space-3);
  }

  .level-sync-panel {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-3);
    padding: var(--space-4);
  }

  .level-sync-panel h3,
  .level-sync-panel p {
    margin: 0;
  }

  .level-sync-panel h3 {
    color: var(--accent-primary);
  }

  .level-sync-panel p {
    margin-top: 4px;
    color: var(--text-secondary);
    font-size: 12px;
  }

  .level-character-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: var(--space-2);
  }

  .level-character-card {
    display: grid;
    grid-template-columns: minmax(0, 1fr) auto auto;
    align-items: center;
    gap: var(--space-2);
    padding: 9px 10px;
    border: 1px solid var(--border-primary);
    background: var(--bg-tertiary);
  }

  .level-character-card span {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    color: var(--text-secondary);
    font-size: 12px;
  }

  .level-character-card span:first-child {
    color: var(--text-primary);
    font-weight: 600;
  }

  .level-character-card strong {
    color: var(--accent-primary);
    font-family: var(--font-mono);
  }

  .achievement-card {
    padding: var(--space-4);
    display: grid;
    gap: var(--space-3);
  }

  .achievement-card.complete {
    border-color: var(--accent-primary);
  }

  .achievement-card-top,
  .achievement-meta {
    display: flex;
    justify-content: space-between;
    gap: var(--space-3);
  }

  .achievement-tier {
    font-size: 11px;
    color: var(--text-secondary);
  }

  .achievement-card h3 {
    margin: 2px 0 0;
    color: var(--text-primary);
  }

  .achievement-status {
    color: var(--accent-primary);
    font-family: var(--font-mono);
  }

  .achievement-bar {
    height: 10px;
    overflow: hidden;
    border: 1px solid var(--border-primary);
    background: var(--bg-primary);
  }

  .achievement-bar span {
    display: block;
    height: 100%;
    background: linear-gradient(90deg, var(--accent-secondary), var(--accent-primary));
  }

  .tier-bronze .achievement-bar span {
    background: #b87539;
  }

  .tier-silver .achievement-bar span {
    background: #c8d0d6;
  }

  .tier-gold .achievement-bar span {
    background: #d7a837;
  }

  .tier-legendary .achievement-bar span {
    background: linear-gradient(90deg, #c93225, #f2c45b);
  }

  .achievement-meta,
  .achievement-card p,
  .backup-status {
    color: var(--text-secondary);
    font-size: 12px;
  }

  .backup-primary {
    display: flex;
    flex-wrap: wrap;
    gap: var(--space-2);
  }

  .backup-tab {
    display: grid;
    gap: var(--space-4);
  }

  .category-backup-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
    gap: var(--space-3);
  }

  .category-backup-card {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-3);
    padding: var(--space-4);
    border: 1px solid var(--border-primary);
    background: var(--bg-tertiary);
  }

  .category-backup-card div {
    display: grid;
    gap: 3px;
  }

  .category-backup-card span {
    color: var(--text-secondary);
    font-size: 12px;
  }

  .confirm-backdrop {
    position: fixed;
    inset: 0;
    z-index: 1000;
    display: grid;
    place-items: center;
    background: rgba(0, 0, 0, 0.62);
  }

  .confirm-dialog {
    width: min(420px, calc(100vw - 36px));
    display: grid;
    gap: var(--space-3);
    padding: var(--space-5);
    border: 1px solid var(--accent-primary);
    background: var(--bg-secondary);
    box-shadow: var(--shadow-lg);
  }

  .confirm-dialog h3,
  .confirm-dialog p {
    margin: 0;
  }

  .confirm-dialog p {
    color: var(--text-secondary);
  }

  .confirm-actions {
    display: flex;
    justify-content: flex-end;
    gap: var(--space-2);
  }
</style>
