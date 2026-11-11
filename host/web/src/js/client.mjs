// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

function emptyDashboard() {
  return { connected: false, identity: null, configuration: null, runtime: null,
    channels: [], bank_errors: [null, null, null], updated_at: null };
}

export class RequestError extends Error {
  constructor(message, status = null, uncertain = false, cause) {
    super(message, { cause });
    this.status = status;
    this.uncertain = uncertain;
  }
}

let authenticating = null;

async function authenticate() {
  if (!authenticating) {
    authenticating = Promise.resolve().then(async () => {
      const token = globalThis.prompt?.("This dashboard requires PICOPWM_TOKEN");
      if (!token) throw new RequestError("unauthorized", 401);
      await request("/api/login", { token });
    }).finally(() => { authenticating = null; });
  }
  return authenticating;
}

export async function request(path, body, { signal, attempt = 0 } = {}) {
  signal?.throwIfAborted();
  const controller = new AbortController();
  const abort = () => controller.abort(signal.reason);
  signal?.addEventListener("abort", abort, { once: true });
  const timeout = setTimeout(() => controller.abort(
    new RequestError("Host request timed out", null, body !== undefined)), body === undefined ? 5000 : 20000);
  let response;
  let onAbort;
  try {
    const cancellation = new Promise((_, reject) => {
      onAbort = () => reject(controller.signal.reason);
      controller.signal.addEventListener("abort", onAbort, { once: true });
    });
    return await Promise.race([cancellation, (async () => {
      response = await fetch(path, body === undefined ? { cache: "no-store", signal: controller.signal } : {
        method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body),
        signal: controller.signal,
      });
      controller.signal.throwIfAborted();
      if (response.status === 401 && attempt === 0 && path.split("?")[0] !== "/api/login") {
        await authenticate();
        controller.signal.throwIfAborted();
        return request(path, body, { signal: controller.signal, attempt: attempt + 1 });
      }
      let data;
      try {
        data = await response.json();
      } catch (error) {
        throw new RequestError(response.ok ? `Invalid JSON response (${response.status})`
          : `Request failed (${response.status}); response is not JSON`, response.status,
        body !== undefined && (response.ok || response.status >= 500), error);
      }
      if (!response.ok) {
        throw new RequestError(data?.error ?? data?.detail ?? `Request failed (${response.status})`,
          response.status, body !== undefined && response.status >= 500);
      }
      return data;
    })()]);
  } catch (error) {
    if (error instanceof RequestError || signal?.aborted) throw error;
    throw new RequestError(error.message, response?.status ?? null, body !== undefined, error);
  } finally {
    clearTimeout(timeout);
    signal?.removeEventListener("abort", abort);
    controller.signal.removeEventListener("abort", onAbort);
  }
}

/** HTTP transport with page-local selection and stale-response protection. */
export class PicoPwmHost {
  #statusController = null;

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
    this.#statusController?.abort();
    this.#statusController = null;
    this.refreshing = false;
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
    const controller = new AbortController();
    this.#statusController = controller;
    this.refreshing = true;
    try {
      const dashboard = await request(`/api/status?board=${encodeURIComponent(board)}`, undefined,
        { signal: controller.signal });
      if (generation !== this.generation) return;
      this.update({ phase: dashboard.connected ? "connected" : "disconnected",
        dashboard: { ...dashboard, updated_at: dashboard.connected ? new Date().toISOString() : null },
        error: dashboard.error ?? null,
        message: dashboard.connected ? "Connected" : "Selected board unavailable; retrying every second" });
    } catch (error) {
      if (generation === this.generation)
        this.update({ phase: "disconnected", dashboard: emptyDashboard(), error: error.message });
    } finally {
      if (this.#statusController === controller) {
        this.#statusController = null;
        this.refreshing = false;
      }
    }
  }

  async action(path, body, message) {
    if (!this.board || this.state.phase !== "connected") throw new Error("Connect to a board before sending commands");
    const board = this.board;
    this.generation++;
    this.#statusController?.abort();
    this.#statusController = null;
    this.refreshing = false;
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
      if (error.uncertain) {
        const unconfirmed = new RequestError(
          `${error.message}. Command outcome is unconfirmed; verify fresh board status before retrying.`,
          error.status, true, error);
        this.update({ phase: "disconnected", dashboard: emptyDashboard(), error: unconfirmed.message,
          message: "Waiting for fresh board status; the command may have taken effect" });
        throw unconfirmed;
      }
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
