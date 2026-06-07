<script lang="ts">
    import { invoke } from "@tauri-apps/api/core";
    import { emit, listen } from "@tauri-apps/api/event";
    import { LogicalPosition, LogicalSize } from "@tauri-apps/api/dpi";
    import { getCurrentWebviewWindow } from "@tauri-apps/api/webviewWindow";
    import { onMount } from "svelte";
    import { Tabs } from "../components";
    import { windowState, itemsDictionaryStore, settingsStore, type DropTrackerStateSnapshot, type WindowState } from "../stores";
    import { categorizeDrop } from "../lib/drop-tracker-categories";
    import { buildHolyGrailItems, findHolyGrailItem } from "../lib/holy-grail";
    import { AchievementsTab, FateCardsTab, GeneralTab, HomeTab, LootFilterTab, NotificationsTab, SoundsTab, DropsTrackerTab, HolyGrailTab, SoeWikiTab } from "./index";

    let scannerStatus = $state<
        "stopped" | "starting" | "running" | "stopping" | "error"
    >("stopped");
    let gameStatus = $state<"unknown" | "ingame" | "menu">("unknown");

    let activeTab = $state("home");
    let dropsTrackerSubTab = $state("overview");
    let holyGrailSubTab = $state("overview");

    interface AccountStatsSyncResult {
        totalKills: number;
        bossKills: Record<string, number>;
        matchedText: string;
    }

    interface CharacterLevelSyncEntry {
        name: string;
        class_name: string;
        level: number;
        path: string;
    }

    interface CharacterLevelSyncResult {
        characters: CharacterLevelSyncEntry[];
        scanned_dirs: string[];
        message: string;
    }

    // Grail drop log watcher state
    let grailDropCount = $state(0);
    const GAME_ENTRY_TRACKING_SUPPRESSION_MS = 10_000;
    const MAX_LIVE_KILL_DELTA = 50_000;
    const FATE_CARD_BACKGROUND_SYNC_INTERVAL_MS = 30 * 1000;
    let suppressTrackingUntilMs = $state(0);
    let fateCardBackgroundSyncBusy = false;
    let masterSyncing = $state(false);

    const tabs = [
        { id: "home", label: "Home" },
        { id: "general", label: "General" },
        { id: "drops-tracker", label: "Drops Tracker" },
        { id: "lootfilter", label: "Loot Filter" },
        { id: "notifications", label: "Notifications" },
        { id: "sounds", label: "Sounds" },
        { id: "achievements", label: "Achievements" },
        { id: "holy-grail", label: "Holy Grail" },
        { id: "fate-cards", label: "Fate Cards" },
        { id: "soe-wiki", label: "SoE Wiki" },
    ];

    let orderedTabs = $derived(applyTabOrder(tabs, settingsStore.settings.mainTabOrder));

    function applyTabOrder<T extends { id: string }>(items: T[], order: string[]): T[] {
        const byId = new Map(items.map((item) => [item.id, item]));
        const ordered: T[] = [];
        const seen = new Set<string>();
        for (const id of order) {
            const item = byId.get(id);
            if (!item || seen.has(id)) continue;
            ordered.push(item);
            seen.add(id);
        }
        for (const item of items) {
            if (!seen.has(item.id)) ordered.push(item);
        }
        return ordered;
    }

    function getGameStatusText(): string {
        switch (gameStatus) {
            case "ingame": return "In Game";
            case "menu": return "Menu";
            default: return "Not Found";
        }
    }

    function setGameStatus(status: unknown): void {
        if (status !== "ingame" && status !== "menu" && status !== "unknown") return;
        const previous = gameStatus;
        gameStatus = status;
        if (status === "ingame" && previous !== "ingame") {
            suppressTrackingUntilMs = Date.now() + GAME_ENTRY_TRACKING_SUPPRESSION_MS;
        }
    }

    function isGameEntryTrackingSuppressed(): boolean {
        return Date.now() < suppressTrackingUntilMs;
    }

    async function openExternalUrl(url: string): Promise<void> {
        try {
            await invoke("open_external_url", { url });
        } catch (error) {
            console.error("[MainWindow] Failed to open external URL:", error);
        }
    }

    function handleGrailLogDrop(drop: { itemName: string; quality: string }): void {
        const name = drop.itemName?.trim();
        const quality = drop.quality?.trim();
        if (!name || !quality || settingsStore.settings.dropsTrackerMulingMode) return;

        const item = { name, quality, is_runeword: quality.toLowerCase() === 'runeword' };
        const grailItem = findHolyGrailItem(name);
        const grailEligible = grailItem?.category !== 'fateCards';
        const isNewGrailItem = !!grailItem && grailEligible && !settingsStore.settings.holyGrailFound[grailItem.key];
        if (grailItem && isNewGrailItem) {
            settingsStore.setHolyGrailFound(grailItem.key, grailItem.name, grailItem.category, true);
        }
        const categories = categorizeDrop(item);
        settingsStore.recordDropTrackerItem(name, categories, { isNewGrail: isNewGrailItem, source: "grail-log" });
        settingsStore.evaluateAchievementUnlocks({
            holyGrailFound: settingsStore.settings.holyGrailFound,
            holyGrailItems: buildHolyGrailItems(itemsDictionaryStore.dict),
            runeTrackerCounts: settingsStore.settings.runeTrackerCounts,
        });
    }

    function handleAccountStatsUpdate(result: AccountStatsSyncResult): void {
        const currentStats = settingsStore.settings.achievementStats;
        const incomingKills = Number(result.totalKills) || 0;
        const incomingBossKills = result.bossKills ?? {};
        const isKillOnlyLiveUpdate = Object.keys(incomingBossKills).length === 0;
        if (isKillOnlyLiveUpdate) {
            const currentKills = Number(currentStats.totalKills) || 0;
            if (incomingKills < currentKills || incomingKills > currentKills + MAX_LIVE_KILL_DELTA) {
                console.warn("[MainWindow] Ignored implausible live kill total", {
                    currentKills,
                    incomingKills,
                    matchedText: result.matchedText,
                });
                return;
            }
            settingsStore.updateAchievementStats({ totalKills: incomingKills });
            settingsStore.evaluateAchievementUnlocks({
                holyGrailFound: settingsStore.settings.holyGrailFound,
                holyGrailItems: buildHolyGrailItems(itemsDictionaryStore.dict),
                runeTrackerCounts: settingsStore.settings.runeTrackerCounts,
            });
            return;
        }

        const mergedBossKills = { ...currentStats.bossKills };
        for (const [key, value] of Object.entries(incomingBossKills)) {
            mergedBossKills[key] = Math.max(Number(mergedBossKills[key]) || 0, Number(value) || 0);
        }
        settingsStore.updateAchievementStats({
            totalKills: Math.max(currentStats.totalKills ?? 0, incomingKills),
            bossKills: mergedBossKills,
        });
        settingsStore.evaluateAchievementUnlocks({
            holyGrailFound: settingsStore.settings.holyGrailFound,
            holyGrailItems: buildHolyGrailItems(itemsDictionaryStore.dict),
            runeTrackerCounts: settingsStore.settings.runeTrackerCounts,
        });
    }

    interface RuneStashSyncResult {
        counts: Partial<Record<string, number>>;
        fate_card_counts?: Partial<Record<string, number>>;
        fate_card_sync_available?: boolean;
        scanned_files: string[];
        message: string;
    }

    function evaluateAchievementUnlocks(): void {
        settingsStore.evaluateAchievementUnlocks({
            holyGrailFound: settingsStore.settings.holyGrailFound,
            holyGrailItems: buildHolyGrailItems(itemsDictionaryStore.dict),
            runeTrackerCounts: settingsStore.settings.runeTrackerCounts,
        });
    }

    function mergeAccountStatsSyncResult(result: AccountStatsSyncResult): void {
        const currentStats = settingsStore.settings.achievementStats;
        const incomingKills = Number(result.totalKills) || 0;
        const mergedBossKills = { ...currentStats.bossKills };
        for (const [key, value] of Object.entries(result.bossKills ?? {})) {
            mergedBossKills[key] = Math.max(Number(mergedBossKills[key]) || 0, Number(value) || 0);
        }
        settingsStore.updateAchievementStats({
            totalKills: Math.max(Number(currentStats.totalKills) || 0, incomingKills),
            bossKills: mergedBossKills,
        });
    }

    function applyCharacterLevelSyncResult(result: CharacterLevelSyncResult): void {
        if (result.characters.length === 0) return;
        settingsStore.updateAchievementStats({
            characterLevels: Object.fromEntries(
                result.characters.map((character) => [
                    character.name,
                    {
                        name: character.name,
                        className: character.class_name,
                        level: character.level,
                    },
                ]),
            ),
        });
    }

    async function syncEverything(): Promise<void> {
        if (masterSyncing) return;
        masterSyncing = true;
        const failed: string[] = [];

        try {
            try {
                const result = await invoke<RuneStashSyncResult>("sync_shared_stash_runes", {
                    stashPath: settingsStore.settings.runewordPlannerStashPath,
                });
                settingsStore.setFateCardCounts(result.fate_card_counts ?? {});
                await emit("master-shared-stash-synced", result);
            } catch (error) {
                console.warn("[MainWindow] Master shared-stash sync failed:", error);
                failed.push("stash");
            }

            try {
                const result = await invoke<AccountStatsSyncResult>("sync_accountstats_kills", {
                    currentKills: settingsStore.settings.achievementStats.totalKills,
                    stashPath: settingsStore.settings.runewordPlannerStashPath,
                });
                mergeAccountStatsSyncResult(result);
            } catch (error) {
                console.warn("[MainWindow] Master account-stats sync failed:", error);
                failed.push("account stats");
            }

            try {
                const result = await invoke<CharacterLevelSyncResult>("sync_character_levels", {
                    stashPath: settingsStore.settings.runewordPlannerStashPath,
                });
                applyCharacterLevelSyncResult(result);
            } catch (error) {
                console.warn("[MainWindow] Master character-level sync failed:", error);
                failed.push("character levels");
            }

            evaluateAchievementUnlocks();
            if (failed.length > 0) {
                console.warn("[MainWindow] Master sync completed with failures:", failed);
            }
        } finally {
            masterSyncing = false;
        }
    }

    async function syncFateCardsInBackground(): Promise<void> {
        if (fateCardBackgroundSyncBusy) return;
        if (masterSyncing) return;
        if (activeTab === "fate-cards" || activeTab === "holy-grail") return;
        fateCardBackgroundSyncBusy = true;
        try {
            const result = await invoke<RuneStashSyncResult>("sync_shared_stash_runes", {
                stashPath: null,
            });
            settingsStore.setFateCardCounts(result.fate_card_counts ?? {});
        } catch (error) {
            console.warn("[MainWindow] Background Fate Card stash sync failed:", error);
        } finally {
            fateCardBackgroundSyncBusy = false;
        }
    }

    async function saveWindowState() {
        try {
            const window = getCurrentWebviewWindow();
            const factor = await window.scaleFactor();
            const position = await window.outerPosition();
            const size = await window.outerSize();
            const maximized = await window.isMaximized();
            const state: WindowState = {
                x: Math.round(position.x / factor),
                y: Math.round(position.y / factor),
                width: Math.round(size.width / factor),
                height: Math.round(size.height / factor),
                maximized,
            };
            await windowState.save("main", state);
        } catch (error) {
            console.error("[MainWindow] Failed to save window state:", error);
        }
    }

    async function restoreWindowState() {
        try {
            const state = await windowState.load("main");
            if (!state) return;
            const window = getCurrentWebviewWindow();
            await window.setPosition(new LogicalPosition(state.x, state.y));
            await window.setSize(new LogicalSize(state.width, state.height));
            if (state.maximized) await window.maximize();
        } catch (error) {
            console.error("[MainWindow] Failed to restore window state:", error);
        }
    }

    onMount(() => {
        const unlisteners: Array<() => void> = [];

        restoreWindowState();
        itemsDictionaryStore.init();
        void syncFateCardsInBackground();
        const fateCardSyncTimer = globalThis.setInterval(() => {
            void syncFateCardsInBackground();
        }, FATE_CARD_BACKGROUND_SYNC_INTERVAL_MS);

        // Scanner / game status (kept for overlay compatibility)
        listen<string>("scanner-status", (event) => {
            scannerStatus = event.payload as typeof scannerStatus;
        }).then((u) => unlisteners.push(u));

        listen<string>("game-status", (event) => {
            setGameStatus(event.payload);
        }).then((u) => unlisteners.push(u));

        invoke("get_scanner_status").then((running: unknown) => {
            if (running) scannerStatus = "running";
        });

        invoke("get_game_status").then((status: unknown) => {
            setGameStatus(status);
        });

        // The ijl11.dll log only contains `name|quality` and can fire for
        // non-drop item flag/name events. Keep the watcher alive for status
        // and manual import, but do not treat live log appends as trusted
        // drops. Live grail/tracker updates now come from the scanner path,
        // which verifies ground-mode items and live tooltip names.
        listen<{ itemName: string; quality: string }>("grail-drop", (event) => {
            if (isGameEntryTrackingSuppressed()) return;
            grailDropCount += 1;
            console.debug("[MainWindow] Ignored live grail log entry", event.payload);
            // Auto-navigate to the grail tab on a new drop so the user sees it
            // (comment this out if you find it annoying)
            // activeTab = "holy-grail";
        }).then((u) => unlisteners.push(u));

        listen<DropTrackerStateSnapshot>("drop-tracker-state-updated", (event) => {
            settingsStore.mergeDropTrackerStateSnapshot(event.payload);
        }).then((u) => unlisteners.push(u));

        listen<AccountStatsSyncResult>("account-stats-updated", (event) => {
            handleAccountStatsUpdate(event.payload);
        }).then((u) => unlisteners.push(u));

        const window = getCurrentWebviewWindow();
        window.onCloseRequested(async () => {
            await saveWindowState();
        }).then((u) => unlisteners.push(u));

        let saveTimeout: ReturnType<typeof setTimeout> | null = null;
        const debouncedSave = () => {
            if (saveTimeout) clearTimeout(saveTimeout);
            saveTimeout = setTimeout(saveWindowState, 1000);
        };
        window.onMoved(debouncedSave).then((u) => unlisteners.push(u));
        window.onResized(debouncedSave).then((u) => unlisteners.push(u));

        return () => {
            globalThis.clearInterval(fateCardSyncTimer);
            if (saveTimeout) clearTimeout(saveTimeout);
            unlisteners.forEach((u) => u());
            itemsDictionaryStore.destroy();
        };
    });
