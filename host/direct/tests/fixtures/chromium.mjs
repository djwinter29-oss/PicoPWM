// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

import { spawn } from "node:child_process";
import { setTimeout as delay } from "node:timers/promises";

/** Run a browser fixture to its explicit result using Chromium's debugging pipe. */
export async function runBrowser(chromium, args, url) {
  const child = spawn(chromium, [...args, "--remote-debugging-pipe", "about:blank"],
    { stdio: ["ignore", "ignore", "pipe", "pipe", "pipe"] });
  const pending = new Map();
  const diagnostics = [];
  let sequence = 0;
  let buffer = "";
  let stderr = "";
  let state = null;
  const exited = new Promise(resolve => {
    child.once("error", error => resolve({ error }));
    child.once("close", (code, signal) => resolve({ code, signal }));
  });
  const failPending = error => {
    for (const { reject } of pending.values()) reject(error);
    pending.clear();
  };
  child.once("error", failPending);
  child.once("exit", (code, signal) => failPending(new Error(`Chromium exited: ${code ?? signal}`)));
  child.stderr.setEncoding("utf8").on("data", text => { stderr = (stderr + text).slice(-6000); });
  child.stdio[4].setEncoding("utf8").on("data", text => {
    buffer += text;
    let end;
    while ((end = buffer.indexOf("\0")) !== -1) {
      const message = JSON.parse(buffer.slice(0, end));
      buffer = buffer.slice(end + 1);
      if (message.id) {
        const request = pending.get(message.id);
        pending.delete(message.id);
        if (message.error) request?.reject(new Error(JSON.stringify(message.error)));
        else request?.resolve(message.result);
      } else if (["Runtime.exceptionThrown", "Runtime.consoleAPICalled", "Log.entryAdded"].includes(message.method)) {
        diagnostics.push(message);
        if (diagnostics.length > 20) diagnostics.shift();
      }
    }
  });
  const send = (method, params = {}, sessionId) => new Promise((resolve, reject) => {
    const id = ++sequence;
    pending.set(id, { resolve, reject });
    child.stdio[3].write(JSON.stringify({ id, method, params, sessionId }) + "\0", error => {
      if (error) {
        pending.delete(id);
        reject(error);
      }
    });
  });
  child.stdio[3].on("error", failPending);
  const timer = setTimeout(() => {
    failPending(new Error("Browser fixture did not finish within 60 seconds"));
    child.kill();
  }, 60000);
  try {
    const { targetId } = await send("Target.createTarget", { url: "about:blank" });
    const { sessionId } = await send("Target.attachToTarget", { targetId, flatten: true });
    await send("Runtime.enable", {}, sessionId);
    await send("Log.enable", {}, sessionId);
    await send("Page.enable", {}, sessionId);
    const navigation = await send("Page.navigate", { url }, sessionId);
    if (navigation.errorText) throw new Error(navigation.errorText);
    while (true) {
      const { result, exceptionDetails } = await send("Runtime.evaluate", {
        expression: `({result: document.body?.dataset.browserResult,
          stage: document.body?.dataset.browserStage, html: document.documentElement?.outerHTML})`,
        returnByValue: true,
      }, sessionId);
      if (exceptionDetails) throw new Error(JSON.stringify(exceptionDetails));
      state = result.value;
      if (state.result === "FAIL") throw new Error("Browser fixture reported FAIL");
      if (state.result === "PASS") return state.html;
      await delay(50);
      if (child.exitCode !== null || child.signalCode !== null) throw new Error("Chromium stopped before completion");
    }
  } catch (error) {
    throw new Error(`${error.message}\nStage: ${state?.stage ?? "fixture not started"}\n`
      + `${state?.html?.slice(-4000) ?? ""}\n${JSON.stringify(diagnostics)}\n${stderr}`, { cause: error });
  } finally {
    clearTimeout(timer);
    // Let Chrome flush its profile and stop its workers before removing the test directory.
    if (child.exitCode === null && child.signalCode === null && child.stdio[3].writable) {
      child.stdio[3].write(JSON.stringify({ id: ++sequence, method: "Browser.close" }) + "\0");
    }
    const terminate = setTimeout(() => child.kill(), 2000);
    const cleanup = setTimeout(() => child.kill("SIGKILL"), 4000);
    await exited;
    clearTimeout(terminate);
    clearTimeout(cleanup);
  }
}
