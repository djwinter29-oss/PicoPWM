import { BACKENDS, CLOCK_PROFILES, ROLES } from "./labels.mjs?v=1";

const BANK_NAMES = ["A", "B", "C"];

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

function describeBank(config, bank) {
  return `${BACKENDS[config.backends[bank]]} ${ROLES[config.roles[bank]].toLowerCase()}`;
}

function renderConfiguration(data) {
  const { configuration, identity } = data;
  setText("#system-clock", configuration ? `${(configuration.system_clock_hz / 1e6).toFixed(3)} MHz` : null);
  setText("#clock-profile", configuration ? CLOCK_PROFILES[configuration.clock_profile] : null);
  setText("#i2c-address", configuration
    ? `0x${configuration.running.i2c_address.toString(16).padStart(2, "0")}`
      + (configuration.target.i2c_address === configuration.running.i2c_address ? ""
        : ` (target 0x${configuration.target.i2c_address.toString(16).padStart(2, "0")})`) : null);
  setText("#target-state", configuration
    ? configuration.target_dirty ? "Unsaved changes; saved targets apply after restart" : "Saved" : null);
  const rows = document.querySelector("#bank-config");
  rows.replaceChildren();
  BANK_NAMES.forEach((name, bank) => {
    const row = document.createElement("tr");
    row.append(cell(`Bank ${name}`), cell(configuration ? describeBank(configuration.running, bank) : "--"),
      cell(configuration ? describeBank(configuration.target, bank) : "--"));
    rows.append(row);
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
  if (!select.value && configuration && profiles.includes(configuration.clock_profile)) {
    select.value = String(configuration.clock_profile);
  }
}

function renderChannels(data) {
  const body = document.querySelector("#channel-rows");
  body.replaceChildren();
  for (const channel of data.channels) {
    const bank = Math.floor(channel.channel / 8);
    const role = data.configuration ? ROLES[data.configuration.running.roles[bank]] : "--";
    const row = document.createElement("tr");
    row.append(cell(String(channel.channel)), cell(BANK_NAMES[bank]), cell(role),
      cell(formatFrequency(channel.freq_hz), "number"),
      cell(channel.duty === null ? "--" : `${channel.duty}%`, "number"));
    if (data.bank_errors[bank]) row.title = data.bank_errors[bank];
    body.append(row);
  }
}

export function renderDashboard(data) {
  const identity = data.identity;
  setText("#product-name", identity ? "PicoPWM" : null);
  setText("#mcu", identity?.mcu);
  setText("#serial", identity?.serial_number);
  setText("#firmware", identity?.firmware_version);
  renderConfiguration(data);
  renderChannels(data);
  const problems = [data.identity_error, data.configuration_error, ...data.bank_errors].filter(Boolean);
  setText("#read-errors", problems.length ? problems.join("; ") : "");
}
