// Take the pictures of doc/mqttviz.md from a running demo (demo.py), in a
// headless Chromium driven over the DevTools protocol -- nothing but
// Node 22 (fetch and WebSocket built in) and a Chromium to install.
//
//   node screenshots.mjs BASE-URL HOUSE.sh3d OUT-DIR [CHROME]
//
// make-screenshots.sh starts the demo and calls this; see there.
import { spawn } from "node:child_process";
import { readFileSync, writeFileSync } from "node:fs";

const [BASE, HOUSE, OUT, CHROME = "chromium"] = process.argv.slice(2);
const W = 1400, H = 820;
const DEBUG_PORT = 18092;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function api(path, body) {
  const response = await fetch(BASE + path, body === undefined ? {} : {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body) });
  if (!response.ok) throw new Error(path + ": HTTP " + response.status);
  return response.json();
}

/* -- the arrangement the pictures show --------------------------------- */

// the topology view's pins, as fractions of the workspace: the middle
// half of it is what the home camera shows
const PINS = {
  "broker": [0.5, 0.69],
  "ap:a0b1c2d3e4f5": [0.38, 0.555],
  "ap:a0b1c2d3e4f6": [0.62, 0.555],
  "panel:panel-living": [0.31, 0.385],
  "panel:panel-kitchen": [0.44, 0.385],
  "panel:panel-office": [0.57, 0.385],
  "panel:panel-bedroom": [0.70, 0.385],
};

// the space view's places in the demo house, in metres: panels on the
// inside of a wall, the access points on the hall and office floors,
// the broker on the upper floor's west wall
const PLACES = {
  "panel:panel-living": { x: 0.085, y: 1.4, z: 2.0, nx: 1, ny: 0, nz: 0,
                          level: "g" },
  "panel:panel-kitchen": { x: 7.0, y: 1.4, z: 0.085, nx: 0, ny: 0, nz: 1,
                           level: "g" },
  "panel:panel-office": { x: 6.5, y: 1.4, z: 7.915, nx: 0, ny: 0, nz: -1,
                          level: "g" },
  "panel:panel-bedroom": { x: 2.5, y: 4.0, z: 0.085, nx: 0, ny: 0, nz: 1,
                           level: "u" },
  "ap:a0b1c2d3e4f5": { x: 2.2, y: 0.2, z: 6.2, nx: 0, ny: 1, nz: 0,
                       level: "g" },
  "ap:a0b1c2d3e4f6": { x: 7.5, y: 0.2, z: 5.5, nx: 0, ny: 1, nz: 0,
                       level: "g" },
  "broker": { x: 0.085, y: 4.2, z: 6.0, nx: 1, ny: 0, nz: 0, level: "u" },
};

/* -- the browser ----------------------------------------------------------- */

const chrome = spawn(CHROME, [
  "--headless=new", "--no-sandbox", "--hide-scrollbars",
  "--enable-unsafe-swiftshader", "--use-angle=swiftshader",
  "--window-size=" + W + "," + H,
  "--remote-debugging-port=" + DEBUG_PORT, "about:blank"],
  { stdio: "ignore" });

let ws;
let seq = 0;
const pending = new Map();
const errors = [];

function send(method, params = {}) {
  return new Promise((resolve) => {
    const id = ++seq;
    pending.set(id, resolve);
    ws.send(JSON.stringify({ id, method, params }));
  });
}

async function js(expression) {
  const reply = await send("Runtime.evaluate", {
    expression, returnByValue: true, awaitPromise: true });
  return reply.result && reply.result.result && reply.result.result.value;
}

async function shot(name) {
  const reply = await send("Page.captureScreenshot", { format: "png" });
  writeFileSync(OUT + "/" + name + ".png",
                Buffer.from(reply.result.data, "base64"));
  console.log("  " + name + ".png");
}

async function open(hash = "") {
  await send("Page.navigate", { url: BASE + "/" + hash });
  await sleep(4000);
}

const click = (id) => js(`document.getElementById("${id}").click()`);
const level = (n) => js(
  `document.querySelectorAll(".space-levels .seg-btn")[${n}].click()`);

async function connect() {
  for (let i = 0; i < 50; i++) {
    try {
      const targets = await (await fetch(
        "http://127.0.0.1:" + DEBUG_PORT + "/json")).json();
      const page = targets.find((t) => t.type === "page");
      if (page) return page.webSocketDebuggerUrl;
    } catch (error) { /* not up yet */ }
    await sleep(200);
  }
  throw new Error("no Chromium on port " + DEBUG_PORT);
}

ws = new WebSocket(await connect());
await new Promise((r) => ws.addEventListener("open", r));
ws.addEventListener("message", (event) => {
  const msg = JSON.parse(event.data);
  if (msg.id && pending.has(msg.id)) {
    pending.get(msg.id)(msg);
    pending.delete(msg.id);
  }
  if (msg.method === "Runtime.exceptionThrown")
    errors.push(msg.params.exceptionDetails.exception?.description
                || msg.params.exceptionDetails.text);
});
await send("Runtime.enable");
await send("Page.enable");
await send("Emulation.setDeviceMetricsOverride",
           { width: W, height: H, deviceScaleFactor: 1, mobile: false });

try {
  await api("/api/settings", { lang: "en", show_fx: true });

  /* the mesh: the classic picture, nothing placed */
  await api("/api/settings", { view_mode: "mesh" });
  await open();
  await sleep(8000);                  // the mesh finds its shape
  await shot("mqttviz_mesh");

  /* the topology, pinned, and the panels around it */
  for (const [key, [x, y]] of Object.entries(PINS))
    await api("/api/position", { key, x, y });
  await api("/api/settings", { view_mode: "topology" });
  await open();
  await sleep(4000);
  await shot("mqttviz_topology");

  await js(`openDetail("panel-kitchen")`);
  await sleep(1000);
  await shot("mqttviz_detail");
  await click("detail-close");

  await js(`openPhysics()`);
  await sleep(1000);
  await shot("mqttviz_physics");

  await api("/api/layout/save", { name: "evening" });
  await open();
  await click("btn-layouts");
  await sleep(800);
  await shot("mqttviz_layouts");

  /* the space view: first without a house, then with one */
  await api("/api/house/delete", {});
  await api("/api/settings", { view_mode: "space", cam3d: null });
  await open();
  await shot("mqttviz_space_empty");

  const house = await fetch(BASE + "/api/house/import", {
    method: "POST", body: readFileSync(HOUSE),
    headers: { "Content-Type": "application/octet-stream",
               "X-Filename": "demo.sh3d" } });
  if (!house.ok) throw new Error("house import: HTTP " + house.status);
  for (const [key, place] of Object.entries(PLACES))
    await api("/api/position3d", { key, ...place });
  await open();
  await sleep(5000);
  await shot("mqttviz_space");

  await level(1);
  await sleep(2000);
  await shot("mqttviz_space_ground");

  await api("/api/layout/save", { name: "evening" });
} finally {
  if (errors.length) console.error("page errors:\n" + errors.join("\n"));
  ws.close();
  chrome.kill();
}
if (errors.length) process.exit(1);
