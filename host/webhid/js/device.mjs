export const DEVICE_FILTER = Object.freeze({
  vendorId: 0xcafe,
  productId: 0x4012,
  usagePage: 0xff00,
  usage: 0x01,
});

const PRODUCT_ID_PICOPWM = 3;
const CHANNEL_COUNT = 24;
const BANK_COUNT = 3;
const BANK_SIZE = 8;
const IDENTITY_LENGTH = 18;
const CONFIGURATION_LENGTH = 21;
const BANK_STATUS_LENGTH = 41;
const ACTION_LENGTH = 12;
const TRANSFER_TIMEOUT_MS = 3000;
const SAVE_TIMEOUT_MS = 15000;
const ACTION_POLL_MS = 50;

export const OPERATION = Object.freeze({
  SET_CHANNEL: 1, STOP: 2, SET_BANK: 3, SET_I2C_ADDRESS: 4, SAVE_CONFIG: 5, SET_CLOCK_PROFILE: 6,
  SAVE_REBOOT: 7,
  SET_LED: 8, REBOOT: 9,
});
export const CLOCK_PROFILES = ["SDK default", "150 MHz", "200 MHz", "250 MHz", "280 MHz"];
export const BACKENDS = ["HW", "PIO", "SW"];
export const ROLES = ["Generator", "Monitor"];
const STATUS_NAMES = ["OK", "BUSY", "INVALID", "UNAVAILABLE", "TIMEOUT", "APPLY_FAILED", "PENDING",
  "UNSUPPORTED", "STORAGE_FAILED"];
const PENDING = 6;

function payload(report, expectedLength, reportName, reportId = null) {
  if (report instanceof DataView && reportId !== null && report.byteLength === expectedLength + 1
    && report.getUint8(0) === reportId) {
    return new DataView(report.buffer, report.byteOffset + 1, expectedLength);
  }
  if (!(report instanceof DataView) || report.byteLength !== expectedLength) {
    const received = report instanceof DataView ? report.byteLength : "non-DataView";
    throw new Error(`${reportName} must contain exactly ${expectedLength} payload bytes (received ${received})`);
  }
  return new DataView(report.buffer, report.byteOffset, report.byteLength);
}

export function decodeIdentity(report) {
  const data = payload(report, IDENTITY_LENGTH, "Identity report", 1);
  if (data.getUint8(0) !== 1) throw new Error("Unsupported identity layout");
  const productId = data.getUint16(1, true);
  if (productId !== PRODUCT_ID_PICOPWM) {
    throw new Error(`Unsupported product ID 0x${productId.toString(16).padStart(4, "0")}`);
  }
  const mcu = data.getUint8(14);
  const channels = data.getUint8(15);
  const capabilities = data.getUint8(16);
  const profiles = data.getUint8(17);
  if (![1, 2].includes(mcu) || channels !== CHANNEL_COUNT || capabilities & ~0x7f || profiles & ~0x1f
    || Boolean(capabilities & 2) !== Boolean(profiles) || profiles && !(profiles & 2)) {
    throw new Error("Invalid identity capabilities");
  }
  return {
    product_id: productId,
    serial_number: Array.from({ length: 8 }, (_, index) =>
      data.getUint8(3 + index).toString(16).padStart(2, "0")).join("").toUpperCase(),
    firmware_version: `${data.getUint8(11)}.${data.getUint8(12)}.${data.getUint8(13)}`,
    mcu: { 1: "RP2040", 2: "RP2350" }[mcu],
    channel_count: channels,
    can_save: Boolean(capabilities & 1),
    can_save_reboot: Boolean(capabilities & 4),
    can_set_led: Boolean(capabilities & 8),
    can_reboot: Boolean(capabilities & 16),
    can_read_memory: Boolean(capabilities & 32),
    can_read_runtime: Boolean(capabilities & 64),
    board_model: { 1: "Pico", 2: "Pico 2" }[mcu],
    can_set_clock: Boolean(capabilities & 2),
    clock_profile_mask: profiles,
  };
}

function bankConfiguration(data, offset) {
  const backends = [0, 1, 2].map(bank => data.getUint8(offset + bank));
  const roles = [0, 1, 2].map(bank => data.getUint8(offset + 3 + bank));
  const address = data.getUint8(offset + 6);
  if (backends.some(value => value > 2) || roles.some(value => value > 1) || address > 127) {
    throw new Error("Invalid bank configuration");
  }
  return { backends, roles, i2c_address: address };
}

