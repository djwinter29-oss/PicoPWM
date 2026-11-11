// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

import { fakeDevice } from "./device.mjs";

const $ = selector => document.querySelector(selector);
const check = (condition, message) => { if (!condition) throw new Error(message); };
const waitFor = async (condition, message) => {
  document.body.dataset.browserStage = message;
  for (let attempt = 0; attempt < 500; attempt++) {
    if (condition()) return;
    await new Promise(resolve => setTimeout(resolve, 10));
  }
  throw new Error(message);
};
const input = (selector, value) => {
  $(selector).value = value;
  $(selector).dispatchEvent(new Event("input", { bubbles: true }));
};

async function run() {
  try {
    const device = fakeDevice({ capabilities: 255, pendingPolls: 0, applyChannelReadback: true });
    Object.defineProperty(navigator, "hid", { value: {
      addEventListener() {},
      requestDevice: async () => [device],
    } });
    await import("../../src/js/app.mjs");
    $("#connect").click();
    await waitFor(() => !$("#toggle-led").disabled, "Connect did not enable commands");
    const channel = $('.channel-value[data-channel="0"]');
    channel.click();
    check($("#channel-dialog").open, "Channel dialog did not open");
    check(document.activeElement.id === "channel-frequency", "Channel editor did not focus the frequency");
    $("#channel-preset").click();
    $("#apply-channel").click();
    await waitFor(() => !$("#channel-dialog").open, "Channel edit did not complete");
    check(device.appliedChannels.get(0)?.freq_hz === 1000, "Frequency was not sent");
    check(device.appliedChannels.get(0)?.duty === 50, "Duty was not sent");
    check($('.channel-value[data-channel="0"]').textContent.trim() === "1 kHz", "Channel readback did not render");

    input("#clock-profile-select", "2");
    $("#save-configuration").click();
    check($("#save-dialog").open, "Save dialog did not open");
    check(!$("#save-overclock").hidden && $("#confirm-save").disabled, "Overclock did not require acknowledgement");
    $("#save-unsafe-ack").click();
    check(!$("#confirm-save").disabled, "Acknowledgement did not enable confirmation");
    $("#cancel-save").click();
    check(!$("#save-dialog").open && device.sent.length === 1, "Cancel wrote settings");
    $("#save-configuration").click();
    $("#save-unsafe-ack").click();
    $("#confirm-save").click();
    await waitFor(() => $("#connect").textContent === "Connect", "Confirmed save did not disconnect");
    check(device.sent.map(request => request[2]).join(",") === "1,6,4,3,3,3,7", "Save commands were not wired correctly");
    check(device.sent.at(-1)[9] === 3 && !device.opened, "Save acknowledgement or cleanup was lost");
    $("#connect").click();
    await waitFor(() => !$("#toggle-led").disabled, "Reconnect after save failed");

    const receive = device.receiveFeatureReport.bind(device);
    device.receiveFeatureReport = id => id === 2 ? new Promise(() => {}) : receive(id);
    await waitFor(() => $("#connection-label").textContent === "Reconnect required", "Timeout did not block the UI");
    check($("#toggle-led").disabled && $("#save-configuration").disabled, "Blocked commands remained enabled");
    check(!$("#connect").disabled && $("#connect").textContent === "Disconnect", "Blocked connection cannot disconnect");
    check(/timed out/i.test($("#status-message").textContent), "Timeout was hidden");
    $("#connect").click();
    await waitFor(() => $("#connect").textContent === "Connect", "Disconnect did not complete");
    check(!device.opened, "HID device remained open");
    device.receiveFeatureReport = receive;
    $("#connect").click();
    await waitFor(() => !$("#toggle-led").disabled, "Reconnect did not restore commands");
    device.close = () => new Promise(() => {});
    $("#connect").click();
    await waitFor(() => $("#connection-label").textContent === "Recovery required", "Close timeout did not show recovery");
    check($("#connect").disabled && $("#toggle-led").disabled, "Recovery allowed another open or command");
    check(/Closing HID interface timed out.*reload/.test($("#status-message").textContent), "Close recovery guidance was hidden");
    document.body.dataset.browserResult = "PASS";
  } catch (error) {
    document.body.dataset.browserResult = "FAIL";
    document.body.append(document.createTextNode(error.stack));
  }
}

run();
