import { CLOCK_PROFILES, ROLES, PicoPwmWebHid } from "./device.mjs?v=6";
import { renderBootEdits, renderDashboard, updateReportAge } from "./view.mjs?v=12";

const $ = selector => document.querySelector(selector);
const connectButton = $("#connect");
const connection = $("#connection");
const statusMessage = $("#status-message");
const saveButton = $("#save-configuration");
const saveDialog = $("#save-dialog");
const saveAcknowledgement = $("#save-unsafe-ack");
const confirmSave = $("#confirm-save");
const ledButton = $("#toggle-led");
const rebootDialog = $("#reboot-dialog");
const channelDialog = $("#channel-dialog");
let editingChannel = null;
let channelApplying = false;
let pendingSettings = null;
const controls = ["#hw-role", "#pio-role", "#sw-role", "#i2c-input"].map($);

function render(state) {
  const dashboard = state.dashboard;
  const connected = ["connected", "writing"].includes(state.phase);
  const ready = state.phase === "connected" && dashboard.identity && dashboard.configuration;
  renderDashboard(dashboard, Boolean(ready));
  $("#apply-channel").disabled = !ready || channelApplying;
  $("#channel-frequency").disabled = channelApplying;
  $("#channel-duty").disabled = channelApplying;
  if (!connected && channelDialog.open) channelDialog.close();
  connection.dataset.state = connected ? "connected" : "disconnected";
  $("#connection-label").textContent = state.phase === "unsupported" ? "Unavailable"
    : connected ? "Connected" : state.phase === "connecting" ? "Connecting" : "Disconnected";
  connectButton.textContent = state.phase === "connecting" ? "Connecting..."
    : state.phase === "disconnecting" ? "Disconnecting..." : connected ? "Disconnect" : "Connect";
  connectButton.className = `button ${connected ? "button-danger" : "button-secondary"}`;
  connectButton.disabled = !["disconnected", "connected"].includes(state.phase);
  for (const control of controls) control.disabled = !ready;
  const clockReady = ready && dashboard.identity.can_set_clock;
  $("#clock-profile-select").disabled = !clockReady;
  saveButton.disabled = !ready || !dashboard.identity.can_save_reboot;
  const ledAvailable = Boolean(dashboard.identity?.can_set_led && dashboard.configuration);
  const ledOn = ledAvailable && dashboard.configuration.led_on;
  ledButton.disabled = !ready || !ledAvailable;
  ledButton.dataset.state = ledAvailable ? ledOn ? "on" : "off" : "unknown";
  ledButton.textContent = `Board LED: ${ledAvailable ? ledOn ? "On" : "Off" : "--"}`;
  ledButton.setAttribute("aria-pressed", String(Boolean(ledOn)));
  ledButton.title = ledAvailable ? `Turn board LED ${ledOn ? "off" : "on"}`
    : "Board LED unavailable; connect with firmware supporting LED control";
  $("#reboot-board").disabled = !ready || !dashboard.identity.can_reboot;
  if (!ready && rebootDialog.open) rebootDialog.close();
  const settingsHelp = !connected
    ? "Connect to read boot settings and enable Save and Reboot."
    : !dashboard.identity || !dashboard.configuration
      ? "Read device identity and configuration successfully to enable Save and Reboot."
      : !dashboard.identity.can_save_reboot
        ? "Connected firmware does not support Save and Reboot. Flash the latest firmware for your board, then disconnect and reconnect this page."
        : state.phase === "writing" ? "A command is in progress. Wait before saving."
          : "Edit the boot settings, then save and reboot to apply them.";
  $("#settings-help").textContent = settingsHelp;
  saveButton.title = saveButton.disabled ? settingsHelp : "Save boot settings and restart the board";
  if (!ready && saveDialog.open) {
    saveDialog.close();
    pendingSettings = null;
  }
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
    client.update({ error: error.message });
  }
}

const integer = selector => Number($(selector).value.trim() === "" ? NaN : $(selector).value);

$(".settings-controls").addEventListener("input", () =>
  renderBootEdits(client.state.dashboard.configuration));

connectButton.addEventListener("click", () =>
  client.state.phase === "disconnected" ? client.connect() : client.disconnect());
