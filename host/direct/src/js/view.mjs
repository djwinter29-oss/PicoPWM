// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

import { CLOCK_PROFILES, ROLES } from "./device.mjs?v=6";

const FREQUENCY_MULTIPLIERS = Object.freeze({ Hz: 1, kHz: 1_000, MHz: 1_000_000 });
const MAX_FREQUENCY_HZ = 0xffffffff;

export function frequencyEditorValue(frequencyHz) {
  const unit = frequencyHz >= FREQUENCY_MULTIPLIERS.MHz ? "MHz"
    : frequencyHz >= FREQUENCY_MULTIPLIERS.kHz ? "kHz" : "Hz";
  return { unit, value: String(frequencyHz / FREQUENCY_MULTIPLIERS[unit]) };
}

export function frequencyMax(unit) {
  return String(MAX_FREQUENCY_HZ / FREQUENCY_MULTIPLIERS[unit]);
}

export function convertFrequencyValue(value, fromUnit, toUnit) {
  const from = FREQUENCY_MULTIPLIERS[fromUnit];
  const to = FREQUENCY_MULTIPLIERS[toUnit];
  const frequency = Number(value);
  if (!from || !to || value === "" || !Number.isFinite(frequency) || frequency < 0) return "";
  return String(frequency * from / to);
}

export function frequencyToHertz(value, unit) {
  const multiplier = FREQUENCY_MULTIPLIERS[unit];
  const frequency = Number(value);
  const frequencyHz = frequency * multiplier;
  if (!multiplier || value === "" || !Number.isFinite(frequency) || frequency < 0
    || !Number.isFinite(frequencyHz) || frequencyHz > MAX_FREQUENCY_HZ + 0.000001) {
    throw new RangeError("Frequency must be between 0 and 4294967295 Hz.");
  }
  const roundedHz = Math.round(frequencyHz);
  if (Math.abs(frequencyHz - roundedHz) > 0.000001) {
    throw new RangeError("Frequency must resolve to a whole number of hertz.");
  }
  return Math.min(roundedHz, MAX_FREQUENCY_HZ);
}

function setText(selector, text) {
  document.querySelector(selector).textContent = text ?? "--";
}

function cell(text, className) {
  const td = document.createElement("td");
  if (className) td.className = className;
  td.textContent = text;
  return td;
}

export function formatFrequency(hz) {
  if (hz === null) return "--";
  if (hz >= 1e6 && hz % 1e6 === 0) return `${hz / 1e6} MHz`;
  if (hz >= 1e3 && hz % 1e3 === 0) return `${hz / 1e3} kHz`;
  return `${hz} Hz`;
}

export function formatUptime(seconds) {
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor(seconds % 86400 / 3600);
  const minutes = Math.floor(seconds % 3600 / 60);
  return `${days ? `${days}d ` : ""}${hours}h ${minutes}m ${seconds % 60}s`;
}

export function updateReportAge(data, now = Date.now()) {
  const sample = Date.parse(data.updated_at);
  const age = document.querySelector("#sample-time");
  age.hidden = !data.connected;
  const seconds = Number.isFinite(sample) ? Math.max(0, Math.floor((now - sample) / 1000)) : null;
  const stale = seconds !== null && seconds > 3;
  age.textContent = seconds === null ? "Waiting for status"
    : `Status ${seconds} s ago${stale ? " (stale)" : ""}`;
  age.dataset.stale = String(stale);
}

function syncTarget(selector, value) {
  const control = document.querySelector(selector);
  const target = value === undefined ? "" : String(value);
  if (control.dataset.target !== target) {
    control.value = target;
    control.dataset.target = target;
  }
}

export function renderBootEdits(configuration, identity) {
  const selectors = ["#clock-profile-select", "#i2c-input", "#hw-role", "#pio-role", "#sw-role"];
  const targets = configuration
    ? [configuration.clock_profile, configuration.target.i2c_address, ...configuration.target.roles] : [];
  let edited = false;
  selectors.forEach((selector, index) => {
    const control = document.querySelector(selector);
    const changed = Boolean(configuration) && control.value !== String(targets[index]);
    control.dataset.edited = String(changed);
    edited ||= changed;
  });
  const expectedClockHz = configuration?.clock_profile === 0
    ? { RP2040: 125_000_000, RP2350: 150_000_000 }[identity?.mcu]
    : [null, 150_000_000, 200_000_000, 250_000_000, 280_000_000][configuration?.clock_profile];
  const pending = Boolean(configuration && (
    configuration.system_clock_hz !== expectedClockHz
    || configuration.target.i2c_address !== configuration.running.i2c_address
    || configuration.target.roles.some((role, bank) => role !== configuration.running.roles[bank])));
  const needsSave = Boolean(edited || pending);
  const status = document.querySelector("#boot-edits");
  status.hidden = !needsSave;
  status.textContent = edited ? "Unsaved edits" : pending ? "Target differs from running" : "";
  status.title = edited ? "Local edits have not been sent. Save and Reboot applies them."
    : "Boot settings differ from the running configuration. Save and Reboot applies the target.";
  return needsSave;
}