</script>

<main class="main-window">
    <header class="header">
        <div class="brand">
            <h1 class="title">SoE<span class="accent">Companion</span></h1>
            <span class="version">v{__APP_VERSION__}</span>
        </div>

        <div class="header-right">
            <div class="master-sync">
                <button
                    class="sync-card"
                    type="button"
                    disabled={masterSyncing}
                    title="Sync shared stash, Fate Cards, account stats, and character levels"
                    onclick={syncEverything}
                >
                    <strong>{masterSyncing ? "Syncing" : "Sync All"}</strong>
                </button>
            </div>

            <div class="status-bar">
                <div class="status-item">
                    <span class="status-label">Diablo II</span>
                    <span
                        class="status-value"
                        style:color={gameStatus === "ingame"
                            ? "var(--status-success-text)"
                            : "var(--text-muted)"}
                    >
                        {getGameStatusText()}
                    </span>
                </div>

            </div>
        </div>
    </header>

    <div class="content">
        <Tabs tabs={orderedTabs} bind:activeTab onReorder={(ids) => settingsStore.setMainTabOrder(ids)}>
            {#snippet children(tab)}
                {#if tab === "home"}
                    <HomeTab {gameStatus} {scannerStatus} onNavigate={(tabId) => { activeTab = tabId; }} />
                {:else if tab === "general"}
                    <GeneralTab {gameStatus} />
                {:else if tab === "lootfilter"}
                    <LootFilterTab />
                {:else if tab === "notifications"}
                    <NotificationsTab />
                {:else if tab === "sounds"}
                    <SoundsTab />
                {:else if tab === "achievements"}
                    <AchievementsTab />
                {:else if tab === "drops-tracker"}
                    <DropsTrackerTab bind:activeSubTab={dropsTrackerSubTab} />
                {:else if tab === "holy-grail"}
                    <HolyGrailTab bind:activeSubTab={holyGrailSubTab} grailDropCount={grailDropCount} />
                {:else if tab === "fate-cards"}
                    <FateCardsTab />
                {:else if tab === "soe-wiki"}
                    <SoeWikiTab />
                {/if}
            {/snippet}
        </Tabs>
    </div>

    <footer class="footer">
        <span class="footer-text">Created by Graxy_TV</span>
        <button class="donate-card" type="button" onclick={() => openExternalUrl("https://www.own3d.pro/u/graxy_tv/tip")}>
            <strong>Donate</strong>
        </button>
        <div class="footer-socials" aria-label="Graxy_TV links">
            <button type="button" title="YouTube" aria-label="YouTube" onclick={() => openExternalUrl("https://www.youtube.com/@graxy_tv")}>
                <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M21.6 7.2a3 3 0 0 0-2.1-2.1C17.6 4.6 12 4.6 12 4.6s-5.6 0-7.5.5a3 3 0 0 0-2.1 2.1C2 9.1 2 12 2 12s0 2.9.4 4.8a3 3 0 0 0 2.1 2.1c1.9.5 7.5.5 7.5.5s5.6 0 7.5-.5a3 3 0 0 0 2.1-2.1c.4-1.9.4-4.8.4-4.8s0-2.9-.4-4.8ZM10 15.5v-7l6 3.5-6 3.5Z"/></svg>
            </button>
            <button type="button" title="Twitch" aria-label="Twitch" onclick={() => openExternalUrl("https://www.twitch.tv/graxy_tv")}>
                <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 3h17v11.5L16.5 19H13l-2.5 2.5H8V19H4V3Zm2 2v12h4v2l2-2h4l3-3V5H6Zm5 3h2v5h-2V8Zm5 0h2v5h-2V8Z"/></svg>
            </button>
            <button type="button" title="Discord" aria-label="Discord" onclick={() => openExternalUrl("https://discord.gg/5zktAk9ct8")}>
                <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M19.6 5.3A16 16 0 0 0 15.7 4l-.2.4-.3.6a14.8 14.8 0 0 0-6.4 0l-.3-.6L8.3 4a16 16 0 0 0-3.9 1.3C2 8.9 1.3 12.4 1.6 15.9A15.8 15.8 0 0 0 6.4 18.3l.6-.9.5-.9c-.9-.3-1.7-.7-2.4-1.2l.6-.5c4.6 2.1 9.6 2.1 14.1 0l.6.5c-.8.5-1.6.9-2.4 1.2l.5.9.6.9a15.8 15.8 0 0 0 4.8-2.4c.4-4.1-.7-7.6-2.5-10.6ZM8.5 14c-.9 0-1.6-.8-1.6-1.8s.7-1.8 1.6-1.8 1.6.8 1.6 1.8S9.4 14 8.5 14Zm7 0c-.9 0-1.6-.8-1.6-1.8s.7-1.8 1.6-1.8 1.6.8 1.6 1.8-.7 1.8-1.6 1.8Z"/></svg>
            </button>
        </div>
    </footer>
</main>

<style>
    .main-window {
        display: flex;
        flex-direction: column;
        height: 100vh;
        background: var(--bg-primary);
        overflow: hidden;
    }

    .header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: var(--space-3) var(--space-4);
        background: var(--bg-secondary);
        border-bottom: 1px solid var(--border-primary);
    }

    .brand {
        display: flex;
        align-items: baseline;
        gap: var(--space-2);
    }

    .title {
        font-family: var(--font-display);
        font-size: 28px;
        font-weight: 400;
        color: var(--text-primary);
        margin: 0;
        letter-spacing: 0;
        line-height: 1;
        text-shadow: 0 1px 8px color-mix(in srgb, var(--accent-secondary) 28%, transparent);
    }

    .accent {
        color: var(--accent-primary);
    }

    .version {
        font-family: var(--font-mono);
        font-size: var(--text-xs);
        color: var(--text-muted);
    }

    .header-right {
        display: flex;
        align-items: center;
        gap: var(--space-3);
    }

    .status-bar {
        display: flex;
        align-items: center;
        gap: var(--space-3);
        height: 36px;
        padding: 0 var(--space-3);
        background: var(--bg-tertiary);
        border-radius: var(--radius-md);
    }

    .master-sync {
        display: flex;
        align-items: center;
        justify-content: center;
    }

    .sync-card {
        display: flex;
        align-items: center;
        justify-content: center;
        min-width: 96px;
        height: 36px;
        padding: 0 var(--space-3);
        border: 1px solid #f8d36a;
        border-radius: var(--radius-md);
        background: linear-gradient(180deg, #ffd45f 0%, #e99d24 100%);
        color: #000;
        cursor: pointer;
        text-align: center;
        box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.45), 0 0 14px rgba(255, 178, 42, 0.16);
    }

    .sync-card:hover:not(:disabled) {
        border-color: #ffe89b;
        background: linear-gradient(180deg, #ffe17d 0%, #f2ae35 100%);
        color: #000;
    }

    .sync-card:disabled {
        cursor: wait;
        opacity: 0.7;
    }

    .sync-card strong {
        color: inherit;
        font-family: var(--font-display);
        font-size: 13px;
        line-height: 1;
        text-transform: uppercase;
        letter-spacing: 0;
    }

    .donate-card {
        display: flex;
        align-items: center;
        justify-content: center;
        min-width: 64px;
        height: 18px;
        padding: 0 8px;
        border: 1px solid #77d8ff;
        border-radius: var(--radius-sm);
        background: linear-gradient(180deg, #c6f0ff 0%, #8ddfff 100%);
        color: #06121a;
        cursor: pointer;
        text-align: center;
        box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.72), 0 0 8px rgba(120, 210, 255, 0.12);
    }

    .donate-card:hover {
        border-color: #b8efff;
        background: linear-gradient(180deg, #dcf8ff 0%, #a6e9ff 100%);
        color: #06121a;
    }

    .donate-card strong {
        color: inherit;
        font-family: "Brush Script MT", "Segoe Script", cursive;
        font-size: 13px;
        font-weight: 900;
        line-height: 1;
        text-shadow: 0 1px 0 rgba(255, 255, 255, 0.65);
    }

    .status-item {
        display: flex;
        flex-direction: column;
        align-items: center;
        gap: 1px;
        justify-content: center;
    }

    .status-label {
        font-size: 9px;
        font-weight: 500;
        color: var(--text-muted);
        text-transform: uppercase;
        letter-spacing: 0.5px;
        line-height: 1;
    }

    .status-value {
        font-family: var(--font-mono);
        font-size: 11px;
        font-weight: 600;
        line-height: 1;
    }

    .content {
        flex: 1;
        min-height: 0;
        padding: var(--space-3) var(--space-4);
        display: flex;
        flex-direction: column;
        overflow: hidden;
    }

    .footer {
        display: flex;
        align-items: center;
        justify-content: flex-end;
        gap: 8px;
        padding: var(--space-1) var(--space-4) var(--space-2);
        background: var(--bg-secondary);
        border-top: 1px solid var(--border-primary);
    }

    .footer-text {
        font-size: var(--text-xs);
        color: var(--text-muted);
        line-height: 1;
    }

    .footer-socials {
        display: flex;
        align-items: center;
        gap: 4px;
    }

    .footer-socials button {
        display: grid;
        place-items: center;
        width: 20px;
        height: 20px;
        padding: 0;
        border: 1px solid transparent;
        border-radius: var(--radius-sm);
        background: transparent;
        color: var(--text-muted);
        cursor: pointer;
    }

    .footer-socials button:hover {
        border-color: var(--border-primary);
        color: var(--accent-primary);
        background: var(--bg-tertiary);
    }

    .footer-socials svg {
        width: 14px;
        height: 14px;
        fill: currentColor;
    }
</style>
