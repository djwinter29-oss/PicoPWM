import { CLOCK_PROFILES, ROLES } from "./device.mjs?v=6";

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
  if (hz >= 1e6) return `${(hz / 1e6).toLocaleString(undefined, { maximumFractionDigits: 6 })} MHz`;
  if (hz >= 1e3) return `${(hz / 1e3).toLocaleString(undefined, { maximumFractionDigits: 3 })} kHz`;
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

export function renderBootEdits(configuration) {
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
  const pending = configuration && (configuration.target.i2c_address !== configuration.running.i2c_address
    || configuration.target.roles.some((role, bank) => role !== configuration.running.roles[bank]));
  const status = document.querySelector("#boot-edits");
  status.hidden = !edited && !pending;
  status.textContent = edited ? "Unsaved edits" : pending ? "Target differs from running" : "";
  status.title = edited ? "Local edits have not been sent. Save and Reboot applies them."
    : "Bank roles or I2C address differ from the running settings. Save and Reboot applies the target.";
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
    row.append(cell(String(channel.channel)), cell(String(data.pins?.channels[channel.channel] ?? "--")),
      cell(formatFrequency(channel.freq_hz), channel.freq_hz === null ? "number missing" : "number"),
      cell(channel.duty === null ? "--" : `${channel.duty}%`, channel.duty === null ? "number missing" : "number"));
    if (editable && data.configuration?.running.roles[bank] === 0 && !data.bank_errors[bank]
      && channel.freq_hz !== null && channel.duty !== null) {
      ["frequency", "duty"].forEach((field, index) => {
        const target = row.children[index + 2];
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
  renderBootEdits(data.configuration);
  renderChannels(data, editable);
  const problems = [data.identity_error, data.configuration_error, ...data.bank_errors].filter(Boolean);
  setText("#read-errors", problems.length ? problems.join("; ") : "");
}