export function decodeConfiguration(report) {
  const data = payload(report, CONFIGURATION_LENGTH, "Configuration report", 2);
  if (data.getUint8(0) !== 1) throw new Error("Unsupported configuration layout");
  const profile = data.getUint8(16);
  const clock = data.getUint32(17, true);
  if (profile > 4 || !clock) throw new Error("Invalid configuration fields");
  return {
    running: bankConfiguration(data, 1),
    target: bankConfiguration(data, 8),
    clock_profile: profile,
    system_clock_hz: clock,
    led_on: Boolean(data.getUint8(15) & 1),
  };
}

export function decodeRuntime(report) {
  const data = payload(report, 13, "Runtime report", 4);
  const core0 = data.getUint16(9, true);
  const core1 = data.getUint16(11, true);
  if (data.getUint8(0) !== 1 || core0 > 10000 || core1 > 10000) {
    throw new Error("Invalid runtime report");
  }
  const temperature = data.getInt32(5, true);
  return {
    uptime_seconds: data.getUint32(1, true),
    temperature_c: temperature === -2147483648 ? null : temperature / 1000,
    core0_load: core0 / 100,
    core1_load: core1 / 100,
  };
}

export function decodeBankStatus(report, reportId) {
  const data = payload(report, BANK_STATUS_LENGTH, `Bank status report ${reportId}`, reportId);
  if (data.getUint8(0) !== 1) throw new Error("Unsupported bank-status layout");
  const bank = reportId - 10;
  return Array.from({ length: BANK_SIZE }, (_, index) => {
    const duty = data.getUint8(5 + index * 5);
    if (duty > 100) throw new Error("Invalid duty in bank status");
    return { channel: bank * BANK_SIZE + index, freq_hz: data.getUint32(1 + index * 5, true), duty };
  });
}

export function decodeActionResult(report) {
  const data = payload(report, ACTION_LENGTH, "Action result report", 20);
  const status = data.getUint8(3);
  const reserved = Array.from({ length: 8 }, (_, index) => data.getUint8(4 + index));
  if (data.getUint8(0) !== 1 || status >= STATUS_NAMES.length || reserved.some(Boolean)) {
    throw new Error("Invalid action result");
  }
  return {
    transaction_id: data.getUint8(1),
    operation: data.getUint8(2),
    status,
    status_name: STATUS_NAMES[status],
  };
}

export function encodeActionRequest(transactionId, operation,
  { channel = 0, arg0 = 0, arg1 = 0, flags = 0 } = {}) {
  const request = new Uint8Array(ACTION_LENGTH);
  const data = new DataView(request.buffer);
  data.setUint8(0, 1);
  data.setUint8(1, transactionId);
  data.setUint8(2, operation);
  data.setUint8(3, channel);
  data.setUint32(4, arg0, true);
  data.setUint8(8, arg1);
  data.setUint8(9, flags);
  return request;
}

function emptyChannels() {
  return Array.from({ length: CHANNEL_COUNT }, (_, channel) => ({ channel, freq_hz: null, duty: null }));
}

function emptyDashboard() {
  return {
    connected: false,
    identity: null,
    identity_error: null,
    configuration: null,
    configuration_error: null,
    runtime: null,
    runtime_error: null,
    channels: emptyChannels(),
    bank_errors: Array(BANK_COUNT).fill(null),
    action_result: null,
    updated_at: null,
  };
}

export function matchesPicoPwm(device) {
  return device?.vendorId === DEVICE_FILTER.vendorId && device?.productId === DEVICE_FILTER.productId
    && (!device.collections?.length
      || device.collections.some(collection => collection.usagePage === DEVICE_FILTER.usagePage));
}

export class PicoPwmWebHid {
  constructor(hid, onChange = () => {}) {
    this.hid = hid;
    this.onChange = onChange;
    this.device = null;
    this.generation = 0;
    this.transactionId = 0;
    this.busy = false;
    this.ioBlocked = false;
    this.state = {
      phase: hid ? "disconnected" : "unsupported",
      message: hid ? "No device selected" : "WebHID is unavailable in this browser or context",
      error: null,
      dashboard: emptyDashboard(),
    };
    hid?.addEventListener("disconnect", event => {
      if (event.device !== this.device) return;
      this.generation++;
      this.device = null;
      this.busy = false;
      this.update({ phase: "disconnected", message: "Device unplugged", error: null, dashboard: emptyDashboard() });
    });
  }

