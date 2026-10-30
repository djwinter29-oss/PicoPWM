// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { join, posix, resolve, win32 } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const root = fileURLToPath(new URL("../", import.meta.url));
const html = readFileSync(join(root, "index.html"), "utf8");
const shared = new Set(["css/webhid.css", "js/view.mjs", "js/device.mjs"]);
const asset = path => {
  const key = posix.normalize(path.replaceAll("\\", "/"));
  return join(shared.has(key) ? resolve(root, "../webhid") : root, key);
};

test("shared asset lookup handles Windows separators and relative import segments", () => {
  for (const path of shared) {
    const windowsPath = win32.join(win32.dirname(path), `./${win32.basename(path)}`);
    const expected = join(resolve(root, "../webhid"), path);
    assert.equal(asset(windowsPath), expected);
    assert.equal(asset(`${posix.dirname(path)}/./${posix.basename(path)}`), expected);
    assert.ok(existsSync(expected), path);
  }
});

test("HTML references only host assets or canonical WebHID presentation assets", () => {
  for (const match of html.matchAll(/(?:href|src)="([^"]+)"/g)) {
    if (match[1] === "./") continue;
    assert.ok(!match[1].startsWith(".."), match[1]);
    assert.ok(existsSync(asset(match[1].split("?")[0])), match[1]);
  }
});

test("every element the scripts query exists in the page", () => {
  const ids = new Set([...html.matchAll(/id="([^"]+)"/g)].map(match => match[1]));
  for (const name of ["app.mjs", "view.mjs"]) {
    const source = readFileSync(asset(`js/${name}`), "utf8");
    for (const match of source.matchAll(/["'`]#([a-z0-9-]+)["'`]/g)) {
      assert.ok(ids.has(match[1]), `${name} queries missing #${match[1]}`);
    }
  }
});

test("module imports resolve to served host or WebHID assets", () => {
  for (const name of ["app.mjs", "client.mjs", "view.mjs"]) {
    const file = asset(`js/${name}`);
    for (const match of readFileSync(file, "utf8").matchAll(/from "([^"]+)"/g)) {
      const path = join("js", match[1].split("?")[0]);
      const target = asset(path);
      assert.ok(existsSync(target), match[1]);
    }
  }
});

test("host dashboard matches WebHID sections and controls except browser pairing", () => {
  const hid = readFileSync(resolve(root, "../webhid/index.html"), "utf8");
  const stylesheets = source => [...source.matchAll(/href="(css\/webhid\.css\?v=\d+)"/g)]
    .map(match => match[1]);
  assert.equal(stylesheets(html).length, 1);
  assert.deepEqual(stylesheets(html), stylesheets(hid));
  const ids = source => [...source.matchAll(/id="([^"]+)"/g)].map(match => match[1]);
  const hostIds = new Set(ids(html));
  for (const id of ids(hid).filter(id => id !== "connect")) assert.ok(hostIds.has(id), id);
  for (const id of ["board-select", "rescan-boards"]) assert.ok(hostIds.has(id), id);
  assert.doesNotMatch(html, /id="stop-all"|id="bank-index"|id="apply-bank"/);
  const pinMap = source => source.match(/<details class="pin-map-disclosure">([\s\S]*?)<\/details>/)[1];
  assert.equal(pinMap(html).replace(/\s+/g, " "), pinMap(hid).replace(/\s+/g, " "));
});

test("the page may only talk to its own host service", () => {
  assert.match(html, /connect-src 'self'/);
});
