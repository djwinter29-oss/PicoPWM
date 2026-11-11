// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

import assert from "node:assert/strict";
import test from "node:test";
import { PicoPwmHost, RequestError, request } from "../src/js/client.mjs";

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

test("stalled status reads time out even if fetch ignores cancellation", async t => {
  const started = Promise.withResolvers();
  let signal;
  t.mock.method(globalThis, "fetch", (_, options) => {
    signal = options.signal;
    started.resolve();
    return new Promise(() => {});
  });
  t.mock.timers.enable({ apis: ["setTimeout"] });
  const client = new PicoPwmHost();
  client.select("a");
  const reading = client.refresh();
  await started.promise;
  t.mock.timers.tick(5001);
  await reading;
  assert.equal(signal.aborted, true);
  assert.equal(client.refreshing, false);
  assert.match(client.state.error, /timed out/);
});

test("board selection cancels a stale read and starts the new board immediately", async t => {
  let oldSignal;
  t.mock.method(globalThis, "fetch", (path, options) => {
    if (path.endsWith("board=a")) {
      oldSignal = options.signal;
      return new Promise(() => {});
    }
    return Promise.resolve(response(dashboard("B")));
  });
  const client = new PicoPwmHost();
  client.select("a");
  const stale = client.refresh();
  client.select("b");
  await client.refresh();
  await stale;
  assert.equal(oldSignal.aborted, true);
  assert.equal(client.refreshing, false);
  assert.equal(client.state.dashboard.identity.serial_number, "B");
});

test("lost write response blocks retries until fresh status and never replays the write", async t => {
  let writes = 0;
  t.mock.method(globalThis, "fetch", async (_, options) => {
    if (options.method === "POST") { writes++; throw new TypeError("Failed to fetch"); }
    return response(dashboard("A"));
  });
  const client = new PicoPwmHost();
  client.select("a");
  await client.refresh();
  await assert.rejects(client.setLed(true), error => error.uncertain && /outcome is unconfirmed/.test(error.message));
  assert.equal(client.state.phase, "disconnected");
  assert.equal(client.state.dashboard.identity, null);
  await assert.rejects(client.setLed(true), /Connect to a board/);
  assert.equal(writes, 1);
  await client.refresh();
  assert.equal(client.state.phase, "connected");
  assert.equal(writes, 1);
});

test("write and response-body deadlines release the writing state", async t => {
  t.mock.method(globalThis, "fetch", async () => response(dashboard("A")));
  const client = new PicoPwmHost();
  client.select("a");
  await client.refresh();
  const started = Promise.withResolvers();
  t.mock.method(globalThis, "fetch", async () => ({
    ok: true, status: 200, json: () => { started.resolve(); return new Promise(() => {}); },
  }));
  t.mock.timers.enable({ apis: ["setTimeout"] });
  const writing = assert.rejects(client.reboot(), /outcome is unconfirmed/);
  await started.promise;
  t.mock.timers.tick(20001);
  await writing;
  assert.equal(client.state.phase, "disconnected");
  client.select("b");
  assert.equal(client.board, "b");
});

test("parallel unauthorized requests share one login and preserve their original payloads", async t => {
  let prompts = 0;
  let logins = 0;
  const seen = new Set();
  const bodies = [];
  const originalPrompt = globalThis.prompt;
  globalThis.prompt = () => { prompts++; return "test-token"; };
  t.after(() => { globalThis.prompt = originalPrompt; });
  t.mock.method(globalThis, "fetch", async (path, options) => {
    if (path === "/api/login") { logins++; return response({ ok: true }); }
    if (!seen.has(path)) { seen.add(path); return { status: 401 }; }
    bodies.push(options.body);
    return response({});
  });
  await Promise.all([request("/api/host"), request("/api/led", { on: true })]);
  assert.equal(prompts, 1);
  assert.equal(logins, 1);
  assert.deepEqual(bodies, [undefined, '{"on":true}']);
});

test("non-JSON failures preserve HTTP status and ambiguous successful writes need recovery", async t => {
  t.mock.method(globalThis, "fetch", async () => ({
    ok: false, status: 502, json: async () => { throw new SyntaxError("HTML, not JSON"); },
  }));
  await assert.rejects(request("/api/status"), error =>
    error instanceof RequestError && error.status === 502 && /502/.test(error.message) && !error.uncertain);
  await assert.rejects(request("/api/led", { on: true }), error => error.status === 502 && error.uncertain);
  t.mock.method(globalThis, "fetch", async () => ({
    ok: true, status: 200, json: async () => { throw new SyntaxError("truncated"); },
  }));
  await assert.rejects(request("/api/led", { on: true }), error => error.status === 200 && error.uncertain);
});

test("a failed shared login is cleared so a later request can authenticate", async t => {
  const originalPrompt = globalThis.prompt;
  let prompts = 0;
  let logins = 0;
  let authenticated = false;
  globalThis.prompt = () => { prompts++; return "test-token"; };
  t.after(() => { globalThis.prompt = originalPrompt; });
  t.mock.method(globalThis, "fetch", async path => {
    if (path === "/api/login") {
      logins++;
      if (logins === 1) return { status: 401, ok: false, json: async () => ({ error: "unauthorized" }) };
      authenticated = true;
      return response({ ok: true });
    }
    return authenticated ? response({ ok: true }) : { status: 401 };
  });
  const first = await Promise.allSettled([request("/api/host"), request("/api/boards")]);
  assert.ok(first.every(result => result.status === "rejected" && result.reason.status === 401));
  assert.equal(prompts, 1);
  assert.equal(logins, 1);
  await request("/api/host");
  assert.equal(prompts, 2);
  assert.equal(logins, 2);
});

test("explicit request rejection retains connected state without replay", async t => {
  t.mock.method(globalThis, "fetch", async () => response(dashboard("A")));
  const client = new PicoPwmHost();
  client.select("a");
  await client.refresh();
  let writes = 0;
  t.mock.method(globalThis, "fetch", async () => {
    writes++;
    return { status: 400, ok: false, json: async () => ({ error: "Invalid request fields" }) };
  });
  await assert.rejects(client.setLed(true), error => error.status === 400 && !error.uncertain);
  assert.equal(client.state.phase, "connected");
  assert.equal(writes, 1);
});