  update(changes) {
    this.state = { ...this.state, ...changes };
    this.onChange(this.state);
  }

  patch(changes) {
    this.update({ dashboard: { ...this.state.dashboard, ...changes } });
  }

  isCurrent(generation) {
    return generation === this.generation && this.device !== null;
  }

  async withTimeout(operation, timeout) {
    let timer;
    try {
      return await Promise.race([Promise.resolve().then(operation), new Promise((_, reject) => {
        timer = globalThis.setTimeout(() => reject(new Error("timeout")), timeout);
      })]);
    } catch (error) {
      if (error.message !== "timeout") throw error;
      this.ioBlocked = true;
      throw new Error("HID operation timed out; outcome is unconfirmed. Disconnect before continuing.");
    } finally {
      globalThis.clearTimeout(timer);
    }
  }

  async readFeature(reportId, timeout = TRANSFER_TIMEOUT_MS) {
    const device = this.device;
    const generation = this.generation;
    if (!device || this.ioBlocked) throw new Error("HID connection unavailable; disconnect and reconnect.");
    const data = await this.withTimeout(() => device.receiveFeatureReport(reportId), timeout);
    if (!this.isCurrent(generation)) throw new Error("HID connection changed during operation");
    return data;
  }

  async readIdentity() {
    try {
      const identity = decodeIdentity(await this.readFeature(1));
      if (identity.can_read_memory) {
        const memory = payload(await this.readFeature(3), 9, "Memory report", 3);
        const reserved = memory.getUint32(1, true);
        const total = memory.getUint32(5, true);
        if (memory.getUint8(0) !== 1 || !total || reserved > total) throw new Error("Invalid memory report");
        identity.ram_reserved_bytes = reserved;
        identity.ram_total_bytes = total;
      }
      this.patch({ identity, identity_error: null });
    } catch (error) {
      if (this.device) this.patch({ identity: null, identity_error: error.message });
      throw error;
    }
  }

  async readConfiguration() {
    try {
      this.patch({ configuration: decodeConfiguration(await this.readFeature(2)), configuration_error: null });
    } catch (error) {
      if (this.device) this.patch({ configuration: null, configuration_error: error.message });
    }
  }

  async readRuntime() {
    if (!this.state.dashboard.identity?.can_read_runtime) return;
    try {
      const runtime = decodeRuntime(await this.readFeature(4));
      this.patch({ runtime, runtime_error: runtime.temperature_c === null ? "Temperature ADC reading is invalid" : null });
    } catch (error) {
      if (this.device) this.patch({ runtime: null, runtime_error: error.message });
    }
  }

  async readBanks() {
    for (let bank = 0; bank < BANK_COUNT; bank++) {
      const reportId = 10 + bank;
      try {
        const entries = decodeBankStatus(await this.readFeature(reportId), reportId);
        const channels = [...this.state.dashboard.channels];
        entries.forEach(entry => { channels[entry.channel] = entry; });
        const bankErrors = [...this.state.dashboard.bank_errors];
        bankErrors[bank] = null;
        this.patch({ channels, bank_errors: bankErrors, updated_at: new Date().toISOString() });
      } catch (error) {
        if (!this.device) return;
        const channels = [...this.state.dashboard.channels];
        const blank = emptyChannels();
        for (let index = bank * BANK_SIZE; index < (bank + 1) * BANK_SIZE; index++) channels[index] = blank[index];
        const bankErrors = [...this.state.dashboard.bank_errors];
        bankErrors[bank] = error.message;
        this.patch({ channels, bank_errors: bankErrors });
        if (this.ioBlocked) return;
      }
    }
  }

  async refresh(initial = false) {
    const generation = this.generation;
    if (!this.device || this.busy || this.ioBlocked || this.state.phase !== "connected") return;
    this.busy = true;
    try {
      if (!this.state.dashboard.identity) await this.readIdentity();
      await this.readConfiguration();
      await this.readRuntime();
      await this.readBanks();
      if (this.isCurrent(generation)) this.update({ message: "HID status received", error: null });
    } catch (error) {
      if (this.isCurrent(generation)) this.update({ error: error.message });
    } finally {
      if (generation === this.generation) this.busy = false;
    }
  }

  nextTransactionId() {
    this.transactionId = this.transactionId % 255 + 1;
    return this.transactionId;
  }

