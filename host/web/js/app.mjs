import { CLOCK_PROFILES, ROLES } from "./device.mjs?v=6";
import { renderBootEdits, renderDashboard, updateReportAge } from "./view.mjs?v=13";
import { PicoPwmHost, request } from "./client.mjs?v=1";

const $ = selector => document.querySelector(selector);
const boardSelect = $("#board-select");
const rescanButton = $("#rescan-boards");
const saveDialog = $("#save-dialog");
const rebootDialog = $("#reboot-dialog");
const channelDialog = $("#channel-dialog");
const saveAcknowledgement = $("#save-unsafe-ack");
let editingChannel = null;
let channelApplying = false;
let pendingSettings = null;
let scanning = false;
let scanError = null;

function updateSaveButton(state, bootConfigurationChanged) {
  const data = state.dashboard;
  const ready = state.phase === "connected" && data.identity && data.configuration;
  const help = state.phase === "writing" ? "A command is in progress. Wait before saving."
    : !ready ? "Select a board and read its configuration to enable Save & Reboot."
      : !data.identity.can_save_reboot ? "Update board firmware to enable Save & Reboot."
        : !bootConfigurationChanged ? "Boot configuration is unchanged."
          : "Save changed boot settings and reboot to apply them.";
  $("#settings-help").textContent = help;
  $("#save-configuration").disabled = !ready || !data.identity.can_save_reboot || !bootConfigurationChanged;
  $("#save-configuration").title = help;
}

function render(state) {
  const data = state.dashboard;
  const connected = ["connected", "writing"].includes(state.phase);
  const ready = state.phase === "connected" && data.identity && data.configuration;
  const bootConfigurationChanged = renderDashboard(data, Boolean(ready));
  $("#connection").dataset.state = connected ? "connected" : "disconnected";
  $("#connection-label").textContent = connected ? "Connected" : "Disconnected";
  boardSelect.disabled = state.phase === "writing" || scanning || !boardSelect.options.length
    || ![...boardSelect.options].some(option => option.value);
  rescanButton.disabled = state.phase === "writing" || scanning;
  for (const selector of ["#hw-role", "#pio-role", "#sw-role", "#i2c-input"])
    $(selector).disabled = !ready;
  $("#clock-profile-select").disabled = !ready || !data.identity.can_set_clock;
  updateSaveButton(state, bootConfigurationChanged);
  $("#reboot-board").disabled = !ready || !data.identity.can_reboot;
  const ledAvailable = Boolean(data.identity?.can_set_led && data.configuration);
  const ledOn = ledAvailable && data.configuration.led_on;
  $("#toggle-led").disabled = !ready || !ledAvailable;
  $("#toggle-led").textContent = `Board LED: ${ledAvailable ? ledOn ? "On" : "Off" : "--"}`;
  $("#toggle-led").dataset.state = ledAvailable ? ledOn ? "on" : "off" : "unknown";
  $("#toggle-led").setAttribute("aria-pressed", String(Boolean(ledOn)));
  $("#toggle-led").title = ledAvailable ? `Turn board LED ${ledOn ? "off" : "on"}`
    : "Board LED unavailable; update firmware if unsupported";
  $("#apply-channel").disabled = !ready || channelApplying;
  $("#channel-frequency").disabled = channelApplying;
  $("#channel-duty").disabled = channelApplying;
  if (!connected && channelDialog.open) channelDialog.close();
  if (!ready && rebootDialog.open) rebootDialog.close();
  if (!ready && saveDialog.open) { saveDialog.close(); pendingSettings = null; }
  $("#status-message").textContent = scanError ?? state.error ?? state.message;
  $("#status-message").parentElement.dataset.error = String(Boolean(scanError || state.error));
}

const client = new PicoPwmHost(render);
const integer = selector => Number($(selector).value.trim() === "" ? NaN : $(selector).value);
async function run(action) {
  try { await action(); }
  catch (error) { client.update({ error: error.message }); }
}

