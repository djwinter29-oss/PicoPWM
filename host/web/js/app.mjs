import { renderDashboard, updateReportAge } from "./view.mjs?v=1";

const $ = selector => document.querySelector(selector);
const connection = $("#connection");
const statusMessage = $("#status-message");
const saveButton = $("#save-configuration");
const saveDialog = $("#save-dialog");
const saveAcknowledgement = $("#save-unsafe-ack");
const confirmSave = $("#confirm-save");
const controls = ["#apply-channel", "#stop-all", "#apply-bank", "#apply-i2c", "#set-channel", "#set-freq",
  "#set-duty", "#bank-index", "#bank-backend", "#bank-role", "#i2c-input"].map($);

const state = { dashboard: { connected: false, channels: [], bank_errors: [], updated_at: null }, message: "Waiting for the host service", error: null, busy: false };

function render() {
  const dashboard = state.dashboard;
  renderDashboard(dashboard);
  const ready = dashboard.connected && dashboard.identity && dashboard.configuration && !state.busy;
  connection.dataset.state = dashboard.connected ? "connected" : "disconnected";
  $("#connection-label").textContent = dashboard.connected ? "Connected" : "Disconnected";
  for (const control of controls) control.disabled = !ready;
  const clockReady = ready && dashboard.identity.can_set_clock;
  $("#apply-clock").disabled = !clockReady;
  $("#clock-profile-select").disabled = !clockReady;
  $("#clock-ack").disabled = !clockReady || Number($("#clock-profile-select").value) <= 1;
  saveButton.hidden = !dashboard.identity?.can_save;
  saveButton.disabled = !ready;
  if (!dashboard.connected && saveDialog.open) saveDialog.close();
  statusMessage.textContent = state.error ?? state.message;
  statusMessage.parentElement.dataset.error = String(Boolean(state.error));
}

async function request(path, body) {
  const response = await fetch(path, body === undefined ? undefined : {
    method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body),
  });
  const data = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(data.error ?? `Request failed (${response.status})`);
  return data;
}

function accept(dashboard, message) {
  state.dashboard = { ...dashboard, updated_at: dashboard.connected ? new Date().toISOString() : null };
  state.error = dashboard.error ?? null;
  state.message = dashboard.connected ? message : "Connect the board; the host service retries every second";
  render();
}

async function refresh() {
  if (state.busy) return;
  try {
    accept(await request("/api/status"), "Connected");
  } catch (error) {
    state.dashboard = { ...state.dashboard, connected: false };
    state.error = error.message;
    render();
  }
}

async function run(path, body, message) {
  state.busy = true;
  state.error = null;
  render();
  try {
    accept(await request(path, body), message);
  } catch (error) {
    state.error = error.message;
  } finally {
    state.busy = false;
    render();
  }
}

const integer = selector => Number($(selector).value.trim() === "" ? NaN : $(selector).value);

$("#apply-channel").addEventListener("click", () =>
  run(`/api/channels/${integer("#set-channel")}`, { freq_hz: integer("#set-freq"), duty: integer("#set-duty") },
    "Channel updated"));
$("#stop-all").addEventListener("click", () => run("/api/stop", {}, "All channels stopped"));
$("#apply-bank").addEventListener("click", () =>
  run("/api/bank", { bank: integer("#bank-index"), backend: integer("#bank-backend"), role: integer("#bank-role") },
    "Bank target updated; save and restart to apply"));
$("#apply-i2c").addEventListener("click", () =>
  run("/api/i2c-address", { address: integer("#i2c-input") }, "I2C address target updated"));
$("#apply-clock").addEventListener("click", () =>
  run("/api/clock-profile", { profile: integer("#clock-profile-select"), acknowledge_overclock: $("#clock-ack").checked },
    "Clock profile target updated"));
$("#clock-profile-select").addEventListener("change", () => {
  $("#clock-ack").checked = false;
  render();
});
saveButton.addEventListener("click", () => {
  const overclock = state.dashboard.configuration?.clock_profile > 1;
  $("#save-overclock").hidden = !overclock;
  saveAcknowledgement.checked = false;
  saveAcknowledgement.disabled = !overclock;
  confirmSave.disabled = overclock;
  saveDialog.showModal();
});
saveAcknowledgement.addEventListener("change", () => { confirmSave.disabled = !saveAcknowledgement.checked; });
$("#cancel-save").addEventListener("click", () => saveDialog.close());
confirmSave.addEventListener("click", () => {
  const acknowledged = saveAcknowledgement.checked;
  saveDialog.close();
  run("/api/save", { acknowledge_overclock: acknowledged }, "Target saved; restart the board to apply");
});

render();
refresh();
globalThis.setInterval(refresh, 1000);
globalThis.setInterval(() => updateReportAge(state.dashboard), 1000);