  async performAction(operation, fields = {}) {
    const device = this.device;
    const generation = this.generation;
    const identity = this.state.dashboard.identity;
    if (!device || !identity || this.busy || this.ioBlocked || this.state.phase !== "connected") {
      throw new Error("Device is not ready for a command.");
    }
    if (operation === OPERATION.SAVE_CONFIG && !identity.can_save
      || operation === OPERATION.SAVE_REBOOT && !identity.can_save_reboot
      || operation === OPERATION.SET_LED && !identity.can_set_led
      || operation === OPERATION.REBOOT && !identity.can_reboot
      || operation === OPERATION.SET_CLOCK_PROFILE && !identity.can_set_clock) {
      throw new Error("The firmware does not advertise this capability.");
    }
    this.busy = true;
    const saving = operation === OPERATION.SAVE_CONFIG || operation === OPERATION.SAVE_REBOOT;
    this.update({ phase: "writing", message: saving ? "Saving configuration" : "Sending command", error: null });
    try {
      const before = decodeActionResult(await this.readFeature(20));
      if (before.status === PENDING) throw new Error("A previous command is still pending.");
      const id = this.nextTransactionId();
      const timeout = saving ? SAVE_TIMEOUT_MS : TRANSFER_TIMEOUT_MS;
      await this.withTimeout(() => device.sendFeatureReport(20, encodeActionRequest(id, operation, fields)), timeout);
      const deadline = Date.now() + timeout;
      let first = true;
      for (;;) {
        if (!this.isCurrent(generation)) throw new Error("HID connection changed; outcome is unconfirmed.");
        const result = decodeActionResult(await this.readFeature(20, Math.max(1, deadline - Date.now())));
        this.patch({ action_result: result });
        if (result.transaction_id !== id || result.operation !== operation) {
          if (first) throw new Error("The device did not accept the request; no change was made.");
          throw new Error("Result no longer matches the request; outcome is unconfirmed.");
        }
        first = false;
        if (result.status !== PENDING) {
          if (result.status !== 0) throw new Error(`Command failed: ${result.status_name}.`);
          return result;
        }
        if (Date.now() >= deadline) throw new Error("Timed out awaiting result; outcome is unconfirmed.");
        await new Promise(resolve => globalThis.setTimeout(resolve, ACTION_POLL_MS));
      }
    } catch (error) {
      if (this.isCurrent(generation)) this.update({ error: error.message });
      throw error;
    } finally {
      if (generation === this.generation) this.busy = false;
      if (this.isCurrent(generation)) this.update({ phase: "connected" });
    }
  }

  async afterChange(configuration) {
    await this.refresh(configuration);
  }

  async setChannel(channel, freqHz, duty) {
    if (!Number.isInteger(channel) || channel < 0 || channel >= CHANNEL_COUNT
      || !Number.isInteger(freqHz) || freqHz < 0 || freqHz > 0xffffffff
      || !Number.isInteger(duty) || duty < 0 || duty > 100) {
      throw new Error("Channel, frequency, or duty is out of range.");
    }
    await this.performAction(OPERATION.SET_CHANNEL, { channel, arg0: freqHz, arg1: duty });
    await this.afterChange(false);
  }

  async stop() {
    await this.performAction(OPERATION.STOP);
    await this.afterChange(false);
  }

  async setBank(bank, backend, role) {
    if (![0, 1, 2].includes(bank) || ![0, 1, 2].includes(backend) || ![0, 1].includes(role)) {
      throw new Error("Bank, backend, or role is out of range.");
    }
    await this.performAction(OPERATION.SET_BANK, { channel: bank, arg0: backend | role << 8 });
    await this.afterChange(true);
  }

  async setI2cAddress(address) {
    if (!Number.isInteger(address) || address < 0 || address > 127) throw new Error("I2C address must be 0-127.");
    await this.performAction(OPERATION.SET_I2C_ADDRESS, { arg0: address });
    await this.afterChange(true);
  }

  async setClockProfile(profile, acknowledgeOverclock) {
    const identity = this.state.dashboard.identity;
    if (!Number.isInteger(profile) || profile < 0 || profile > 4 || !(identity?.clock_profile_mask & 1 << profile)) {
      throw new Error("Choose a profile the firmware advertises.");
    }
    if (profile > 1 && !acknowledgeOverclock) throw new Error("Acknowledge the overclock first.");
    await this.performAction(OPERATION.SET_CLOCK_PROFILE, { arg0: profile, flags: profile > 1 ? 2 : 0 });
    await this.afterChange(true);
  }

