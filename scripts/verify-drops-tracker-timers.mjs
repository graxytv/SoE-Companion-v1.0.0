import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

const sourcePath = resolve("src/stores/settings.svelte.ts");
const source = readFileSync(sourcePath, "utf8");

assert.match(
  source,
  /private normalizeSettings\(\s*settings: AppSettings,\s*options: \{ resetTimerBaseline\?: boolean \} = \{\},\s*\): AppSettings \{\s*const resetTimerBaseline = options\.resetTimerBaseline \?\? false;/s,
  "normalizeSettings must preserve timer baselines by default",
);

assert.match(
  source,
  /dropsTrackerTimerLastTickAtMs: resetTimerBaseline\s*\?\s*Date\.now\(\)\s*:\s*this\.normalizeTimerStart\(settings\.dropsTrackerTimerLastTickAtMs\)/s,
  "normalizeSettings must only reset dropsTrackerTimerLastTickAtMs when explicitly requested",
);

assert.match(
  source,
  /this\.normalizeSettings\(\{\s*\.\.\.DEFAULT_SETTINGS,\s*\.\.\.loaded,\s*\}, \{ resetTimerBaseline: true \}\)/s,
  "initial settings load must reset the timer baseline",
);

assert.doesNotMatch(
  source,
  /const merged: AppSettings = this\.normalizeSettings\(\{\s*\.\.\.DEFAULT_SETTINGS,\s*\.\.\.saved,\s*\}, \{ resetTimerBaseline: true \}\)/s,
  "settings save merges must not reset the timer baseline",
);

const normalizeElapsedMs = (value) =>
  typeof value === "number" && Number.isFinite(value) && value > 0
    ? Math.floor(value)
    : 0;

const normalizeTimerStart = (value, now) =>
  typeof value === "number" && Number.isFinite(value) && value > 0 ? value : now;

const normalizeSettings = (settings, { resetTimerBaseline = false, now }) => ({
  ...settings,
  dropsTrackerRunElapsedMs: normalizeElapsedMs(settings.dropsTrackerRunElapsedMs),
  dropsTrackerSessionElapsedMs: normalizeElapsedMs(settings.dropsTrackerSessionElapsedMs),
  dropsTrackerTimerLastTickAtMs: resetTimerBaseline
    ? now
    : normalizeTimerStart(settings.dropsTrackerTimerLastTickAtMs, now),
});

const tickDropsTrackerTimers = (settings, now) => {
  const lastTick = normalizeTimerStart(settings.dropsTrackerTimerLastTickAtMs, now);
  const deltaMs = Math.max(0, Math.min(now - lastTick, 60_000));
  return {
    ...settings,
    dropsTrackerRunElapsedMs: normalizeElapsedMs(settings.dropsTrackerRunElapsedMs) + deltaMs,
    dropsTrackerSessionElapsedMs:
      normalizeElapsedMs(settings.dropsTrackerSessionElapsedMs) + deltaMs,
    dropsTrackerTimerLastTickAtMs: now,
  };
};

let settings = normalizeSettings(
  {
    dropsTrackerRunElapsedMs: 0,
    dropsTrackerSessionElapsedMs: 0,
    dropsTrackerTimerLastTickAtMs: 1_000,
  },
  { resetTimerBaseline: true, now: 1_000 },
);

for (let now = 2_000; now <= 6_000; now += 1_000) {
  settings = tickDropsTrackerTimers(settings, now);
  settings = normalizeSettings(settings, { now: now + 500 });
}

assert.equal(
  settings.dropsTrackerRunElapsedMs,
  5_000,
  "five one-second ticks with debounced saves should still record five seconds",
);
assert.equal(
  settings.dropsTrackerSessionElapsedMs,
  5_000,
  "session timer should advance at the same rate as run timer",
);
assert.equal(
  settings.dropsTrackerTimerLastTickAtMs,
  6_000,
  "save normalization must preserve the last tick timestamp",
);

const oldBugSettings = normalizeSettings(settings, {
  resetTimerBaseline: true,
  now: 6_500,
});

assert.equal(
  tickDropsTrackerTimers(oldBugSettings, 7_000).dropsTrackerRunElapsedMs,
  5_500,
  "this verification should catch the old behavior where a save reset the baseline and lost 500ms",
);

console.log("drops tracker timer verification passed");
