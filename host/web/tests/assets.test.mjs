import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const root = fileURLToPath(new URL("../", import.meta.url));
const html = readFileSync(join(root, "index.html"), "utf8");

test("HTML references only assets owned by the shared web directory", () => {
  for (const match of html.matchAll(/(?:href|src)="([^"]+)"/g)) {
    if (match[1] === "./") continue;
    assert.ok(!match[1].startsWith(".."), match[1]);
    assert.ok(existsSync(join(root, match[1].split("?")[0])), match[1]);
  }
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

test("module imports resolve inside the web directory", () => {
  for (const name of ["app.mjs", "view.mjs"]) {
    const file = join(root, "js", name);
    for (const match of readFileSync(file, "utf8").matchAll(/from "([^"]+)"/g)) {
      const target = resolve(dirname(file), match[1].split("?")[0]);
      assert.ok(!relative(root, target).startsWith(".."), match[1]);
      assert.ok(existsSync(target), match[1]);
    }
  }
});

test("the page may only talk to its own host service", () => {
  assert.match(html, /connect-src 'self'/);
});
