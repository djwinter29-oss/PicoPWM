// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

function emptyDashboard() {
  return { connected: false, identity: null, configuration: null, runtime: null,
    channels: [], bank_errors: [null, null, null], updated_at: null };
}

export async function request(path, body, attempt = 0) {
  const response = await fetch(path, body === undefined ? { cache: "no-store" } : {
    method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body),
  });
  if (response.status === 401 && attempt === 0 && path.split("?")[0] !== "/api/login") {
    const token = globalThis.prompt?.("This dashboard requires PICOPWM_TOKEN");
    if (!token) throw new Error("unauthorized");
    const login = await fetch("/api/login", {
      method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ token }),
    });
    if (!login.ok) throw new Error("unauthorized");
    return request(path, body, attempt + 1);
  }
  const data = await response.json();
  if (!response.ok) throw new Error(data.error ?? data.detail ?? `Request failed (${response.status})`);
  return data;
}

/** HTTP transport with page-local selection and stale-response protection. */
export class PicoPwmHost {
  constructor(onChange = () => {}) {
    this.onChange = onChange;
    this.board = null;
    this.generation = 0;
    this.refreshing = false;
    this.rebootUntil = 0;
    this.state = { phase: "disconnected", dashboard: emptyDashboard(), error: null, message: "Scanning boards..." };
  }

  update(changes) {
    this.state = { ...this.state, ...changes };
    this.onChange(this.state);
  }

  select(board) {
    if (this.state.phase === "writing") return;
    this.board = board || null;
    this.generation++;
    this.rebootUntil = 0;
    this.update({ phase: "disconnected", dashboard: emptyDashboard(), error: null,
      message: board ? "Connecting to selected board..." : "Select a board attached to this server" });
  }

  async refresh() {
    if (!this.board || this.refreshing || this.state.phase === "writing" || Date.now() < this.rebootUntil) return;
    const generation = this.generation;
    const board = this.board;
    this.refreshing = true;
    try {
      const dashboard = await request(`/api/status?board=${encodeURIComponent(board)}`);
      if (generation !== this.generation) return;
      this.update({ phase: dashboard.connected ? "connected" : "disconnected",
        dashboard: { ...dashboard, updated_at: dashboard.connected ? new Date().toISOString() : null },
        error: dashboard.error ?? null,
        message: dashboard.connected ? "Connected" : "Selected board unavailable; retrying every second" });
    } catch (error) {
      if (generation === this.generation)
        this.update({ phase: "disconnected", dashboard: emptyDashboard(), error: error.message });
    } finally {
      this.refreshing = false;
    }
  }

  async action(path, body, message) {
    if (!this.board || this.state.phase !== "connected") throw new Error("Connect to a board before sending commands");
    const board = this.board;
    this.generation++;
    this.update({ phase: "writing", error: null });
    try {
      const result = await request(`${path}?board=${encodeURIComponent(board)}`, body);
      if (result.rebooting) {
        this.rebootUntil = Date.now() + 1500;
        this.update({ phase: "disconnected", dashboard: emptyDashboard(),
          message: "Reboot command confirmed; waiting for the selected board to reconnect" });
      } else {
        this.update({ phase: result.connected ? "connected" : "disconnected",
          dashboard: { ...result, updated_at: result.connected ? new Date().toISOString() : null },
          error: result.error ?? null, message });
      }
    } catch (error) {
      this.update({ phase: this.state.dashboard.connected ? "connected" : "disconnected", error: error.message });
      throw error;
    }
  }

  setChannel(channel, freq_hz, duty) {
    if (!Number.isInteger(channel) || channel < 0 || channel > 23
      || !Number.isInteger(freq_hz) || freq_hz < 0 || freq_hz > 0xffffffff
      || !Number.isInteger(duty) || duty < 0 || duty > 100)
      throw new Error("Choose a valid channel, frequency, and duty");
    return this.action(`/api/channels/${channel}`, { freq_hz, duty }, "Channel updated");
  }

  setLed(on) { return this.action("/api/led", { on }, "Board LED updated"); }
  reboot() { return this.action("/api/reboot", {}, "Board restarting"); }
  saveAndReboot(settings, acknowledged) {
    return this.action("/api/save-reboot", {
      profile: settings.clockProfile, address: settings.i2cAddress, roles: settings.roles,
      acknowledge_overclock: acknowledged,
    }, "Configuration saved; board restarting");
  }
}
