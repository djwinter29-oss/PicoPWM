// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

import assert from "node:assert/strict";
import test from "node:test";
import { PicoPwmHost } from "../js/client.mjs";

const dashboard = serial => ({
  connected: true, identity: { serial_number: serial }, configuration: {},
  channels: [], bank_errors: [null, null, null],
});
const response = (data, ok = true) => ({ ok, status: ok ? 200 : 409, json: async () => data });

test("board selection is page-local and stale status cannot replace the selected board", async () => {
  const original = globalThis.fetch;
  try {
    let resolve;
    globalThis.fetch = () => new Promise(done => { resolve = done; });
    const client = new PicoPwmHost();
    client.select("board-a");
    const reading = client.refresh();
    client.select("board-b");
    resolve(response(dashboard("A")));
    await reading;
    assert.equal(client.state.dashboard.identity, null);
    globalThis.fetch = async path => {
      assert.equal(path, "/api/status?board=board-b");
      return response(dashboard("B"));
    };
    await client.refresh();
    assert.equal(client.state.dashboard.identity.serial_number, "B");
    assert.equal(new PicoPwmHost().board, null);
  } finally { globalThis.fetch = original; }
});

test("actions pin the board, block switching, and invalidate earlier polling responses", async () => {
  const original = globalThis.fetch;
  try {
    const calls = [];
    globalThis.fetch = async (path, options) => {
      calls.push([path, options]);
      return response(dashboard("A"));
    };
    const client = new PicoPwmHost();
    client.select("a & b");
    await client.refresh();
    let resolve;
    globalThis.fetch = (path, options) => {
      calls.push([path, options]);
      return new Promise(done => { resolve = done; });
    };
    const action = client.setChannel(3, 1234, 25);
    client.select("wrong-board");
    assert.equal(client.board, "a & b");
    assert.equal(calls.at(-1)[0], "/api/channels/3?board=a%20%26%20b");
    assert.deepEqual(JSON.parse(calls.at(-1)[1].body), { freq_hz: 1234, duty: 25 });
    resolve(response(dashboard("A")));
    await action;
    assert.equal(client.state.phase, "connected");
    assert.throws(() => client.setChannel(24, 1, 50), /valid/);
  } finally { globalThis.fetch = original; }
});

test("save and reboot uses one confirmed API call and waits before reconnecting", async () => {
  const original = globalThis.fetch;
  try {
    const calls = [];
    globalThis.fetch = async (path, options) => {
      calls.push([path, options]);
      return response(path.startsWith("/api/status") ? dashboard("A") : { rebooting: true });
    };
    const client = new PicoPwmHost();
    client.select("a");
    await client.refresh();
    await client.saveAndReboot({ clockProfile: 2, i2cAddress: 64, roles: [0, 1, 1] }, true);
    assert.deepEqual(JSON.parse(calls.at(-1)[1].body),
      { profile: 2, address: 64, roles: [0, 1, 1], acknowledge_overclock: true });
    assert.equal(client.state.phase, "disconnected");
    assert.equal(client.state.dashboard.identity, null);
    await client.refresh();
    assert.equal(calls.length, 2);
  } finally { globalThis.fetch = original; }
});

test("a 401 prompts once, logs in, and retries the same request", async () => {
  const original = globalThis.fetch;
  const originalPrompt = globalThis.prompt;
  try {
    const calls = [];
    globalThis.prompt = () => "secret";
    globalThis.fetch = async (path, options) => {
      calls.push([path, options]);
      if (path === "/api/login") {
        assert.deepEqual(JSON.parse(options.body), { token: "secret" });
        return response({ ok: true });
      }
      if (calls.filter(([item]) => item.startsWith("/api/status")).length === 1)
        return { ok: false, status: 401, json: async () => ({ error: "unauthorized" }) };
      return response(dashboard("A"));
    };
    const client = new PicoPwmHost();
    client.select("a");
    await client.refresh();
    assert.equal(client.state.dashboard.identity.serial_number, "A");
    assert.deepEqual(calls.map(([path]) => path), ["/api/status?board=a", "/api/login", "/api/status?board=a"]);
  } finally {
    globalThis.fetch = original;
    globalThis.prompt = originalPrompt;
  }
});

test("HTTP failures surface and unavailable boards never switch to a different device", async () => {
  const original = globalThis.fetch;
  try {
    globalThis.fetch = async () => response(dashboard("A"));
    const client = new PicoPwmHost();
    client.select("a");
    await client.refresh();
    globalThis.fetch = async () => response({ error: "command failed: INVALID" }, false);
    await assert.rejects(client.setLed(true), /INVALID/);
    assert.match(client.state.error, /INVALID/);
    globalThis.fetch = async () => response({ connected: false, error: "board not found" });
    await client.refresh();
    assert.equal(client.board, "a");
    assert.equal(client.state.phase, "disconnected");
    assert.match(client.state.error, /not found/);
  } finally { globalThis.fetch = original; }
});
