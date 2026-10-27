import assert from "node:assert/strict";
import test from "node:test";
import {
  DEVICE_FILTER, OPERATION, PicoPwmWebHid, decodeActionResult, decodeBankStatus, decodeConfiguration,
  decodeIdentity, decodePins, decodeRuntime, encodeActionRequest, matchesPicoPwm,
} from "../js/device.mjs";

const view = bytes => new DataView(Uint8Array.from(bytes).buffer);

const identityBytes = (overrides = {}) => {
  const bytes = [1, 3, 0, 1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 2, 24, 3, 0x1f];
  Object.entries(overrides).forEach(([index, value]) => { bytes[index] = value; });
  return bytes;
};

const configurationBytes = () => [
  1, 0, 1, 2, 0, 0, 1, 0x20, 0, 1, 2, 0, 0, 1, 0x20, 1, 1, 0x80, 0xd1, 0xf0, 0x08,
];

test("identity decodes and rejects bad contracts", () => {
  const identity = decodeIdentity(view(identityBytes()));
  assert.equal(identity.mcu, "RP2350");
  assert.equal(identity.firmware_version, "1.2.3");
  assert.equal(identity.serial_number, "0102030405060708");
  assert.ok(identity.can_save && identity.can_set_clock);
  assert.throws(() => decodeIdentity(view(identityBytes({ 1: 1 }))), /product ID/);
  assert.throws(() => decodeIdentity(view(identityBytes({ 15: 8 }))), /capabilities/);
  assert.throws(() => decodeIdentity(view(identityBytes({ 16: 1 }))), /capabilities/);
  assert.throws(() => decodeIdentity(view(identityBytes({ 16: 0 }))), /capabilities/);
  assert.throws(() => decodeIdentity(view(identityBytes().slice(1))), /exactly 18/);
  assert.deepEqual(decodeIdentity(view([1, ...identityBytes()])).mcu, "RP2350");
});

test("configuration decodes the documented example", () => {
  const configuration = decodeConfiguration(view(configurationBytes()));
  assert.equal(configuration.system_clock_hz, 150_000_000);
  assert.equal(configuration.running.i2c_address, 0x20);
  assert.deepEqual(configuration.running.backends, [0, 1, 2]);
  assert.equal(configuration.clock_profile, 1);
  const invalid = configurationBytes();
  invalid[16] = 5;
  assert.throws(() => decodeConfiguration(view(invalid)), /Invalid/);
});

test("bank status decodes eight channels and validates duty", () => {
  const bytes = [1];
  for (let index = 0; index < 8; index++) bytes.push(...[100 + index, 0, 0, 0], 50);
  const entries = decodeBankStatus(view(bytes), 11);
  assert.equal(entries.length, 8);
  assert.deepEqual(entries[0], { channel: 8, freq_hz: 100, duty: 50 });
  assert.equal(entries[7].channel, 15);
  bytes[5] = 101;
  assert.throws(() => decodeBankStatus(view(bytes), 11), /duty/);
  assert.throws(() => decodeBankStatus(view(bytes.slice(1)), 11), /exactly 41/);
});

test("action request and result match the report 20 layout", () => {
  const request = encodeActionRequest(7, OPERATION.SET_CHANNEL, { channel: 5, arg0: 0x01020304, arg1: 50 });
  assert.equal(request.length, 12);
  assert.deepEqual([...request], [1, 7, 1, 5, 4, 3, 2, 1, 50, 0, 0, 0]);
  assert.deepEqual(decodeActionResult(view([1, 7, 1, 6, 0, 0, 0, 0, 0, 0, 0, 0])),
    { transaction_id: 7, operation: 1, status: 6, status_name: "PENDING" });
  assert.throws(() => decodeActionResult(view([1, 7, 1, 9, 0, 0, 0, 0, 0, 0, 0, 0])), /Invalid/);
  assert.throws(() => decodeActionResult(view([1, 7, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1])), /Invalid/);
  assert.throws(() => decodeActionResult(view([2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])), /Invalid/);
});

test("device filter requires the vendor usage page", () => {
  assert.equal(DEVICE_FILTER.productId, 0x4012);
  assert.ok(matchesPicoPwm({ vendorId: 0xcafe, productId: 0x4012, collections: [{ usagePage: 0xff00 }] }));
  assert.ok(!matchesPicoPwm({ vendorId: 0xcafe, productId: 0x4010, collections: [{ usagePage: 0xff00 }] }));
  assert.ok(!matchesPicoPwm({ vendorId: 0xcafe, productId: 0x4012, collections: [{ usagePage: 1 }] }));
});

