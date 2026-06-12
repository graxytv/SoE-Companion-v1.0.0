<script lang="ts">
    import { invoke } from "@tauri-apps/api/core";
    import { emit, listen } from "@tauri-apps/api/event";
    import { LogicalPosition, LogicalSize } from "@tauri-apps/api/dpi";
    import { getCurrentWebviewWindow } from "@tauri-apps/api/webviewWindow";
    import { onMount } from "svelte";
    import { Tabs } from "../components";
    import { windowState, itemsDictionaryStore, settingsStore, type DropTrackerStateSnapshot, type ItemsDictionary, type WindowState } from "../stores";
    import { categorizeDrop, type DropTrackerCategoryKey } from "../lib/drop-tracker-categories";
    import { materialAchievementNameFromDrop } from "../lib/achievements";
    import {
        buildHolyGrailItems,
        canonicalTrackedItemName,
        cleanTrackedItemName,
        findHolyGrailItem,
        holyGrailItemFromDrop,
        inferHolyGrailCategory,
    } from "../lib/holy-grail";
    import { materialNameFromDrop } from "../lib/item-sounds";
    import { materialTrackerNameFromDrop } from "../lib/material-tracker";
    import type { ItemDrop } from "../lib/item-drop";
    import { AchievementsTab, FateCardsTab, GeneralTab, HomeTab, LootFilterTab, OverlaysTab, SoundsTab, DropsTrackerTab, HolyGrailTab, SoeWikiTab } from "./index";

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
    const GAME_ENTRY_TRACKING_SUPPRESSION_MS = 10_000;
    const MAX_LIVE_KILL_DELTA = 50_000;
    const QUIET_SYNC_DEBOUNCE_MS = 2500;
    const SAVE_EXIT_SYNC_DEBOUNCE_MS = 250;
    const QUIET_SYNC_COOLDOWN_MS = 10_000;
    const SAVE_EXIT_CONFIRM_MS = 750;
    const SAVE_EXIT_FAST_REENTRY_MIN_MS = 0;
    const SAVE_EXIT_LATE_HOOK_PASS_MS = 1000;
    let suppressTrackingUntilMs = $state(0);
    let quietSyncBusy = $state(false);
    let quietSyncPending = $state(false);
    let quietSyncTimer: ReturnType<typeof setTimeout> | null = null;
    let quietSyncSources = new Set<string>();
    let lastQuietSyncAtMs = 0;
    let masterSyncing = $state(false);
    let saveExitSyncTimer: ReturnType<typeof setTimeout> | null = null;
    let saveExitMenuEnteredAtMs = 0;
    let syncUiActive = $derived(masterSyncing || quietSyncBusy);
    let syncUiLabel = $derived(masterSyncing ? "Syncing All" : quietSyncBusy ? "Syncing" : quietSyncPending ? "Queued" : "Sync All");

    const tabs = [
        { id: "home", label: "Home" },
        { id: "general", label: "General" },
        { id: "overlays", label: "Overlays" },
        { id: "drops-tracker", label: "Drops Tracker" },
        { id: "lootfilter", label: "Loot Filter" },
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
            if (saveExitSyncTimer) {
                const menuElapsedMs = saveExitMenuEnteredAtMs > 0 ? Date.now() - saveExitMenuEnteredAtMs : 0;
                clearTimeout(saveExitSyncTimer);
                saveExitSyncTimer = null;
                saveExitMenuEnteredAtMs = 0;
                if (menuElapsedMs >= SAVE_EXIT_FAST_REENTRY_MIN_MS) {
                    scheduleQuietSync("save-exit");
                }
            }
            suppressTrackingUntilMs = Date.now() + GAME_ENTRY_TRACKING_SUPPRESSION_MS;
        }
        if (status === "menu" && previous === "ingame") {
            if (saveExitSyncTimer) clearTimeout(saveExitSyncTimer);
            saveExitMenuEnteredAtMs = Date.now();
            saveExitSyncTimer = setTimeout(() => {
                saveExitSyncTimer = null;
                saveExitMenuEnteredAtMs = 0;
                if (gameStatus === "menu") {
                    scheduleQuietSync("save-exit");
                }
            }, SAVE_EXIT_CONFIRM_MS);
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

    interface HookDropEventsResult {
        logPath: string;
        events: ItemDrop[];
        eventIds: string[];
        linesRead: number;
        skippedProcessed: number;
        parseErrors: number;
        cursorBefore: number;
        cursorAfter: number;
        logLength: number;
        reachedLimit?: boolean;
        skippedBacklogBytes?: number;
    }

    interface HookDropCompactResult {
        logPath: string;
        oldLength: number;
        newLength: number;
        compacted: boolean;
    }

    interface CollectedDropRecordResult {
        applied: boolean;
        newGrailNotification?: ItemDrop;
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

    function emitDropTrackerStateSnapshot(): void {
        const snapshot = {
            dropsTrackerCounts: settingsStore.settings.dropsTrackerCounts,
            totalDropsTrackerCounts: settingsStore.settings.totalDropsTrackerCounts,
            dropsTrackerRecentItems: settingsStore.settings.dropsTrackerRecentItems,
            runeTrackerCounts: settingsStore.settings.runeTrackerCounts,
            materialTrackerCounts: settingsStore.settings.materialTrackerCounts,
            fateCardDropCounts: settingsStore.settings.fateCardDropCounts,
            fateCardTrackerCounts: settingsStore.settings.fateCardTrackerCounts,
            holyGrailFound: settingsStore.settings.holyGrailFound,
            achievementStats: settingsStore.settings.achievementStats,
        };
        void emit("drop-tracker-state-updated", JSON.parse(JSON.stringify(snapshot)));
    }

    function isUniqueOrSetDrop(item: ItemDrop): boolean {
        const quality = String(item.quality ?? "").toLowerCase();
        return quality === "unique" || quality === "set";
    }

    function isUnidentifiedUniqueSetDrop(item: ItemDrop): boolean {
        return isUniqueOrSetDrop(item) && !item.is_identified;
    }

    function isIdentifyInventoryEvent(item: ItemDrop): boolean {
        return String(item.source ?? "").toLowerCase() === "identify-inventory";
    }

    function hasTrustedExactUniqueSetName(item: ItemDrop): boolean {
        if (holyGrailItemFromDrop(item)) return true;
        if (!isUniqueOrSetDrop(item)) return true;
        if (!item.is_identified) return false;
        const source = String(item.source ?? "").toLowerCase();
        if (source === "grail-log") return true;
        if (cleanTrackedItemName(item.canonical_name || item.canonicalName || "")) return true;
        if (String(item.name_source ?? "").toLowerCase() === "live-tooltip") return true;
        const cleanedName = trackedItemDisplayName(item);
        return /\bhellforged\b/i.test(cleanedName);
    }

    function itemCode(item: ItemDrop): string {
        return String(item.item_code || item.itemCode || "").trim().toLowerCase();
    }

    function isPlaceholderItemName(value: unknown, item?: ItemDrop): boolean {
        const raw = String(value ?? "").trim();
        if (!raw) return true;
        const normalized = raw.toLowerCase();
        const code = item ? itemCode(item) : "";
        return (
            normalized === "item-code" ||
            normalized === "unknown item" ||
            /^item\s+#\d+$/i.test(raw) ||
            (!!code && normalized === code)
        );
    }

    function baseNameFromCode(item: ItemDrop, dict: ItemsDictionary = itemsDictionaryStore.dict): string {
        const code = itemCode(item);
        const mapped = code ? dict.base_code_names?.[code] : "";
        if (mapped && !isPlaceholderItemName(mapped, item)) return cleanTrackedItemName(mapped);
        if (item.base_name && !isPlaceholderItemName(item.base_name, item)) return cleanTrackedItemName(item.base_name);
        return "";
    }

    function qualityLabelForGenericDrop(item: ItemDrop): string {
        const quality = String(item.quality ?? "").trim().toLowerCase();
        if (item.is_runeword || item.isRuneword || quality === "runeword") return "Runeword";
        if (quality === "rare") return "Rare";
        if (quality === "magic") return "Magic";
        if (quality === "crafted") return "Crafted";
        if (quality === "set") return "Set";
        if (quality === "unique") return "Unique";
        return quality ? `${quality.charAt(0).toUpperCase()}${quality.slice(1)}` : "Dropped";
    }

    function genericDropDisplayName(item: ItemDrop): string {
        const baseName = baseNameFromCode(item);
        const label = qualityLabelForGenericDrop(item);
        return baseName ? `${label} ${baseName}` : `${label} item`;
    }

    function trackedItemDisplayName(item: ItemDrop): string {
        const codeOnlyGrailItem = holyGrailItemFromDrop(item);
        if (codeOnlyGrailItem && (item.item_code || item.itemCode || isUnidentifiedUniqueSetDrop(item))) {
            return codeOnlyGrailItem.name;
        }
        if (isUnidentifiedUniqueSetDrop(item)) {
            const baseName = baseNameFromCode(item);
            return baseName ? `Unidentified ${baseName}` : "Unidentified item";
        }
        const canonicalName = cleanTrackedItemName(item.canonical_name || item.canonicalName || "");
        if (canonicalName && !isPlaceholderItemName(canonicalName, item)) {
            return canonicalTrackedItemName(canonicalName, inferHolyGrailCategory(item));
        }
        if (item.name && !isPlaceholderItemName(item.name, item)) {
            return canonicalTrackedItemName(item.name, inferHolyGrailCategory(item));
        }
        const genericName = genericDropDisplayName(item);
        if (genericName) return genericName;
        if (item.base_name && !isPlaceholderItemName(item.base_name, item)) {
            return canonicalTrackedItemName(item.base_name, inferHolyGrailCategory(item));
        }
        return "Unknown item";
    }

    function trackingCategoriesForDrop(item: ItemDrop): DropTrackerCategoryKey[] {
        const grailItem = holyGrailItemFromDrop(item);
        if (grailItem?.category === "su") return ["unique"];
        if (grailItem?.category === "ssu") return ["hellforged"];
        if (grailItem?.category === "sets") return ["sets"];
        if (grailItem?.category === "fateCards") return ["fateCard"];
        if (grailItem?.category === "hatredOrbs") return ["hatredOrb"];
        if (grailItem?.category === "essences") return ["essence"];
        if (grailItem?.category === "ascendancy") return ["ascendancy"];
        if (hasTrustedExactUniqueSetName(item)) {
            return categorizeDrop(item);
        }
        return String(item.quality ?? "").toLowerCase() === "set" ? ["sets"] : ["unique"];
    }

    function recordCollectedDrop(
        item: ItemDrop,
        holyGrailItems = buildHolyGrailItems(itemsDictionaryStore.dict),
    ): CollectedDropRecordResult {
        if (settingsStore.settings.dropsTrackerMulingMode) return { applied: false };
        if (item.is_runeword || item.isRuneword || String(item.quality ?? "").trim().toLowerCase() === "runeword") {
            return { applied: false };
        }

        const identifyInventoryEvent = isIdentifyInventoryEvent(item);
        const grailDropItem = holyGrailItemFromDrop(item);
        let recordedNewGrailItem = false;
        if (grailDropItem && hasTrustedExactUniqueSetName(item)) {
            recordedNewGrailItem = settingsStore.recordHolyGrailDrop(item);
        }

        const categories = trackingCategoriesForDrop(item);
        const displayName = hasTrustedExactUniqueSetName(item)
            ? trackedItemDisplayName(item)
            : `Unverified ${genericDropDisplayName(item)}`;
        const trackerMaterialName = materialTrackerNameFromDrop(item);
        const matchedMaterialName = materialNameFromDrop(item);
        const materialName =
            materialAchievementNameFromDrop(item.canonical_name || item.canonicalName) ??
            materialAchievementNameFromDrop(item.base_name) ??
            materialAchievementNameFromDrop(displayName) ??
            materialAchievementNameFromDrop(matchedMaterialName);
        const fateCardName = grailDropItem?.category === "fateCards" ? grailDropItem.name : null;
        const newGrailNotification = recordedNewGrailItem
            ? {
                ...item,
                name: grailDropItem?.name ?? displayName,
                base_name: item.base_name || grailDropItem?.name || displayName,
                canonical_name: grailDropItem?.name ?? item.canonical_name ?? item.canonicalName ?? displayName,
                is_new_grail: true,
                new_grail_label: "New Grail Item!",
                filter: item.filter ?? { color: "gold", sound: null, display_stats: false },
            }
            : undefined;
        if (fateCardName && !categories.includes("fateCard")) {
            categories.push("fateCard");
        }
        if (grailDropItem?.category === "hatredOrbs" && !categories.includes("hatredOrb")) {
            categories.push("hatredOrb");
        }
        if (grailDropItem?.category === "essences" && !categories.includes("essence")) {
            categories.push("essence");
        }
        if (grailDropItem?.category === "ascendancy" && !categories.includes("ascendancy")) {
            categories.push("ascendancy");
        }
        const hasTrackerWork =
            categories.length > 0 || !!materialName || !!trackerMaterialName || !!fateCardName;

        if (!identifyInventoryEvent && hasTrackerWork) {
            const debugSource = [
                item.source ?? "silent-scan",
                item.item_code || item.itemCode ? `code=${item.item_code || item.itemCode}` : null,
                item.name_source ? `name=${item.name_source}` : null,
                item.canonical_name || item.canonicalName ? `canonical=${item.canonical_name || item.canonicalName}` : null,
                item.mode != null ? `mode=${item.mode}` : null,
                item.file_index != null ? `idx=${item.file_index}` : null,
                item.seed ? `seed=${item.seed}` : null,
                item.unit_id ? `unit=${item.unit_id}` : null,
            ].filter(Boolean).join(" ");

            settingsStore.recordLiveDrop({
                displayName,
                drop: item,
                categories,
                isNewGrail: recordedNewGrailItem,
                source: debugSource,
                materialAchievementName: materialName,
                fateCardName,
                trackerMaterialName,
                holyGrailItems,
            });
            return { applied: true, newGrailNotification };
        }

        if (recordedNewGrailItem) {
            settingsStore.recordGrailOnlyRecentItem(trackedItemDisplayName(item), categorizeDrop(item), {
                source: "identify-inventory grail-only",
            });
            return { applied: true, newGrailNotification };
        }

        return { applied: false };
    }

    async function readHookLoggedDrops(reason: string): Promise<HookDropEventsResult | null> {
        try {
            return await invoke<HookDropEventsResult>("read_hook_drop_events", {
                processedIds: settingsStore.settings.processedHookDropIds,
                cursor: settingsStore.settings.hookDropLogCursor,
                skipExisting: false,
            });
        } catch (error) {
            console.warn(`[MainWindow] Failed to read hook drop events for ${reason}:`, error);
            return null;
        }
    }

    async function baselineHookDropLogCursor(): Promise<void> {
        if (settingsStore.settings.hookDropLogCursor > 0) return;
        try {
            const result = await invoke<HookDropEventsResult>("read_hook_drop_events", {
                processedIds: [],
                cursor: 0,
                skipExisting: true,
            });
            if (result.cursorAfter > 0) {
                settingsStore.setHookDropLogCursor(result.cursorAfter);
                console.info(
                    `[MainWindow] Hook drop log baselined at ${result.cursorAfter} bytes; existing backlog was not imported.`,
                );
            }
        } catch (error) {
            console.warn("[MainWindow] Failed to baseline hook drop log cursor:", error);
        }
    }

    async function compactHookDropLog(cursor: number): Promise<void> {
        if (gameStatus === "ingame" || cursor <= 0) return;
        try {
            const result = await invoke<HookDropCompactResult>("compact_hook_drop_log", { cursor });
            if (result.compacted) {
                settingsStore.setHookDropLogCursor(0);
            }
        } catch (error) {
            console.warn("[MainWindow] Failed to compact hook drop log:", error);
        }
    }

    async function advanceHookDropCursorIfClean(
        hookDrops: HookDropEventsResult | null,
        hookProcessingFailed: boolean,
    ): Promise<void> {
        if (
            !hookDrops ||
            hookProcessingFailed ||
            (hookDrops.cursorAfter <= hookDrops.cursorBefore && !hookDrops.skippedBacklogBytes)
        ) {
            return;
        }
        settingsStore.setHookDropLogCursor(hookDrops.cursorAfter);
        await compactHookDropLog(hookDrops.cursorAfter);
    }

    async function flushCollectedDrops(reason: string): Promise<number> {
        let drops: ItemDrop[] = [];
        try {
            drops = await invoke<ItemDrop[]>("drain_collected_drops");
        } catch (error) {
            console.warn(`[MainWindow] Failed to drain collected drops for ${reason}:`, error);
        }

        const hookDrops = await readHookLoggedDrops(reason);
        if (hookDrops?.parseErrors) {
            console.warn(`[MainWindow] Hook drop log had ${hookDrops.parseErrors} parse error(s):`, hookDrops.logPath);
        }
        if (hookDrops?.skippedBacklogBytes) {
            console.warn(
                `[MainWindow] Skipped ${(hookDrops.skippedBacklogBytes / 1024 / 1024).toFixed(1)} MB of old hook log backlog before ${reason}.`,
            );
        }
        if (hookDrops?.reachedLimit) {
            console.warn(
                `[MainWindow] Hook drop sync hit a safety limit for ${reason}; remaining entries will be handled by a later sync.`,
            );
        }

        const hookEvents = hookDrops?.events ?? [];
        if (drops.length === 0 && hookEvents.length === 0) {
            await advanceHookDropCursorIfClean(hookDrops, false);
            return 0;
        }
        const holyGrailItems = buildHolyGrailItems(itemsDictionaryStore.dict);
        let applied = 0;
        const newGrailNotifications: ItemDrop[] = [];
        for (const drop of drops) {
            try {
                const result = recordCollectedDrop(drop, holyGrailItems);
                if (result.applied) applied += 1;
                if (result.newGrailNotification) newGrailNotifications.push(result.newGrailNotification);
            } catch (error) {
                console.warn("[MainWindow] Failed to apply collected drop:", drop, error);
            }
        }
        const processedHookEventIds: string[] = [];
        let hookProcessingFailed = false;
        for (const [index, drop] of hookEvents.entries()) {
            try {
                const result = recordCollectedDrop(drop, holyGrailItems);
                const eventId = hookDrops?.eventIds[index];
                if (eventId) processedHookEventIds.push(eventId);
                if (result.applied) {
                    applied += 1;
                }
                if (result.newGrailNotification) newGrailNotifications.push(result.newGrailNotification);
            } catch (error) {
                hookProcessingFailed = true;
                console.warn("[MainWindow] Failed to apply hook drop:", drop, error);
            }
        }
        if (processedHookEventIds.length) {
            settingsStore.addProcessedHookDropIds(processedHookEventIds);
        }
        await advanceHookDropCursorIfClean(hookDrops, hookProcessingFailed);

        if (applied > 0) {
            await emit("holy-grail-found-updated", JSON.parse(JSON.stringify(settingsStore.settings.holyGrailFound)));
            emitDropTrackerStateSnapshot();
            for (const drop of newGrailNotifications) {
                void emit("holy-grail-sync-notification", drop).catch((error) => {
                    console.warn("[MainWindow] Failed to emit synced grail notification:", error);
                });
            }
        }
        return applied;
    }

    function delay(ms: number): Promise<void> {
        return new Promise((resolve) => setTimeout(resolve, ms));
    }

    async function syncEverything(): Promise<void> {
        if (masterSyncing || quietSyncBusy) return;
        if (quietSyncTimer) {
            clearTimeout(quietSyncTimer);
            quietSyncTimer = null;
        }
        quietSyncSources.clear();
        quietSyncPending = false;
        masterSyncing = true;
        const failed: string[] = [];

        try {
            await flushCollectedDrops("Sync All");

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
            if (quietSyncSources.size > 0 && !quietSyncTimer) {
                armQuietSyncTimer();
            }
        }
    }

    async function syncFromQuietTrigger(sources: string[] = []): Promise<void> {
        if (quietSyncBusy || masterSyncing) {
            for (const source of sources) quietSyncSources.add(source);
            quietSyncPending = quietSyncSources.size > 0;
            return;
        }
        quietSyncBusy = true;
        const label = sources.length > 0 ? sources.join(", ") : "quiet trigger";
        try {
            await flushCollectedDrops(label);
            if (sources.includes("save-exit")) {
                await delay(SAVE_EXIT_LATE_HOOK_PASS_MS);
                await flushCollectedDrops(`${label} late hook pass`);
            }
            try {
                const result = await invoke<AccountStatsSyncResult>("sync_accountstats_stash", {
                    stashPath: settingsStore.settings.runewordPlannerStashPath,
                });
                mergeAccountStatsSyncResult(result);
            } catch (error) {
                console.warn(`[MainWindow] ${label} account-stats sync failed:`, error);
            }
        } finally {
            evaluateAchievementUnlocks();
            lastQuietSyncAtMs = Date.now();
            quietSyncBusy = false;
            if (quietSyncSources.size > 0 && !quietSyncTimer && !masterSyncing) {
                armQuietSyncTimer();
            }
        }
    }

    function isPriorityQuietSyncSource(source: string): boolean {
        return source === "save-exit";
    }

    function armQuietSyncTimer(): void {
        if (quietSyncBusy || masterSyncing || quietSyncTimer) return;
        const sources = [...quietSyncSources];
        const hasPrioritySource = sources.some(isPriorityQuietSyncSource);
        const now = Date.now();
        if (!hasPrioritySource && lastQuietSyncAtMs > 0 && now - lastQuietSyncAtMs < QUIET_SYNC_COOLDOWN_MS) {
            quietSyncSources.clear();
            quietSyncPending = false;
            return;
        }

        quietSyncPending = true;
        quietSyncTimer = setTimeout(() => {
            const pendingSources = [...quietSyncSources];
            quietSyncSources.clear();
            quietSyncTimer = null;
            quietSyncPending = false;
            void syncFromQuietTrigger(pendingSources);
        }, hasPrioritySource ? SAVE_EXIT_SYNC_DEBOUNCE_MS : QUIET_SYNC_DEBOUNCE_MS);
    }

    function scheduleQuietSync(source: string): void {
        quietSyncSources.add(source);
        quietSyncPending = true;
        armQuietSyncTimer();
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
        void baselineHookDropLogCursor();

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
            if (quietSyncTimer) clearTimeout(quietSyncTimer);
            if (saveExitSyncTimer) clearTimeout(saveExitSyncTimer);
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
                    class:syncing={syncUiActive}
                    class:queued={quietSyncPending && !syncUiActive}
                    type="button"
                    disabled={syncUiActive}
                    aria-busy={syncUiActive}
                    title={syncUiActive ? "SoE Companion is syncing data" : "Sync shared stash, Fate Cards, account stats, and character levels"}
                    onclick={syncEverything}
                >
                    {#if syncUiActive}
                        <span class="sync-spinner" aria-hidden="true"></span>
                    {/if}
                    <strong>{syncUiLabel}</strong>
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
                    <GeneralTab />
                {:else if tab === "overlays"}
                    <OverlaysTab />
                {:else if tab === "lootfilter"}
                    <LootFilterTab />
                {:else if tab === "sounds"}
                    <SoundsTab />
                {:else if tab === "achievements"}
                    <AchievementsTab />
                {:else if tab === "drops-tracker"}
                    <DropsTrackerTab bind:activeSubTab={dropsTrackerSubTab} />
                {:else if tab === "holy-grail"}
                    <HolyGrailTab bind:activeSubTab={holyGrailSubTab} />
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
        gap: 7px;
        min-width: 116px;
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
        opacity: 1;
    }

    .sync-card.syncing {
        border-color: #fff0a8;
        background: linear-gradient(180deg, #fff0a8 0%, #f0bd36 100%);
        box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.55), 0 0 18px rgba(255, 202, 72, 0.36);
        animation: sync-pulse 1.1s ease-in-out infinite;
    }

    .sync-card.queued {
        border-color: #ffd978;
        background: linear-gradient(180deg, #ffe08a 0%, #eba638 100%);
        box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.5), 0 0 12px rgba(255, 178, 42, 0.22);
    }

    .sync-spinner {
        width: 13px;
        height: 13px;
        border: 2px solid rgba(0, 0, 0, 0.24);
        border-top-color: #000;
        border-radius: 50%;
        flex: 0 0 auto;
        animation: sync-spin 0.75s linear infinite;
    }

    .sync-card strong {
        color: inherit;
        font-family: var(--font-display);
        font-size: 13px;
        line-height: 1;
        text-transform: uppercase;
        letter-spacing: 0;
    }

    @keyframes sync-spin {
        to {
            transform: rotate(360deg);
        }
    }

    @keyframes sync-pulse {
        0%, 100% {
            filter: brightness(1);
        }
        50% {
            filter: brightness(1.12);
        }
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
