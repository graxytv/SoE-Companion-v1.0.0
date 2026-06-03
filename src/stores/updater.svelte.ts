/**
 * Dormant updater store for SoE Companion.
 *
 * The original update flow is intentionally disabled until this project has
 * its own release channel. The module stays as a future integration point.
 */

export type UpdaterState = { kind: 'idle' };

class UpdaterStore {
  private _state = $state<UpdaterState>({ kind: 'idle' });

  get state(): UpdaterState {
    return this._state;
  }

  async initListeners(): Promise<void> {}
  destroyListeners(): void {}

  async check(_manual = false): Promise<void> {
    this._state = { kind: 'idle' };
  }

  async install(): Promise<void> {}
  async restart(): Promise<void> {}
}

export const updaterStore = new UpdaterStore();
