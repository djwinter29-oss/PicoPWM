import { CLOCK_PROFILES, ROLES, PicoPwmWebHid } from "./device.mjs?v=8";
import {
  convertFrequencyValue, frequencyEditorValue, frequencyMax, frequencyToHertz,
  renderBootEdits, renderDashboard, updateReportAge,
} from "./view.mjs?v=16";

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

function updateSaveButton(state, bootConfigurationChanged) {
  const dashboard = state.dashboard;
  const connected = ["connected", "writing"].includes(state.phase);
  const ready = state.phase === "connected" && dashboard.identity && dashboard.configuration;
  const settingsHelp = !connected
    ? "Connect to read boot settings and enable Save & Reboot."
    : !dashboard.identity || !dashboard.configuration
      ? "Read device identity and configuration successfully to enable Save & Reboot."
      : !dashboard.identity.can_save_reboot
        ? "Connected firmware does not support Save & Reboot. Flash the latest firmware for your board, then disconnect and reconnect this page."
        : state.phase === "writing" ? "A command is in progress. Wait before saving."
          : !bootConfigurationChanged ? "Boot configuration is unchanged."
            : "Save changed boot settings and reboot to apply them.";
  saveButton.disabled = !ready || !dashboard.identity.can_save_reboot || !bootConfigurationChanged;
  $("#settings-help").textContent = settingsHelp;
  saveButton.title = saveButton.disabled ? settingsHelp : "Save boot settings and restart the board";
}

function render(state) {
  const dashboard = state.dashboard;
  const connected = ["connected", "writing"].includes(state.phase);
  const ready = state.phase === "connected" && dashboard.identity && dashboard.configuration;
  const bootConfigurationChanged = renderDashboard(dashboard, Boolean(ready));
  $("#apply-channel").disabled = !ready || channelApplying;
  $("#channel-frequency").disabled = channelApplying;
  $("#channel-frequency-unit").disabled = channelApplying;
  $("#channel-duty").disabled = channelApplying;
  $("#channel-preset").disabled = channelApplying;
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
  updateSaveButton(state, bootConfigurationChanged);
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

$(".settings-controls").addEventListener("input", () => {
  const state = client.state;
  const changed = renderBootEdits(state.dashboard.configuration, state.dashboard.identity);
  updateSaveButton(state, changed);
});

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
  const frequency = frequencyEditorValue(reading.freq_hz);
  $("#channel-frequency").value = frequency.value;
  $("#channel-frequency-unit").value = frequency.unit;
  $("#channel-frequency").dataset.unit = frequency.unit;
  $("#channel-frequency").max = frequencyMax(frequency.unit);
  $("#channel-duty").value = String(reading.duty);
  $("#channel-error").textContent = "";
  channelDialog.showModal();
  $(button.dataset.field === "duty" ? "#channel-duty" : "#channel-frequency").focus();
});
$("#channel-frequency-unit").addEventListener("change", () => {
  const input = $("#channel-frequency");
  const unit = $("#channel-frequency-unit");
  input.value = convertFrequencyValue(input.value, input.dataset.unit, unit.value);
  input.dataset.unit = unit.value;
  input.max = frequencyMax(unit.value);
});
$("#channel-preset").addEventListener("click", () => {
  $("#channel-frequency-unit").value = "kHz";
  $("#channel-frequency").dataset.unit = "kHz";
  $("#channel-frequency").max = frequencyMax("kHz");
  $("#channel-frequency").value = "1";
  $("#channel-duty").value = "50";
  $("#channel-error").textContent = "";
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
  if (!$("#channel-frequency").reportValidity() || !$("#channel-duty").reportValidity()) return;
  let frequencyHz;
  try {
    frequencyHz = frequencyToHertz($("#channel-frequency").value, $("#channel-frequency-unit").value);
  } catch (error) {
    $("#channel-error").textContent = error.message;
    return;
  }
  channelApplying = true;
  $("#channel-error").textContent = "";
  render(client.state);
  try {
    await client.setChannel(channel, frequencyHz, integer("#channel-duty"));
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