$(".channel-panels").addEventListener("click", event => {
  const button = event.target.closest(".channel-value");
  if (!button || client.state.phase !== "connected") return;
  const channel = Number(button.dataset.channel);
  const data = client.state.dashboard;
  const reading = data.channels.find(entry => entry.channel === channel);
  const bank = Math.floor(channel / 8);
  if (!reading || data.configuration?.running.roles[bank] !== 0 || data.bank_errors[bank]
    || reading.freq_hz === null || reading.duty === null) return;
  editingChannel = channel;
  $("#channel-dialog-title").textContent =
    `${["Hardware PWM", "PIO PWM", "Software PWM"][bank]} - Channel ${channel} - GPIO ${data.pins?.channels[channel] ?? "--"}`;
  $("#channel-frequency").value = String(reading.freq_hz);
  $("#channel-duty").value = String(reading.duty);
  $("#channel-error").textContent = "";
  channelDialog.showModal();
  $(button.dataset.field === "duty" ? "#channel-duty" : "#channel-frequency").focus();
});
$("#cancel-channel").addEventListener("click", () => channelDialog.close());
$("#channel-form").addEventListener("submit", async event => {
  event.preventDefault();
  if (editingChannel === null || channelApplying) return;
  const channel = editingChannel;
  const data = client.state.dashboard;
  const bank = Math.floor(channel / 8);
  if (client.state.phase !== "connected" || data.configuration?.running.roles[bank] !== 0
    || data.bank_errors[bank]) {
    $("#channel-error").textContent = "Channel is no longer available for editing. Reconnect and read its status.";
    return;
  }
  channelApplying = true;
  $("#channel-error").textContent = "";
  render(client.state);
  try {
    await client.setChannel(channel, integer("#channel-frequency"), integer("#channel-duty"));
    channelDialog.close();
  } catch (error) {
    $("#channel-error").textContent = error.message;
    client.update({ error: error.message });
  } finally {
    channelApplying = false;
    render(client.state);
  }
});
ledButton.addEventListener("click", () =>
  run(() => client.setLed(!client.state.dashboard.configuration.led_on)));
$("#reboot-board").addEventListener("click", () => rebootDialog.showModal());
$("#cancel-reboot").addEventListener("click", () => rebootDialog.close());
$("#confirm-reboot").addEventListener("click", () => {
  rebootDialog.close();
  run(() => client.reboot());
});
saveButton.addEventListener("click", () => {
  if (!$("#i2c-input").reportValidity()) return;
  pendingSettings = {
    clockProfile: integer("#clock-profile-select"),
    i2cAddress: integer("#i2c-input"),
    roles: ["hw", "pio", "sw"].map(backend => integer(`#${backend}-role`)),
  };
  $("#save-summary").textContent = [
    `Clock: ${CLOCK_PROFILES[pendingSettings.clockProfile]}`,
    `I2C: 0x${pendingSettings.i2cAddress.toString(16).padStart(2, "0")}`,
    ...["HW", "PIO", "SW"].map((backend, bank) => `${backend}: ${ROLES[pendingSettings.roles[bank]]}`),
  ].join(" / ");
  const overclock = pendingSettings.clockProfile > 1;
  $("#save-overclock").hidden = !overclock;
  saveAcknowledgement.checked = false;
  saveAcknowledgement.disabled = !overclock;
  confirmSave.disabled = overclock;
  saveDialog.showModal();
});
saveAcknowledgement.addEventListener("change", () => { confirmSave.disabled = !saveAcknowledgement.checked; });
$("#cancel-save").addEventListener("click", () => {
  pendingSettings = null;
  saveDialog.close();
});
confirmSave.addEventListener("click", () => {
  const settings = pendingSettings;
  if (!settings || client.state.phase !== "connected") return;
  const acknowledged = saveAcknowledgement.checked;
  pendingSettings = null;
  saveDialog.close();
  run(() => client.saveAndReboot(settings, acknowledged));
});
globalThis.setInterval(() => client.refresh(), 1000);
globalThis.setInterval(() => updateReportAge(client.state.dashboard), 1000);