  async saveConfiguration(acknowledgeOverclock) {
    const profile = this.state.dashboard.configuration?.clock_profile;
    if (profile === undefined) throw new Error("Read the configuration first.");
    if (profile > 1 && !acknowledgeOverclock) throw new Error("Acknowledge the overclock first.");
    await this.performAction(OPERATION.SAVE_CONFIG, { flags: 1 | (profile > 1 ? 2 : 0) });
    await this.afterChange(true);
  }

  async saveAndReboot({ clockProfile, i2cAddress, roles }, acknowledgeOverclock) {
    const identity = this.state.dashboard.identity;
    if (!identity?.can_save_reboot) {
      throw new Error("Update the board firmware to enable Save and reboot.");
    }
    if (!Number.isInteger(clockProfile) || clockProfile < 0 || clockProfile > 4
      || !(identity.clock_profile_mask & 1 << clockProfile)
      || !Number.isInteger(i2cAddress) || i2cAddress < 8 || i2cAddress > 119
      || !Array.isArray(roles) || roles.length !== 3 || roles.some(role => ![0, 1].includes(role))) {
      throw new Error("Choose a supported clock, I2C address (8-119), and a role for each bank.");
    }
    if (clockProfile > 1 && !acknowledgeOverclock) throw new Error("Acknowledge the overclock first.");
    try {
      await this.performAction(OPERATION.SET_CLOCK_PROFILE,
        { arg0: clockProfile, flags: clockProfile > 1 ? 2 : 0 });
      await this.performAction(OPERATION.SET_I2C_ADDRESS, { arg0: i2cAddress });
      for (let bank = 0; bank < 3; bank++) {
        await this.performAction(OPERATION.SET_BANK, { channel: bank, arg0: bank | roles[bank] << 8 });
      }
      await this.performAction(OPERATION.SAVE_REBOOT, { flags: 1 | (clockProfile > 1 ? 2 : 0) });
    } catch (error) {
      throw new Error(`${error.message} Save and reboot was not confirmed; target settings may be partially updated. Reconnect and verify before retrying.`);
    }
    await this.disconnect();
    this.update({ message: "Configuration saved. Board is rebooting; reconnect to read the applied settings." });
  }

  async setLed(on) {
    if (typeof on !== "boolean") throw new Error("LED state must be on or off.");
    await this.performAction(OPERATION.SET_LED, { arg0: on ? 1 : 0 });
    await this.afterChange(true);
  }

  async reboot() {
    await this.performAction(OPERATION.REBOOT, { flags: 1 });
    await this.disconnect();
    this.update({ message: "Board is rebooting without saving. Reconnect after restart." });
  }

  async connect() {
    if (!this.hid || this.state.phase !== "disconnected") return;
    const generation = ++this.generation;
    this.update({ phase: "connecting", message: "Selecting PicoPWM HID interface", error: null,
      dashboard: emptyDashboard() });
    let selected;
    try {
      const devices = await this.hid.requestDevice({ filters: [{ ...DEVICE_FILTER }] });
      if (generation !== this.generation) return;
      if (!devices.length) {
        this.update({ phase: "disconnected", message: "No device selected" });
        return;
      }
      if (devices.length !== 1 || !matchesPicoPwm(devices[0])) {
        throw new Error("Select one PicoPWM vendor HID interface.");
      }
      selected = devices[0];
      if (!selected.opened) await selected.open();
      if (generation !== this.generation) {
        if (selected.opened) await selected.close();
        return;
      }
      this.device = selected;
      this.ioBlocked = false;
      this.busy = false;
      this.update({ phase: "connected", message: "Device connected",
        dashboard: { ...emptyDashboard(), connected: true } });
      await this.refresh(true);
      if (!this.state.dashboard.identity) {
        throw new Error(this.state.dashboard.identity_error ?? "Identity unavailable");
      }
    } catch (error) {
      if (selected?.opened) await selected.close().catch(() => {});
      if (generation !== this.generation) return;
      this.device = null;
      this.update({ phase: "disconnected", message: "Connection failed", error: error.message,
        dashboard: emptyDashboard() });
    }
  }

  async disconnect() {
    if (!this.hid) return;
    const device = this.device;
    this.generation++;
    this.device = null;
    this.busy = false;
    this.update({ phase: "disconnecting", message: "Closing HID interface" });
    let error = null;
    try {
      if (device?.opened) await device.close();
    } catch (closeError) {
      error = closeError.message;
    }
    this.ioBlocked = false;
    this.update({ phase: "disconnected", message: "Device disconnected", error, dashboard: emptyDashboard() });
  }
}