function fakeDevice({ acceptSet = true, pendingPolls = 1, capabilities = 3 } = {}) {
  const device = {
    vendorId: 0xcafe, productId: 0x4012, collections: [{ usagePage: 0xff00 }], opened: false,
    sent: [], result: [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], pending: 0,
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    async receiveFeatureReport(id) {
      if (id === 1) return view(identityBytes({ 16: capabilities }));
      if (id === 3) return view([1, 0, 0x80, 0, 0, 0, 0x20, 4, 0]);
      if (id === 4) return view([1, 123, 0, 0, 0, 0x78, 0xEC, 0xFF, 0xFF, 100, 0, 0xC4, 9]);
      if (id === 5) return view([1, ...Array.from({ length: 24 }, (_, channel) => 47 - channel), 2, 3, 255]);
      if (id === 2) return view(configurationBytes());
      if (id === 20) {
        const result = [...this.result];
        if (this.pending > 0) { this.pending--; result[3] = 6; }
        return view(result);
      }
      const bytes = [1];
      for (let index = 0; index < 8; index++) bytes.push(...[100 * index, 0, 0, 0], index);
      return view(bytes);
    },
    async sendFeatureReport(id, data) {
      assert.equal(id, 20);
      this.sent.push([...data]);
      if (acceptSet) {
        this.result = [1, data[1], data[2], 0, 0, 0, 0, 0, 0, 0, 0, 0];
        this.pending = pendingPolls;
      }
    },
  };
  return device;
}

test("save and reboot submits all edited fields before one confirmed save", async () => {
  const device = fakeDevice({ capabilities: 7, pendingPolls: 0 });
  const client = await connected(device);
  assert.equal(client.state.dashboard.identity.can_save_reboot, true);
  await client.saveAndReboot({ clockProfile: 2, i2cAddress: 64, roles: [1, 0, 1] }, true);
  assert.deepEqual(device.sent.map(request => request[2]), [6, 4, 3, 3, 3, 7]);
  assert.equal(device.sent[0][9], 2);
  assert.equal(device.sent[1][4], 64);
  assert.deepEqual(device.sent.slice(2, 5).map(request => [request[3], request[4], request[5]]),
    [[0, 0, 1], [1, 1, 0], [2, 2, 1]]);
  assert.equal(device.sent[5][9], 3);
  assert.equal(client.state.phase, "disconnected");
  assert.match(client.state.message, /rebooting/);
});

test("board LED and reboot are capability gated and reboot never saves", async () => {
  const legacy = await connected(fakeDevice());
  await assert.rejects(legacy.setLed(true), /capability/);
  await assert.rejects(legacy.reboot(), /capability/);
  const device = fakeDevice({ capabilities: 31, pendingPolls: 0 });
  const client = await connected(device);
  await client.setLed(true);
  await client.setLed(false);
  await assert.rejects(client.setLed(1), /LED state/);
  await client.reboot();
  assert.deepEqual(device.sent.map(request => request[2]), [8, 8, 9]);
  assert.deepEqual(device.sent.map(request => request[4]), [1, 0, 0]);
  assert.equal(device.sent[2][9], 1);
  assert.equal(client.state.phase, "disconnected");
  assert.match(client.state.message, /without saving/);
  const config = configurationBytes();
  config[15] = 1;
  assert.equal(decodeConfiguration(view(config)).led_on, true);
});

test("identity reads firmware RAM only when advertised", async () => {
  const client = await connected(fakeDevice({ capabilities: 63 }));
  assert.equal(client.state.dashboard.identity.ram_reserved_bytes, 32768);
  assert.equal(client.state.dashboard.identity.ram_total_bytes, 270336);
  assert.equal(client.state.dashboard.identity.board_model, "Pico 2");
  const legacy = await connected(fakeDevice());
  assert.equal(legacy.state.dashboard.identity.ram_total_bytes, undefined);
});

test("runtime decodes signed temperature, uptime, and exact service-load percentages", async () => {
  const bytes = [1, 123, 0, 0, 0, 0x78, 0xEC, 0xFF, 0xFF, 100, 0, 0xC4, 9];
  assert.deepEqual(decodeRuntime(view(bytes)),
    { uptime_seconds: 123, temperature_c: -5, core0_load: 1, core1_load: 25 });
  const invalid = [...bytes];
  invalid[9] = 0xff;
  invalid[10] = 0xff;
  assert.throws(() => decodeRuntime(view(invalid)), /Invalid runtime/);
  assert.throws(() => decodeRuntime(view(bytes.slice(1))), /exactly 13/);
  const missingTemp = [...bytes];
  missingTemp.splice(5, 4, 0, 0, 0, 0x80);
  assert.equal(decodeRuntime(view(missingTemp)).temperature_c, null);
  const client = await connected(fakeDevice({ capabilities: 127 }));
  assert.equal(client.state.dashboard.runtime.uptime_seconds, 123);
  assert.equal(client.state.dashboard.runtime_error, null);
  assert.equal((await connected(fakeDevice())).state.dashboard.runtime, null);
});

