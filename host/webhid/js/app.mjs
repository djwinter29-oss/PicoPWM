import { PicoPwmWebHid } from "./device.mjs?v=1";
import { renderDashboard, updateReportAge } from "./view.mjs?v=1";

const $ = selector => document.querySelector(selector);
const connectButton = $("#connect");
const connection = $("#connection");
const statusMessage = $("#status-message");
const saveButton = $("#save-configuration");
const saveDialog = $("#save-dialog");
const saveAcknowledgement = $("#save-unsafe-ack");
const confirmSave = $("#confirm-save");
const controls = ["#apply-channel", "#stop-all", "#apply-bank", "#apply-i2c", "#set-channel", "#set-freq",
  "#set-duty", "#bank-index", "#bank-backend", "#bank-role", "#i2c-input"].map($);

function render(state) {
  const dashboard = state.dashboard;
  renderDashboard(dashboard);
  const connected = ["connected", "writing"].includes(state.phase);
  const ready = state.phase === "connected" && dashboard.identity && dashboard.configuration;
  connection.dataset.state = connected ? "connected" : "disconnected";
  $("#connection-label").textContent = state.phase === "unsupported" ? "Unavailable"
    : connected ? "Connected" : state.phase === "connecting" ? "Connecting" : "Disconnected";
  connectButton.textContent = state.phase === "connecting" ? "Connecting..."
    : state.phase === "disconnecting" ? "Disconnecting..." : connected ? "Disconnect" : "Connect";
  connectButton.className = `button ${connected ? "button-danger" : "button-secondary"}`;
  connectButton.disabled = !["disconnected", "connected"].includes(state.phase);
  for (const control of controls) control.disabled = !ready;
  const clockReady = ready && dashboard.identity.can_set_clock;
  $("#apply-clock").disabled = !clockReady;
  $("#clock-profile-select").disabled = !clockReady;
  $("#clock-ack").disabled = !clockReady || Number($("#clock-profile-select").value) <= 1;
  saveButton.hidden = !dashboard.identity?.can_save;
  saveButton.disabled = !ready || !dashboard.configuration?.target_dirty;
  if (state.phase === "disconnected" && saveDialog.open) saveDialog.close();
  statusMessage.textContent = state.error ?? state.message;
  statusMessage.parentElement.dataset.error = String(Boolean(state.error) || state.phase === "unsupported");
}

const hid = globalThis.isSecureContext ? navigator.hid : undefined;
const client = new PicoPwmWebHid(hid, render);
render(client.state);

async function run(action) {
  try {
    await action();
  } catch (error) {
    if (client.state.phase !== "disconnected") client.update({ error: error.message });
  }
}

const integer = selector => Number($(selector).value.trim() === "" ? NaN : $(selector).value);

connectButton.addEventListener("click", () =>
  client.state.phase === "disconnected" ? client.connect() : client.disconnect());
$("#apply-channel").addEventListener("click", () =>
  run(() => client.setChannel(integer("#set-channel"), integer("#set-freq"), integer("#set-duty"))));
$("#stop-all").addEventListener("click", () => run(() => client.stop()));
$("#apply-bank").addEventListener("click", () =>
  run(() => client.setBank(integer("#bank-index"), integer("#bank-backend"), integer("#bank-role"))));
$("#apply-i2c").addEventListener("click", () => run(() => client.setI2cAddress(integer("#i2c-input"))));
$("#apply-clock").addEventListener("click", () =>
  run(() => client.setClockProfile(integer("#clock-profile-select"), $("#clock-ack").checked)));
$("#clock-profile-select").addEventListener("change", () => {
  $("#clock-ack").checked = false;
  render(client.state);
});
saveButton.addEventListener("click", () => {
  const overclock = client.state.dashboard.configuration?.clock_profile > 1;
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
  run(() => client.saveConfiguration(acknowledged));
});
globalThis.setInterval(() => client.refresh(), 1000);
globalThis.setInterval(() => updateReportAge(client.state.dashboard), 1000);
