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
import { runBrowser } from "../../direct/tests/fixtures/chromium.mjs";

const execute = promisify(execFile);
const host = fileURLToPath(new URL("../../", import.meta.url));

test("browser: hosted login, board switching, editing, save and uncertain-response recovery", { timeout: 75000 }, async t => {
  const chromium = process.env.CHROMIUM || "chromium";
  try {
    await execute(chromium, ["--version"]);
  } catch (error) {
    if (error.code !== "ENOENT" || process.env.CHROMIUM) throw error;
    t.skip("Chromium is not installed; set CHROMIUM to require a specific executable.");
    return;
  }
  const temporary = await mkdtemp(join(tmpdir(), "picopwm-web-browser-"));
  t.after(() => rm(temporary, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 }));
  for (const path of ["web/src", "web/tests/fixtures", "direct/src", "direct/tests/fixtures"]) {
    await cp(join(host, path), join(temporary, path), { recursive: true });
  }
  for (const asset of ["js/view.mjs", "js/device.mjs", "css/direct.css"]) {
    await cp(join(host, "direct/src", asset), join(temporary, "web/src", asset));
  }
  const index = join(temporary, "web/src/index.html");
  await writeFile(index, (await readFile(index, "utf8"))
    .replace(/src="js\/app\.mjs[^"]*"/, 'src="../tests/fixtures/browser.mjs"'));
  const args = ["--headless", "--disable-gpu", "--no-first-run", "--enable-logging=stderr",
    `--user-data-dir=${join(temporary, "profile")}`, "--allow-file-access-from-files",
  ];
  if (process.env.CHROMIUM_NO_SANDBOX === "1") args.unshift("--no-sandbox");
  const htmlResult = await runBrowser(chromium, args, pathToFileURL(index).href);
  assert.match(htmlResult, /data-browser-result="PASS"/);
});