function renderConfiguration(data) {
  const { configuration, identity } = data;
  setText("#system-clock", configuration ? `${(configuration.system_clock_hz / 1e6).toFixed(3)} MHz` : null);
  syncTarget("#i2c-input", configuration?.target.i2c_address);
  ["hw", "pio", "sw"].forEach((backend, bank) => {
    syncTarget(`#${backend}-role`, configuration?.target.roles[bank]);
  });
  const select = document.querySelector("#clock-profile-select");
  const mask = identity?.clock_profile_mask ?? 0;
  const profiles = CLOCK_PROFILES.map((_, profile) => profile).filter(profile => mask & 1 << profile);
  if (select.children.length !== profiles.length
    || profiles.some((profile, index) => select.children[index].value !== String(profile))) {
    const selected = select.value;
    select.replaceChildren(...profiles.map(profile => {
      const option = document.createElement("option");
      option.value = String(profile);
      option.textContent = CLOCK_PROFILES[profile];
      return option;
    }));
    select.value = selected;
  }
  syncTarget("#clock-profile-select", configuration && profiles.includes(configuration.clock_profile)
    ? configuration.clock_profile : undefined);
}

function renderChannels(data, editable) {
  const focused = document.activeElement?.dataset;
  const bodies = ["hw", "pio", "sw"].map((backend, bank) => {
    const role = data.configuration ? ROLES[data.configuration.running.roles[bank]] : null;
    setText(`#${backend}-running-role`, role);
    document.querySelector(`#${backend}-running-role`).dataset.role = role?.toLowerCase() ?? "unknown";
    const body = document.querySelector(`#${backend}-channel-rows`);
    body.replaceChildren();
    return body;
  });
  for (const channel of data.channels) {
    const bank = Math.floor(channel.channel / 8);
    const row = document.createElement("tr");
    const gpio = data.pins?.channels[channel.channel] ?? "--";
    row.append(cell(`${channel.channel} (GPIO ${gpio})`),
      cell(formatFrequency(channel.freq_hz), channel.freq_hz === null ? "number missing" : "number"),
      cell(channel.duty === null ? "--" : `${channel.duty}%`, channel.duty === null ? "number missing" : "number"));
    if (editable && data.configuration?.running.roles[bank] === 0 && !data.bank_errors[bank]
      && channel.freq_hz !== null && channel.duty !== null) {
      ["frequency", "duty"].forEach((field, index) => {
        const target = row.children[index + 1];
        const button = document.createElement("button");
        button.type = "button";
        button.className = "channel-value";
        button.dataset.channel = String(channel.channel);
        button.dataset.field = field;
        button.textContent = target.textContent;
        button.setAttribute("aria-label", `Edit channel ${channel.channel} ${field}: ${target.textContent}`);
        target.replaceChildren(button);
      });
    }
    if (data.bank_errors[bank]) row.title = data.bank_errors[bank];
    bodies[bank].append(row);
  }
  if (focused?.channel !== undefined && focused.field) {
    document.querySelector(`.channel-value[data-channel="${focused.channel}"][data-field="${focused.field}"]`)
      ?.focus({ preventScroll: true });
  }
}

function renderPins(data) {
  for (const [bank, backend] of ["hw", "pio", "sw"].entries()) {
    const body = document.querySelector(`#${backend}-pin-rows`);
    body.replaceChildren();
    for (let index = 0; index < 8; index++) {
      const channel = bank * 8 + index;
      const row = document.createElement("tr");
      const label = document.createElement("th");
      label.scope = "row";
      label.textContent = String(channel);
      row.append(label, cell(String(data.pins?.channels[channel] ?? "--")));
      body.append(row);
    }
  }
  for (const pin of ["i2c_sda", "i2c_scl", "led"]) {
    setText(`#pin-${pin}`, data.pins?.[pin]);
  }
  setText("#pins-message", data.pins_error ?? (data.pins ? "" : data.identity
    ? data.identity.can_read_pins ? "Waiting for pin assignment from device."
      : "Update board firmware to read pin assignment over HID."
    : "Connect a board to read its pin assignment."));
  document.querySelector("#pins-message").dataset.error = String(Boolean(data.pins_error));
}

export function renderDashboard(data, editable = false) {
  const identity = data.identity;
  setText("#product-name", identity ? "PicoPWM" : null);
  setText("#board-model", identity ? `${identity.board_model} (${identity.mcu})` : null);
  setText("#ram-usage", identity?.ram_total_bytes
    ? `${(identity.ram_reserved_bytes / 1024).toFixed(1)} / ${(identity.ram_total_bytes / 1024).toFixed(0)} KiB`
    : identity ? "Unavailable (update firmware)" : null);
  setText("#serial", identity?.serial_number);
  setText("#firmware", identity?.firmware_version);
  const runtime = data.runtime;
  setText("#uptime", runtime ? formatUptime(runtime.uptime_seconds) : null);
  setText("#temperature", runtime?.temperature_c != null ? `${runtime.temperature_c.toFixed(1)} \u00b0C` : null);
  setText("#service-load", runtime ? `${runtime.core0_load.toFixed(1)}% / ${runtime.core1_load.toFixed(1)}%` : null);
  setText("#runtime-message", data.runtime_error
    ?? (identity && !identity.can_read_runtime ? "Update board firmware to enable runtime telemetry." : ""));
  document.querySelector("#runtime-message").dataset.error = String(Boolean(data.runtime_error));
  renderConfiguration(data);
  renderPins(data);
  const bootConfigurationChanged = renderBootEdits(data.configuration, identity);
  renderChannels(data, editable);
  const problems = [data.identity_error, data.configuration_error, ...data.bank_errors].filter(Boolean);
  setText("#read-errors", problems.length ? problems.join("; ") : "");
  return bootConfigurationChanged;
}
