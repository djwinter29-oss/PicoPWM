import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const root = fileURLToPath(new URL("../", import.meta.url));
const html = readFileSync(join(root, "index.html"), "utf8");

test("HTML references only WebHID-owned assets", () => {
  for (const match of html.matchAll(/(?:href|src)="([^"]+)"/g)) {
    if (match[1] === "./") continue;
    assert.ok(!match[1].startsWith(".."), match[1]);
    assert.ok(existsSync(join(root, match[1].split("?")[0])), match[1]);
  }
  assert.match(html, /aria-label="Host application version">0\.0\.0-debug<\/span>/);
});

test("Device information matches PicoUart field names and order", () => {
  const section = html.slice(html.indexOf('id="device-information-title"'), html.indexOf('aria-labelledby="configuration-title"'));
  const labels = [...section.matchAll(/class="summary-item"><span>([^<]+)<\/span>/g)].map(match => match[1]);
  assert.deepEqual(labels, ["Product", "Board model", "Serial / UID", "System clock",
    "Firmware RAM reserved / total", "Firmware version"]);
  assert.match(section, />Device information<\/h2>/);
});

test("channel editing uses a dialog instead of detached channel controls", () => {
  assert.doesNotMatch(html, /id="stop-all"/);
  assert.doesNotMatch(html, /channel-controls|id="set-channel"/);
  assert.match(html, /<dialog[^>]*id="channel-dialog"/);
  assert.match(html, /id="channel-frequency"[^>]*max="4294967295"/);
  assert.match(html, /id="channel-frequency-unit" aria-label="Frequency unit">[\s\S]*?<option value="MHz">MHz<\/option>/);
  assert.match(html, /id="channel-duty"[^>]*max="100"/);
  assert.match(html, /id="channel-preset" type="button">Use 1 kHz \/ 50%<\/button>/);
  assert.match(html, /id="apply-channel" type="submit">Apply/);
});

test("monitoring has three backend tables with channel/GPIO, frequency, and duty columns", () => {
  const panels = html.slice(html.indexOf('class="channel-panels"'), html.indexOf('id="read-errors"'));
  assert.equal([...panels.matchAll(/<table>/g)].length, 3);
  for (const backend of ["hw", "pio", "sw"]) {
    assert.ok(panels.includes(`id="${backend}-running-role"`));
    assert.ok(panels.includes(`id="${backend}-channel-rows"`));
  }
  assert.equal([...panels.matchAll(/<th scope="col" title="[^"]+">Channel \(GPIO\)<\/th>/g)].length, 3);
  assert.doesNotMatch(panels, /<th[^>]*>GPIO<\/th>/);
  assert.equal([...panels.matchAll(/<th scope="col">Frequency<\/th>/g)].length, 3);
  assert.equal([...panels.matchAll(/<th scope="col">Duty<\/th>/g)].length, 3);
  assert.doesNotMatch(panels, /<th[^>]*>(Bank|Role)<\/th>/);
});

test("every element the scripts query exists in the page", () => {
  const ids = new Set([...html.matchAll(/id="([^"]+)"/g)].map(match => match[1]));
  for (const name of ["app.mjs", "view.mjs"]) {
    const source = readFileSync(join(root, "js", name), "utf8");
    for (const match of source.matchAll(/["'`]#([a-z0-9-]+)["'`]/g)) {
      assert.ok(ids.has(match[1]), `${name} queries missing #${match[1]}`);
    }
  }
});

test("WebHID module imports stay within its own codebase", () => {
  for (const name of ["app.mjs", "device.mjs", "view.mjs"]) {
    const file = join(root, "js", name);
    for (const match of readFileSync(file, "utf8").matchAll(/from "([^"]+)"/g)) {
      const target = resolve(dirname(file), match[1].split("?")[0]);
      assert.ok(!relative(root, target).startsWith(".."), match[1]);
      assert.ok(existsSync(target), match[1]);
    }
  }
});

test("settings follow board order with fixed backend role controls", () => {
  const ids = ["clock-profile-select", "i2c-input", "hw-role", "pio-role", "sw-role"];
  const positions = ids.map(id => html.indexOf(`id="${id}"`));
  assert.ok(positions.every(position => position >= 0));
  assert.deepEqual(positions, [...positions].sort((a, b) => a - b));
  for (const backend of ["hw", "pio", "sw"]) {
    assert.match(html, new RegExp(`id="${backend}-role"[^>]*disabled>[\\s\\S]*?value="1">Monitor`));
  }
  assert.doesNotMatch(html, /id="bank-(index|backend|role)"/);
  assert.doesNotMatch(html, /id="apply-(hw|pio|sw|clock|i2c)"/);
  assert.match(html, /id="save-configuration"[^>]*>Save &amp; Reboot/);
  assert.match(html, /id="save-dialog-title">Save &amp; Reboot\?/);
  assert.match(html, /id="i2c-input"[^>]*min="8" max="119"/);
  assert.match(html, /for="hw-role">Hardware PWM role \(Channels 0-7\)<\/label>/);
  assert.match(html, /for="pio-role">PIO PWM role \(Channels 8-15\)<\/label>/);
  assert.match(html, /for="sw-role">Software PWM role \(Channels 16-23\)<\/label>/);
  const settings = html.slice(html.indexOf('class="settings-controls"'), html.indexOf('<details'));
  assert.doesNotMatch(settings, /Bank [ABC]|<small>/);
});

test("pin assignment is collapsed by default and populated from device reports", () => {
  assert.match(html, /<details class="pin-map-disclosure">\s*<summary>Pin assignment<\/summary>/);
  assert.ok(html.indexOf('<details') > html.indexOf('id="sw-role"'));
  assert.ok(html.indexOf('<details') < html.indexOf('id="channels-title"'));
  for (const id of ["hw", "pio", "sw"]) {
    const table = html.match(new RegExp(`<table id="${id}-pins">([\\s\\S]*?)</table>`))[1];
    assert.match(table, new RegExp(`<tbody id="${id}-pin-rows"></tbody>`));
    assert.doesNotMatch(table, /<td>\d+<\/td>/);
  }
  assert.match(html, /id="pins-message"/);
});