test("save and reboot validates before writing and stops on failures", async () => {
  const legacy = fakeDevice({ pendingPolls: 0 });
  const legacyClient = await connected(legacy);
  const settings = { clockProfile: 2, i2cAddress: 64, roles: [1, 0, 1] };
  await assert.rejects(legacyClient.saveAndReboot(settings, true), /Update the board firmware/);
  assert.equal(legacy.sent.length, 0);
  const device = fakeDevice({ capabilities: 7, pendingPolls: 0, acceptSet: false });
  const client = await connected(device);
  await assert.rejects(client.saveAndReboot(settings, false), /Acknowledge/);
  await assert.rejects(client.saveAndReboot({ ...settings, i2cAddress: 0 }, true), /I2C address/);
  assert.equal(device.sent.length, 0);
  await assert.rejects(client.saveAndReboot(settings, true), /not confirmed/);
  assert.equal(device.sent.length, 1);
  assert.equal(client.state.phase, "connected");
});

async function connected(device) {
  const hid = { addEventListener() {}, requestDevice: async () => [device] };
  const client = new PicoPwmWebHid(hid);
  await client.connect();
  return client;
}

test("connect reads identity, configuration, and all 24 channels", async () => {
  const client = await connected(fakeDevice());
  assert.equal(client.state.phase, "connected");
  const { dashboard } = client.state;
  assert.equal(dashboard.identity.mcu, "RP2350");
  assert.equal(dashboard.configuration.system_clock_hz, 150_000_000);
  assert.ok(dashboard.channels.every(channel => channel.freq_hz !== null));
  assert.equal(dashboard.channels[23].channel, 23);
  await client.disconnect();
  assert.equal(client.state.phase, "disconnected");
});

test("pin reports are capability gated, validated, and cleared on disconnect", async () => {
  const bytes = [1, ...Array.from({ length: 24 }, (_, channel) => channel), 26, 27, 255];
  assert.equal(decodePins(view(bytes)).led, null);
  assert.equal(decodePins(view(bytes)).channels[23], 23);
  assert.throws(() => decodePins(view(bytes.slice(1))), /exactly 28/);
  const invalid = [...bytes]; invalid[1] = 48;
  assert.throws(() => decodePins(view(invalid)), /Invalid pin/);
  const device = fakeDevice({ capabilities: 255 });
  const client = await connected(device);
  assert.equal(client.state.dashboard.pins.channels[0], 47);
  assert.equal(client.state.dashboard.pins.i2c_sda, 2);
  await client.disconnect();
  assert.equal(client.state.dashboard.pins, null);
  const legacy = fakeDevice();
  const original = legacy.receiveFeatureReport.bind(legacy);
  legacy.receiveFeatureReport = id => {
    assert.notEqual(id, 5);
    return original(id);
  };
  assert.equal((await connected(legacy)).state.dashboard.pins, null);
  const bad = fakeDevice({ capabilities: 255 });
  const read = bad.receiveFeatureReport.bind(bad);
  bad.receiveFeatureReport = id => id === 5 ? view(invalid) : read(id);
  const failed = await connected(bad);
  assert.equal(failed.state.dashboard.pins, null);
  assert.match(failed.state.dashboard.pins_error, /Invalid pin/);
  assert.ok(failed.state.dashboard.identity);
});

test("setChannel sends a nonzero transaction and waits past PENDING", async () => {
  const device = fakeDevice();
  const client = await connected(device);
  await client.setChannel(3, 1000, 25);
  assert.deepEqual(device.sent[0], [1, 1, 1, 3, 0xe8, 3, 0, 0, 25, 0, 0, 0]);
  assert.equal(client.state.dashboard.action_result.status_name, "OK");
  await client.stop();
  assert.equal(device.sent[1][1], 2);
  assert.equal(device.sent[1][2], OPERATION.STOP);
});

test("a dropped SET is reported as not accepted", async () => {
  const client = await connected(fakeDevice({ acceptSet: false }));
  await assert.rejects(() => client.setChannel(0, 1000, 50), /did not accept/);
  assert.equal(client.state.phase, "connected");
});

test("failure status and local validation are surfaced without a send", async () => {
  const device = fakeDevice();
  const client = await connected(device);
  await assert.rejects(() => client.setChannel(24, 1, 1), /out of range/);
  await assert.rejects(() => client.setChannel(0, 1, 101), /out of range/);
  await assert.rejects(() => client.setClockProfile(2, false), /Acknowledge/);
  await assert.rejects(() => client.setBank(3, 0, 0), /out of range/);
  assert.equal(device.sent.length, 0);
  device.sendFeatureReport = async function send(id, data) {
    this.result = [1, data[1], data[2], 2, 0, 0, 0, 0, 0, 0, 0, 0];
  };
  await assert.rejects(() => client.setI2cAddress(0x20), /INVALID/);
});

test("save and overclock rules shape the request flags", async () => {
  const device = fakeDevice();
  const client = await connected(device);
  await client.setClockProfile(2, true);
  assert.deepEqual(device.sent[0].slice(2, 10), [6, 0, 2, 0, 0, 0, 0, 2]);
  await client.saveConfiguration(false);
  assert.equal(device.sent[1][2], OPERATION.SAVE_CONFIG);
  assert.equal(device.sent[1][9], 1);
});