async function rescan() {
  if (scanning || client.state.phase === "writing") return;
  scanning = true;
  render(client.state);
  try {
    const result = await request("/api/boards");
    scanError = null;
    const selected = client.board;
    const label = boardSelect.selectedOptions[0]?.textContent ?? selected;
    boardSelect.replaceChildren();
    for (const board of result.boards) {
      const option = document.createElement("option");
      option.value = board.id;
      option.textContent = `PicoPWM - ${board.serial_number || board.device_path}`;
      boardSelect.append(option);
    }
    if (selected && !result.boards.some(board => board.id === selected)) {
      const missing = document.createElement("option");
      missing.value = selected;
      missing.textContent = `${label.replace(/ \(not found\)$/, "")} (not found)`;
      boardSelect.append(missing);
    }
    if (!selected) {
      const initial = result.boards.find(board => board.id === result.default_board_id);
      if (initial) client.select(initial.id);
      else {
        const placeholder = document.createElement("option");
        placeholder.value = "";
        placeholder.textContent = result.boards.length ? "Select a board" : "No boards found";
        boardSelect.prepend(placeholder);
      }
    }
    boardSelect.value = client.board ?? "";
    if (!client.board) client.update({ message: result.boards.length ? "Select a board" : "No boards found; connect a board and Rescan" });
    await client.refresh();
  } catch (error) {
    scanError = `Board scan failed: ${error.message}`;
  } finally {
    scanning = false;
    render(client.state);
  }
}

boardSelect.addEventListener("change", () => {
  if (client.state.phase === "writing" || scanning) return;
  scanError = null;
  client.select(boardSelect.value);
  client.refresh();
});
rescanButton.addEventListener("click", rescan);
$(".settings-controls").addEventListener("input", () => {
  const state = client.state;
  const changed = renderBootEdits(state.dashboard.configuration, state.dashboard.identity);
  updateSaveButton(state, changed);
});
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
  const data = client.state.dashboard;
  const bank = Math.floor(editingChannel / 8);
  if (client.state.phase !== "connected" || data.configuration?.running.roles[bank] !== 0 || data.bank_errors[bank]) {
    $("#channel-error").textContent = "Channel is no longer available for editing. Reconnect and read its status.";
    return;
  }
  channelApplying = true;
  $("#channel-error").textContent = "";
  render(client.state);
  try {
    await client.setChannel(editingChannel, integer("#channel-frequency"), integer("#channel-duty"));
    channelDialog.close();
  } catch (error) {
    $("#channel-error").textContent = error.message;
    client.update({ error: error.message });
  } finally {
    channelApplying = false;
    render(client.state);
  }
});
$("#toggle-led").addEventListener("click", () => run(() => client.setLed(!client.state.dashboard.configuration.led_on)));
$("#reboot-board").addEventListener("click", () => rebootDialog.showModal());
$("#cancel-reboot").addEventListener("click", () => rebootDialog.close());
$("#confirm-reboot").addEventListener("click", () => { rebootDialog.close(); run(() => client.reboot()); });
$("#save-configuration").addEventListener("click", () => {
  if (!$("#i2c-input").reportValidity()) return;
  pendingSettings = {
    clockProfile: integer("#clock-profile-select"), i2cAddress: integer("#i2c-input"),
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
  $("#confirm-save").disabled = overclock;
  saveDialog.showModal();
});
saveAcknowledgement.addEventListener("change", () => { $("#confirm-save").disabled = !saveAcknowledgement.checked; });
$("#cancel-save").addEventListener("click", () => { pendingSettings = null; saveDialog.close(); });
$("#confirm-save").addEventListener("click", () => {
  const settings = pendingSettings;
  if (!settings || client.state.phase !== "connected") return;
  pendingSettings = null;
  saveDialog.close();
  run(() => client.saveAndReboot(settings, saveAcknowledgement.checked));
});

render(client.state);
request("/api/host").then(host => {
  $("#dashboard-type").textContent = host.dashboard_type;
  document.title = `PicoPWM Control | ${host.dashboard_type}`;
}).catch(error => client.update({ error: `Host identity unavailable: ${error.message}` }));
rescan();
globalThis.setInterval(() => client.refresh(), 1000);
globalThis.setInterval(() => updateReportAge(client.state.dashboard), 1000);
