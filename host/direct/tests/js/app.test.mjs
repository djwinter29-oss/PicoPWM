// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { cp, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { fileURLToPath, pathToFileURL } from "node:url";
import { promisify } from "node:util";

const execute = promisify(execFile);
const root = fileURLToPath(new URL("../../", import.meta.url));

test("browser: connect, edit, save acknowledgement, timeout recovery, disconnect", { timeout: 75000 }, async t => {
  const chromium = process.env.CHROMIUM || "chromium";
  try {
    await execute(chromium, ["--version"]);
  } catch (error) {
    if (error.code !== "ENOENT" || process.env.CHROMIUM) throw error;
    t.skip("Chromium is not installed; set CHROMIUM to require a specific executable.");
    return;
  }
  const profile = await mkdtemp(join(tmpdir(), "picopwm-browser-"));
  t.after(() => rm(profile, { recursive: true, force: true }));
  await cp(join(root, "src"), join(profile, "site", "src"), { recursive: true });
  await cp(join(root, "tests", "fixtures"), join(profile, "site", "tests", "fixtures"), { recursive: true });
  const index = join(profile, "site", "src", "index.html");
  const html = await readFile(index, "utf8");
  await writeFile(index, html.replace(/src="js\/app\.mjs[^"]*"/, 'src="../tests/fixtures/browser.mjs"'));
  const args = ["--headless", "--disable-gpu", "--no-first-run", "--enable-logging=stderr",
    `--user-data-dir=${join(profile, "profile")}`, "--allow-file-access-from-files",
    "--dump-dom", "--timeout=30000", "--virtual-time-budget=30000", pathToFileURL(index).href];
  if (process.env.CHROMIUM_NO_SANDBOX === "1") args.unshift("--no-sandbox");
  const { stdout, stderr } = await execute(chromium, args, { timeout: 65000, maxBuffer: 1024 * 1024 });
  assert.match(stdout, /data-browser-result="PASS"/,
    `${stdout.match(/<body[^>]*>/)?.[0] ?? "No page output"}\n${stdout.slice(-2500)}\n${stderr.slice(-2500)}`);
});
