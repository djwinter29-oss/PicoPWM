import assert from "node:assert/strict";
import test from "node:test";
import { formatUptime, renderBootEdits, renderDashboard } from "../js/view.mjs";

test("runtime uptime formatting handles second, minute, and day boundaries", () => {
  assert.equal(formatUptime(0), "0h 0m 0s");
  assert.equal(formatUptime(60), "0h 1m 0s");
  assert.equal(formatUptime(90061), "1d 1h 1m 1s");
});

class Element {
  constructor() {
    this.children = [];
    this.dataset = {};
    this.value = "";
  }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
  setAttribute(name, value) { this[name] = value; }
}

test("settings initialize from target, preserve edits during polling, and refresh after changes", () => {
  const elements = new Map();
  const previous = globalThis.document;
  globalThis.document = {
    querySelector(selector) {
      if (!elements.has(selector)) elements.set(selector, new Element());
      return elements.get(selector);
    },
    createElement() { return new Element(); },
  };
  try {
    const data = {
      identity: { clock_profile_mask: 0x1f },
      pins: { channels: Array.from({ length: 24 }, (_, channel) => 47 - channel),
        i2c_sda: 2, i2c_scl: 3, led: null },
      configuration: {
        system_clock_hz: 125_000_000,
        clock_profile: 0,
        running: { backends: [0, 1, 2], roles: [1, 1, 1], i2c_address: 64 },
        target: { backends: [0, 1, 2], roles: [0, 1, 0], i2c_address: 65 },
      },
      channels: Array.from({ length: 24 }, (_, channel) => ({
        channel, freq_hz: channel === 8 ? null : 1000, duty: channel === 8 ? null : 50,
      })),
      bank_errors: [null, "PIO read failed", null],
      runtime: { uptime_seconds: 123, temperature_c: 25.25, core0_load: 1, core1_load: 25 },
    };
    renderDashboard(data);
    assert.equal(elements.get("#uptime").textContent, "0h 2m 3s");
    assert.equal(elements.get("#temperature").textContent, "25.3 \u00b0C");
    assert.equal(elements.get("#service-load").textContent, "1.0% / 25.0%");
    for (const [bank, backend] of ["hw", "pio", "sw"].entries()) {
      const rows = elements.get(`#${backend}-channel-rows`).children;
      assert.equal(rows.length, 8);
      assert.equal(rows[0].children.length, 4);
      rows.forEach((row, index) => {
        assert.equal(row.children[1].textContent, String(data.pins.channels[bank * 8 + index]));
        const pinRow = elements.get(`#${backend}-pin-rows`).children[index];
        assert.equal(pinRow.children[1].textContent, row.children[1].textContent);
      });
      assert.equal(rows[0].children[0].textContent, String(bank * 8));
      assert.equal(rows[7].children[0].textContent, String(bank * 8 + 7));
      assert.equal(elements.get(`#${backend}-running-role`).textContent, "Monitor");
      assert.equal(elements.get(`#${backend}-running-role`).dataset.role, "monitor");
    }
    const pioRow = elements.get("#pio-channel-rows").children[0];
    assert.equal(pioRow.children[2].textContent, "--");
    assert.equal(pioRow.children[3].textContent, "--");
    assert.equal(pioRow.children[2].className, "number missing");
    assert.equal(pioRow.title, "PIO read failed");
    assert.equal(elements.get("#hw-channel-rows").children[0].children[3].textContent, "50%");
    assert.equal(elements.get("#hw-role").value, "0");
    assert.equal(elements.get("#pio-role").value, "1");
    assert.equal(elements.get("#sw-role").value, "0");
    assert.equal(elements.get("#i2c-input").value, "65");
    assert.equal(elements.get("#clock-profile-select").value, "0");
    assert.equal(elements.get("#boot-edits").textContent, "Target differs from running");
    assert.equal(elements.get("#pin-i2c_sda").textContent, 2);
    assert.equal(elements.get("#pin-led").textContent, "--");
    data.configuration.running.roles[0] = 0;
    renderDashboard(data, true);
    const generatorRow = elements.get("#hw-channel-rows").children[0];
    assert.equal(generatorRow.children[2].children[0].dataset.channel, "0");
    assert.equal(generatorRow.children[2].children[0].dataset.field, "frequency");
    assert.equal(generatorRow.children[3].children[0].dataset.field, "duty");
    assert.match(generatorRow.children[2].children[0]["aria-label"], /Edit channel 0 frequency/);
    assert.equal(elements.get("#sw-channel-rows").children[0].children[2].children.length, 0);
    data.bank_errors[0] = "Read failed";
    renderDashboard(data, true);
    assert.equal(elements.get("#hw-channel-rows").children[0].children[2].children.length, 0);
    data.bank_errors[0] = null;
    renderDashboard(data, false);
    assert.equal(elements.get("#hw-channel-rows").children[0].children[2].children.length, 0);

    elements.get("#hw-role").value = "1";
    elements.get("#i2c-input").value = "66";
    elements.get("#clock-profile-select").value = "2";
    renderBootEdits(data.configuration);
    assert.equal(elements.get("#boot-edits").textContent, "Unsaved edits");
    assert.equal(elements.get("#i2c-input").dataset.edited, "true");
    renderDashboard(data);
    assert.equal(elements.get("#hw-role").value, "1");
    assert.equal(elements.get("#i2c-input").value, "66");
    assert.equal(elements.get("#clock-profile-select").value, "2");

    data.configuration.target.i2c_address = 67;
    data.configuration.clock_profile = 3;
    renderDashboard(data);
    assert.equal(elements.get("#i2c-input").value, "67");
    assert.equal(elements.get("#clock-profile-select").value, "3");

    data.configuration = null;
    data.identity = null;
    data.pins = null;
    renderDashboard(data);
    assert.equal(elements.get("#hw-role").value, "");
    assert.equal(elements.get("#i2c-input").value, "");
    assert.equal(elements.get("#clock-profile-select").value, "");
    assert.equal(elements.get("#boot-edits").hidden, true);
    assert.equal(elements.get("#hw-running-role").textContent, "--");
    assert.equal(elements.get("#hw-running-role").dataset.role, "unknown");
    assert.equal(elements.get("#hw-channel-rows").children[0].children[1].textContent, "--");
    assert.equal(elements.get("#hw-pin-rows").children[0].children[1].textContent, "--");
    assert.match(elements.get("#pins-message").textContent, /Connect a board/);
    data.identity = { clock_profile_mask: 0 };
    renderDashboard(data);
    assert.match(elements.get("#pins-message").textContent, /Update board firmware/);
    data.pins_error = "Invalid pin assignment report";
    renderDashboard(data);
    assert.match(elements.get("#pins-message").textContent, /Invalid pin assignment/);
  } finally {
    if (previous === undefined) delete globalThis.document;
    else globalThis.document = previous;
  }
});
