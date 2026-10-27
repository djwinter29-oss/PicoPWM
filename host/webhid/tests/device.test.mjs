import assert from "node:assert/strict";
import test from "node:test";
import {
  DEVICE_FILTER, OPERATION, PicoPwmWebHid, decodeActionResult, decodeBankStatus, decodeConfiguration,
  decodeIdentity, encodeActionRequest, matchesPicoPwm,
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

function fakeDevice({ acceptSet = true, pendingPolls = 1 } = {}) {
  const device = {
    vendorId: 0xcafe, productId: 0x4012, collections: [{ usagePage: 0xff00 }], opened: false,
    sent: [], result: [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], pending: 0,
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    async receiveFeatureReport(id) {
      if (id === 1) return view(identityBytes());
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
