// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

import { decodeIdentity, decodeConfiguration, decodeBankStatus, decodePins } from "../../../direct/src/js/device.mjs";
import { fakeDevice } from "../../../direct/tests/fixtures/device.mjs";

const $ = selector => document.querySelector(selector);
const check = (condition, message) => { if (!condition) throw new Error(message); };
const waitFor = async (condition, message) => {
  document.body.dataset.browserStage = message;
  for (let attempt = 0; attempt < 600; attempt++) {
    if (condition()) return;
    await new Promise(resolve => setTimeout(resolve, 10));
  }
  throw new Error(message);
};
const input = (selector, value, event = "input") => {
  $(selector).value = value;
  $(selector).dispatchEvent(new Event(event, { bubbles: true }));
};

async function run() {
  try {
    const device = fakeDevice({ capabilities: 255, pendingPolls: 0 });
    const identity = decodeIdentity(await device.receiveFeatureReport(1));
    const configuration = decodeConfiguration(await device.receiveFeatureReport(2));
    const pins = decodePins(await device.receiveFeatureReport(5));
    const channels = [];
    for (let bank = 0; bank < 3; bank++) channels.push(...decodeBankStatus(await device.receiveFeatureReport(10 + bank), 10 + bank));
    const status = board => ({ connected: true, identity: { ...identity, serial_number: board },
      configuration, pins, channels, bank_errors: [null, null, null] });
    const reply = (data, code = 200) => ({ ok: code === 200, status: code, json: async () => structuredClone(data) });
    let loggedIn = false;
    let prompts = 0;
    let stalledSignal;
    let stallA = false;
    let loseWrite = false;
    let recovering = false;
    const writes = [];
    globalThis.prompt = () => { prompts++; return "test-token"; };
    globalThis.fetch = async (path, options = {}) => {
      if (path === "/api/login") {
        check(JSON.parse(options.body).token === "test-token", "Login payload was lost");
        loggedIn = true;
        return reply({ ok: true });
      }
      if (!loggedIn) return reply({ error: "unauthorized" }, 401);
      if (path === "/api/host") return reply({ dashboard_type: "Python", version: "1.2.3" });
      if (path === "/api/boards") return reply({ default_board_id: "A", boards: [
        { id: "A", serial_number: "A" }, { id: "B", serial_number: "B" },
      ] });
      const board = new URL(path, "http://localhost").searchParams.get("board");
      if (path.startsWith("/api/status")) {
        if (stallA && board === "A") {
          stalledSignal = options.signal;
          return new Promise(() => {});
        }
        if (recovering) return new Promise(() => {});
        return reply(status(board));
      }
      const body = JSON.parse(options.body);
      writes.push({ path, body });
      if (loseWrite) {
        recovering = true;
        throw new TypeError("Simulated lost response");
      }
      if (path.startsWith("/api/channels/")) {
        const channel = Number(path.split("/").at(-1).split("?")[0]);
        channels[channel] = { channel, freq_hz: body.freq_hz, duty: body.duty };
      }
      return reply(path.startsWith("/api/save-reboot") ? { rebooting: true } : status(board));
    };
    await import("../../src/js/app.mjs");
    await waitFor(() => !$("#toggle-led").disabled, "Initial connection failed");
    check(prompts === 1 && $("#host-version").textContent === "1.2.3", "Startup auth/version failed");
    stallA = true;
    await waitFor(() => stalledSignal, "Status polling did not start");
    input("#board-select", "B", "change");
    await waitFor(() => $("#serial").textContent === "B", "Board B was blocked by the old status read");
    check(stalledSignal.aborted, "Old board status was not canceled");
    $('.channel-value[data-channel="0"]').click();
    check($("#channel-dialog").open && document.activeElement.id === "channel-frequency", "Channel focus failed");
    $("#channel-preset").click();
    $("#apply-channel").click();
    await waitFor(() => !$("#channel-dialog").open, "Channel edit failed");
    check(writes.at(-1).path.endsWith("board=B") && channels[0].freq_hz === 1000, "Edit targeted the wrong board");
    input("#clock-profile-select", "2");
    $("#save-configuration").click();
    check($("#confirm-save").disabled && !$("#save-overclock").hidden, "Save bypassed acknowledgement");
    $("#save-unsafe-ack").click();
    $("#confirm-save").click();
    await waitFor(() => !$("#channel-dialog").open && writes.some(write => write.path.startsWith("/api/save-reboot")),
      "Save was not submitted");
    check(writes.at(-1).body.acknowledge_overclock === true, "Save lost acknowledgement");
    await waitFor(() => !$("#toggle-led").disabled, "Reboot reconnect failed");
    loseWrite = true;
    $("#toggle-led").click();
    await waitFor(() => /outcome is unconfirmed/.test($("#status-message").textContent), "Lost write was not identified");
    check($("#toggle-led").disabled && $("#save-configuration").disabled, "Unconfirmed write allowed retry");
    const count = writes.length;
    recovering = false;
    await waitFor(() => !$("#toggle-led").disabled, "Fresh status did not restore commands");
    check(writes.length === count, "Unconfirmed write was replayed");
    document.body.dataset.browserResult = "PASS";
  } catch (error) {
    document.body.dataset.browserResult = "FAIL";
    document.body.append(document.createTextNode(error.stack));
  }
}

run();
