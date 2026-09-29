/* OhEzTouch MQTT Visualizer -- the page's logic.
 *
 * Everything comes from /api/state, polled once a second; the console's
 * log comes from /api/log with a cursor, so entries are fetched once and
 * in order. The devices are objects on a canvas: boxes that spring to an
 * orbit around the broker, drift a little while idle, repel each other,
 * and can be picked up and thrown. Every message a panel publishes
 * becomes a particle flowing from its box to the broker.
 */
"use strict";

/* ---------------------------------------------------------------- helpers */

function $(id) { return document.getElementById(id); }

async function api(path, body) {
  const options = body === undefined
    ? {}
    : { method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body) };

  const response = await fetch(path, options);
  const doc = await response.json().catch(() => ({}));

  if (!response.ok) {
    const message = doc.error
      ? (typeof doc.error === "string" ? doc.error : JSON.stringify(doc.error))
      : ("HTTP " + response.status);
    throw new Error(message);
  }

  return doc;
}

let toastTimer = null;

function toast(message, kind) {
  const el = $("toast");
  el.textContent = message;
  el.className = kind || "";
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.add("hidden"), 4000);
}

function esc(text) {
  const span = document.createElement("span");
  span.textContent = text == null ? "" : String(text);
  return span.innerHTML;
}

function fmtUptime(seconds) {
  const n = Number(seconds);
  if (!isFinite(n) || n <= 0) return "-";
  const d = Math.floor(n / 86400);
  const h = Math.floor((n % 86400) / 3600);
  const m = Math.floor((n % 3600) / 60);
  if (d > 0) return d + "d " + h + "h";
  if (h > 0) return h + "h " + m + "m";
  if (m > 0) return m + "m";
  return Math.floor(n) + "s";
}

function fmtBytes(bytes) {
  const n = Number(bytes);
  if (!isFinite(n) || n <= 0) return "-";
  if (n >= 1024 * 1024) return (n / 1048576).toFixed(1) + "M";
  return Math.round(n / 1024) + "K";
}

function fmtAge(seconds) {
  const n = Number(seconds);
  if (!isFinite(n) || n < 0) return "-";
  if (n < 60) return Math.round(n) + "s";
  if (n < 3600) return Math.floor(n / 60) + "m";
  if (n < 86400) return Math.floor(n / 3600) + "h";
  return Math.floor(n / 86400) + "d";
}

function trunc(text, max) {
  text = String(text == null ? "" : text);
  return text.length > max ? text.slice(0, max - 1) + "…" : text;
}

const LED_COLORS = {
  red: "#e06a5f", green: "#7ec97e", blue: "#99c2ff",
};

const SOUND_NAMES = [
  "press", "tick", "tick_back", "toggle_on", "toggle_off", "change",
  "accept", "cancel", "link", "link_back", "screen", "screen_out",
  "notify", "warning", "error", "boot", "wake", "door_chime",
];

/* ------------------------------------------------------------------ state */

const state = {
  settings: {},
  devices: [],
  beacons: [],
  byHost: {},
  byAddr: {},
  broker: { connected: false, host: "", port: 0, messages_in: 0 },
  pollTimer: null,
  failed: false,
};

const consoleState = {
  since: 0,
  paused: false,
  levels: new Set(["info", "warn", "error"]),
  entries: [],
  unseen: 0,
};

async function pollState() {
  try {
    const doc = await api("/api/state");
    state.settings = doc.settings;
    state.broker = doc.broker;
    state.byHost = {};
    for (const dev of doc.devices) state.byHost[dev.host] = dev;
    state.devices = doc.devices;
    state.byAddr = {};
    for (const beacon of doc.beacons || []) state.byAddr[beacon.addr] = beacon;
    state.beacons = doc.beacons || [];
    state.failed = false;

    /* The server keeps the language for every browser that opens the
     * tool; this one remembers it too, so the two only meet here. */
    if (doc.settings.lang && doc.settings.lang !== lang) {
      setLang(doc.settings.lang);
    }

    /* The camera is adopted once, where the last session left it
     * looking; after that the hands own it, and it is saved when it
     * settles. */
    if (!camAdopted && doc.settings.cam) {
      camAdopted = true;
      const c = clampCam(Number(doc.settings.cam.zoom) || 1,
                         Number(doc.settings.cam.x), Number(doc.settings.cam.y));
      cam.zoom = cam.tZoom = c.zoom;
      cam.x = cam.tx = c.x;
      cam.y = cam.ty = c.y;
    }
  } catch (error) {
    if (!state.failed) {
      state.failed = true;
      toast(t("toast.unreachable") + error.message, "err");
    }
  }

  syncBoxes();
  renderBrokerChip();
  renderBrokerSelect();
  renderViewToggles();
  if (!$("detail").classList.contains("hidden")) renderDetail();
  /* While the physics panel is open, the sliders own the feel: the
   * server still says what was saved, and says it again every second,
   * but the canvas follows the hand on the slider until the panel
   * closes. */
  if (!$("physics").classList.contains("hidden") && physicsDraft) {
    for (const kind of PHYS_KINDS) {
      state.settings[PHYS_SETTINGS_KEY[kind]] = physicsDraft[kind];
    }
  }
  state.pollTimer = setTimeout(pollState, 1000);
}

/* ----------------------------------------------------------------- canvas */

const canvas = $("canvas");
const ctx = canvas.getContext("2d");

const BOX_W = 250;
const BEACON_W = 190;
const AP_W = 190;
const PAD = 8;
const ROWS = { header: 30, sub: 15, sys: 22, net: 18, ui: 22, sens: 30,
               out: 28, foot: 18 };
const BEACON_ROWS = { header: 26, sub: 15, rssi: 20, spark: 22, chips: 20,
                      foot: 15 };
const BEACON_PAD = 6;
const AP_ROWS = { header: 26, sub: 15, rssi: 20, foot: 15 };
const AP_PAD = 6;

const stage = { w: 0, h: 0 };
const boxes = [];
const particles = [];
const waves = [];          // expanding rings: pin shockwaves, arrivals
const ghostLinks = [];      // links whose box died, fading out
let hits = [];            // the previous frame's clickable regions
let frameTime = performance.now();
let hovered = null;        // the box under the cursor, for the lift

/* ------------------------------------------------------------- view mode */

function isTopology() {
  return state.settings.view_mode === "topology";
}

function fxOn() {
  return state.settings.show_fx !== false;
}

/* One access point key, whatever the panel published: the firmware
 * sends the MAC as lowercase hex with colons; the simulator sends its
 * own. Anything else is not an access point. */
function normaliseBssid(value) {
  if (value == null) return null;
  const bssid = String(value).trim().toLowerCase()
    .replace(/[\s:.-]/g, "");
  return /^[0-9a-f]{12}$/.test(bssid) ? bssid : null;
}

function deviceBssid(dev) {
  const record = dev.topics && dev.topics["system/bssid"];
  return record ? normaliseBssid(record.value) : null;
}

function deviceSsid(dev) {
  const record = dev.topics && dev.topics["system/ssid"];
  return record && record.value ? String(record.value) : "";
}

/* The WLAN quality of a panel, as the panels report it: rssi on the
 * scale the beacon lines use, so a strong link is a short leash. */
function deviceWifiQ(dev) {
  const rssi = Number(dev.topics && dev.topics["system/rssi"]
                     && dev.topics["system/rssi"].value);
  if (!isFinite(rssi) || rssi === 0) return 0;
  return Math.max(0, Math.min(1, (rssi + 100) / 70));
}

function pinKeyFor(box) {
  if (box.kind === "broker") return "broker";
  if (box.kind === "ap") return "ap:" + box.bssid;
  return "panel:" + box.host;
}

/* ------------------------------------------------------------------- fx */

function hexRgb(hex) {
  const n = parseInt(hex.slice(1), 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

const spriteCache = {};

/* A soft radial glow, rendered once per colour and stamped additively
 * under every box: the halo is the cheapest thing on the canvas that
 * makes it look alive. */
function glowSprite(hex) {
  if (spriteCache[hex]) return spriteCache[hex];

  const sprite = document.createElement("canvas");
  sprite.width = sprite.height = 128;
  const c = sprite.getContext("2d");
  const [r, g, b] = hexRgb(hex);
  const grad = c.createRadialGradient(64, 64, 0, 64, 64, 64);
  grad.addColorStop(0, "rgba(" + r + "," + g + "," + b + ",0.55)");
  grad.addColorStop(0.4, "rgba(" + r + "," + g + "," + b + ",0.16)");
  grad.addColorStop(1, "rgba(" + r + "," + g + "," + b + ",0)");
  c.fillStyle = grad;
  c.fillRect(0, 0, 128, 128);

  spriteCache[hex] = sprite;
  return sprite;
}

function stampGlow(x, y, size, alpha, hex) {
  ctx.save();
  ctx.globalCompositeOperation = "lighter";
  ctx.globalAlpha = Math.max(0, Math.min(1, alpha));
  ctx.drawImage(glowSprite(hex), x - size / 2, y - size / 2, size, size);
  ctx.restore();
}

function easeOutBack(t) {
  const c1 = 1.70158, c3 = c1 + 1;
  return 1 + c3 * Math.pow(t - 1, 3) + c1 * Math.pow(t - 1, 2);
}

/* One expanding ring: a pin snapping in, a particle arriving, a box
 * letting go of its pin. */
function wave(x, y, color, r1, dur) {
  if (waves.length > 40) waves.shift();
  waves.push({ x, y, color, r1, dur, t0: performance.now() });
}

/* The workspace the topology view lays out in: twice the canvas each
 * way -- four times the area -- with the classic canvas picture in its
 * middle. The stage is the workspace: every coordinate the physics
 * and the drawing speak is a point in it. The viewport is the window
 * the camera looks through. */
const WORLD_SCALE = 2;
const view = { w: 0, h: 0 };

function resize() {
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.round(rect.width * dpr);
  canvas.height = Math.round(rect.height * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  view.w = rect.width;
  view.h = rect.height;
  stage.w = rect.width * WORLD_SCALE;
  stage.h = rect.height * WORLD_SCALE;
  buildBackground();
}

new ResizeObserver(resize).observe(canvas);

/* ---------------------------------------------------------------- camera */

/* How the viewport looks at the workspace: a zoom and the workspace
 * point it is centred on, kept as fractions so the picture survives a
 * resize. It eases toward what the input asked for, the way the boxes
 * ease toward their pins -- a zoom is the workspace gliding, not
 * snapping. The mesh view keeps it home: zoom 1, centred, which shows
 * exactly the middle the canvas always was. */
const ZOOM_MIN = 0.5;        // the whole workspace at once: 4x the canvas
const ZOOM_MAX = 2.5;
const CAM_HOME = { zoom: 1, x: 0.5, y: 0.5 };

const cam = { zoom: 1, x: 0.5, y: 0.5,          // what is shown
              tZoom: 1, tx: 0.5, ty: 0.5 };     // what was asked for
let camAdopted = false;       // the settings say where to look, once

function clampCam(zoom, x, y) {
  const f = (v) => Math.max(0, Math.min(1, v));
  return { zoom: Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, zoom)),
           x: f(x), y: f(y) };
}

/* The camera as the transform speaks it: the viewport's top-left
 * corner in workspace pixels -- held inside the workspace whatever
 * the fractions say, and at the minimum zoom the viewport is the
 * workspace. */
function camView(zoom = cam.zoom, x = cam.x, y = cam.y) {
  const spanX = view.w / zoom, spanY = view.h / zoom;
  const lx = Math.max(0, Math.min(stage.w - spanX, x * stage.w - spanX / 2));
  const ly = Math.max(0, Math.min(stage.h - spanY, y * stage.h - spanY / 2));
  return { zoom, x: lx, y: ly };
}

/* One frame of the camera's motion: home in the mesh view, eased
 * toward the hand's request in the topology view. */
function camStep(dtSec) {
  if (!isTopology()) {
    cam.zoom = cam.tZoom = CAM_HOME.zoom;
    cam.x = cam.tx = CAM_HOME.x;
    cam.y = cam.ty = CAM_HOME.y;
    return;
  }
  const k = 1 - Math.exp(-dtSec * 10);
  cam.zoom += (cam.tZoom - cam.zoom) * k;
  cam.x += (cam.tx - cam.x) * k;
  cam.y += (cam.ty - cam.y) * k;
}

/* Where in the workspace the pointer points, through the camera that
 * is on the screen right now. */
function eventWorld(event) {
  const rect = canvas.getBoundingClientRect();
  const v = camView();
  return { x: v.x + (event.clientX - rect.left) / v.zoom,
           y: v.y + (event.clientY - rect.top) / v.zoom };
}

/* The wheel: the point under the cursor stays under the cursor -- the
 * workspace does not slide away while it is being inspected. A
 * trackpad's pinch arrives here too. */
canvas.addEventListener("wheel", (event) => {
  if (!isTopology()) return;
  event.preventDefault();

  const rect = canvas.getBoundingClientRect();
  const sx = event.clientX - rect.left, sy = event.clientY - rect.top;
  const now = camView();
  const wx = now.x + sx / now.zoom;
  const wy = now.y + sy / now.zoom;

  const next = clampCam(cam.tZoom * Math.exp(-event.deltaY * 0.0016),
                        cam.tx, cam.ty);
  const spanX = view.w / next.zoom, spanY = view.h / next.zoom;
  cam.tZoom = next.zoom;
  cam.tx = (wx - sx / next.zoom + spanX / 2) / stage.w;
  cam.ty = (wy - sy / next.zoom + spanY / 2) / stage.h;
  saveCamSoon();
}, { passive: false });

/* The buttons: one step in, one step out, around the middle of the
 * viewport, and home -- a glide back to the classic picture. */
function zoomBy(factor) {
  const v = camView(cam.tZoom, cam.tx, cam.ty);
  const zoom = clampCam(cam.tZoom * factor, cam.tx, cam.ty).zoom;
  const wx = v.x + view.w / (2 * v.zoom);
  const wy = v.y + view.h / (2 * v.zoom);
  cam.tZoom = zoom;
  cam.tx = wx / stage.w;
  cam.ty = wy / stage.h;
  saveCamSoon();
}

$("btn-zoom-in").addEventListener("click", () => zoomBy(1.25));
$("btn-zoom-out").addEventListener("click", () => zoomBy(0.8));
$("btn-zoom-home").addEventListener("click", () => {
  cam.tZoom = CAM_HOME.zoom;
  cam.tx = CAM_HOME.x;
  cam.ty = CAM_HOME.y;
  saveCamSoon();
});

/* The camera is a view like the others: kept with the settings, so the
 * page comes up looking where it was left -- and saved when it
 * settles, because the poll's copy would fight the hand on the
 * wheel. */
let camSaveTimer = null;

function saveCamSoon() {
  clearTimeout(camSaveTimer);
  camSaveTimer = setTimeout(() => {
    api("/api/settings", { cam: {
      zoom: Math.round(cam.tZoom * 1000) / 1000,
      x: Math.round(cam.tx * 10000) / 10000,
      y: Math.round(cam.ty * 10000) / 10000,
    } }).catch(() => {});
  }, 800);
}

/* The background is a faint dot grid, pre-rendered once and stamped
 * with a slow drift -- a HUD that breathes without ever getting loud. */
let bgPattern = null;
let bgFill = null;
let vignette = null;

function buildBackground() {
  const cell = 28;

  bgPattern = document.createElement("canvas");
  bgPattern.width = bgPattern.height = cell;
  const c = bgPattern.getContext("2d");
  c.fillStyle = "rgba(143, 161, 184, 0.055)";
  c.fillRect(cell / 2 - 0.5, cell / 2 - 0.5, 1, 1);
  c.fillStyle = "rgba(143, 161, 184, 0.028)";
  c.fillRect(0.5, cell / 2 - 0.5, 1, 1);
  c.fillRect(cell - 0.5, cell / 2 - 0.5, 1, 1);
  c.fillRect(cell / 2 - 0.5, 0.5, 1, 1);
  c.fillRect(cell / 2 - 0.5, cell - 0.5, 1, 1);
  bgFill = ctx.createPattern(bgPattern, "repeat");

  vignette = document.createElement("canvas");
  vignette.width = Math.max(1, Math.round(stage.w));
  vignette.height = Math.max(1, Math.round(stage.h));
  const v = vignette.getContext("2d");
  const grad = v.createRadialGradient(
    stage.w / 2, stage.h / 2, Math.min(stage.w, stage.h) * 0.35,
    stage.w / 2, stage.h / 2, Math.max(stage.w, stage.h) * 0.75);
  grad.addColorStop(0, "rgba(5, 7, 11, 0)");
  grad.addColorStop(1, "rgba(5, 7, 11, 0.5)");
  v.fillStyle = grad;
  v.fillRect(0, 0, stage.w, stage.h);
}

/* Which of a device's rows exist at all: only the hardware that is
 * publishing gets a row, the way only the hardware that exists
 * publishes. */
function boxRows(dev) {
  const t = (s) => dev.topics[s];
  const rows = [];

  if (t("system/target") || t("system/version")) rows.push("sub");
  if (t("system/uptime") || t("system/heap") || t("system/fps")) rows.push("sys");
  if (t("system/ip") || t("system/rssi") || t("system/quality")) rows.push("net");
  if (t("ui/night") || t("ui/backlight") || t("ui/activity")
      || t("ui/brightness")) rows.push("ui");
  if (t("sensor/temperature") || t("sensor/humidity")
      || t("sensor/pressure")) rows.push("sens");
  if (Object.keys(dev.topics).some((s) => /^(relay|led)\//.test(s)))
    rows.push("out");
  rows.push("foot");

  return rows;
}

function rowHeight(rows) {
  let h = 2 * PAD;
  for (const row of rows) h += ROWS[row];
  return h;
}

/* A beacon box has the same shape of job -- show what is known, only as
 * much as there is -- over a smaller card: name, kind, the strength it
 * is heard with, a sparkline of that strength, and whatever telemetry
 * it carried along. */
function beaconRows(beacon) {
  const rows = [];

  if (beacon.type || beacon.id) rows.push("sub");
  if (beacon.rssi != null
      || beacon.fields.distance) rows.push("rssi");
  if ((beacon.history || []).length > 1) rows.push("spark");
  if (beacon.fields.battery || beacon.fields.temperature
      || beacon.fields.power) rows.push("chips");
  rows.push("foot");

  return rows;
}

function beaconHeight(rows) {
  let h = 2 * BEACON_PAD;
  for (const row of rows) h += BEACON_ROWS[row];
  return h;
}

function boxFor(host) {
  return boxes.find((b) => b.kind === "device" && b.host === host);
}

function apBoxFor(bssid) {
  return boxes.find((b) => b.kind === "ap" && b.bssid === bssid);
}

/* The broker is a box like the others -- the hub of every link and the
 * one object that never floats: pinned, in the middle of the canvas in
 * the mesh view, wherever it was pinned in the topology view. */
let brokerBox = null;

function ensureBrokerBox(live) {
  if (!brokerBox || !boxes.includes(brokerBox)) {
    brokerBox = {
      kind: "broker",
      key: "broker",
      host: "broker",
      cx: stage.w / 2, cy: stage.h / 2,
      vx: 0, vy: 0,
      w: 190, h: 48,
      seed: Math.random() * 7,
      bornAt: performance.now(),
      activity: 0, flash: {},
      hoverEase: 0,
      drag: null, pinned: true, pin: { x: 0.5, y: 0.5 },
    };
    boxes.unshift(brokerBox);
  }
  live.add("broker");

  const stored = (state.settings.positions || {})["broker"];
  brokerBox.pinned = true;
  brokerBox.pin = (isTopology() && stored) ? stored : { x: 0.5, y: 0.5 };
}

/* A box leaves the canvas the way it arrived: it fades and shrinks over
 * a few hundred milliseconds, its links leave ghosts behind, and only
 * then is it really gone. */
function startDeath(box) {
  if (box === brokerBox) return;              // the hub never dies
  box.dyingAt = performance.now();
  const now = performance.now();

  const ghost = (x0, y0, x1, y1, hex) => {
    if (ghostLinks.length > 48) ghostLinks.shift();
    const hang = hangControl(x0, y0, x1, y1);
    ghostLinks.push({ x0, y0, cx: hang.x, cy: hang.y, x1, y1,
                      hex, t0: now });
  };

  const bp = brokerBox;
  if (box.kind === "device") {
    const uplink = (isTopology() && box.ap && apBoxFor(box.ap))
                   || (isTopology() ? null : bp);
    if (uplink && state.settings.show_broker !== false)
      ghost(uplink.cx, uplink.cy, box.cx, box.cy, PALETTE.peri);
    for (const other of boxes)
      if (other.kind === "beacon" && other.dev
          && other.dev.devices.some((d) => d.host === box.host))
        ghost(other.cx, other.cy, box.cx, box.cy, PALETTE.teal);
  } else if (box.kind === "ap") {
    if (state.settings.show_broker !== false)
      ghost(bp.cx, bp.cy, box.cx, box.cy, PALETTE.peri);
    for (const other of boxes)
      if (other.kind === "device" && other.ap === box.bssid)
        ghost(box.cx, box.cy, other.cx, other.cy, PALETTE.peri);
  } else if (box.kind === "beacon" && box.dev) {
    for (const seen of box.dev.devices) {
      const deviceBox = boxFor(seen.host);
      if (deviceBox) ghost(box.cx, box.cy, deviceBox.cx, deviceBox.cy,
                           PALETTE.teal);
    }
  }

  dyingBoxes.push(box);
}

const dyingBoxes = [];

/* Turn the polled device and beacon lists into box objects. A new box
 * is born where its links already point, so the network grows from
 * there: the springs pull, the shoves spread, and a moment later the
 * mesh has found its shape. In the topology view the access points come
 * first -- one per BSSID the panels report -- and the panels are born
 * at their access point, the beacons at the middle of the panels that
 * hear them. */
function syncBoxes() {
  const live = new Set();
  const topo = isTopology();
  const positions = state.settings.positions || {};

  ensureBrokerBox(live);

  /* -- the access points: one box per BSSID any panel reports -------- */

  if (topo) {
    const groups = new Map();
    for (const dev of state.devices) {
      const bssid = deviceBssid(dev);
      if (!bssid) continue;
      if (!groups.has(bssid)) groups.set(bssid, deviceSsid(dev));
    }

    const bssids = [...groups.keys()].sort();
    bssids.forEach((bssid, index) => {
      const key = "ap:" + bssid;
      live.add(key);

      let box = boxes.find((b) => b.key === key);
      if (!box) {
        const angle = (index / Math.max(1, bssids.length)) * Math.PI * 2
                      + (Math.random() - 0.5) * 0.4;
        const born_r = physParams("ap").brokerRest * 0.45;
        box = {
          kind: "ap",
          key,
          bssid,
          host: bssid,          // the debug dump and the pin key say who it is
          ssid: groups.get(bssid),
          cx: brokerBox.cx + Math.cos(angle) * born_r,
          cy: brokerBox.cy + Math.sin(angle) * born_r,
          vx: 0,
          vy: 0,
          w: AP_W,
          seed: Math.random() * 7,
          bornAt: performance.now(),
          rows: [],
          h: 60,
          flash: {},
          activity: 0,
          hoverEase: 0,
          panels: [],
          drag: null,
          pinned: false,
          pin: null,
        };

        const pos = positions[key];
        if (pos) {
          box.pinned = true;
          box.pin = pos;
          box.cx = pos.x * stage.w;
          box.cy = pos.y * stage.h;
        }
        boxes.push(box);
      }

      box.ssid = groups.get(bssid);
      box.panels = state.devices
        .filter((dev) => deviceBssid(dev) === bssid)
        .map((dev) => {
          const rssi = Number(dev.topics["system/rssi"]
                              && dev.topics["system/rssi"].value);
          return { host: dev.host, q: deviceWifiQ(dev),
                   rssi: isFinite(rssi) ? rssi : null,
                   online: !!dev.online };
        });
      box.rows = ["header", "sub", "rssi", "foot"];
      box.h = apHeight(box.rows);

      const pos = positions[key];
      if (pos) {
        box.pinned = true;
        box.pin = pos;
      } else {
        box.pinned = false;
      }
    });
  }

  let born = 0;
  for (const dev of state.devices) {
    live.add(dev.host);

    let box = boxes.find((b) => b.kind === "device" && b.host === dev.host);
    if (!box) {
      /* Born where its links already point: at its pinned place when it
       * has one, at its access point in the topology view, and on its
       * own slice of the ring around the broker in the mesh view -- so
       * none of them ever has to cross the fleet to find its place. */
      let sx, sy;
      const pos = positions["panel:" + dev.host];
      if (topo && pos) {
        sx = pos.x * stage.w;
        sy = pos.y * stage.h;
      } else if (topo) {
        const bssid = deviceBssid(dev);
        const apBox = bssid && apBoxFor(bssid);
        if (apBox) {
          const ang = Math.random() * Math.PI * 2;
          const r = 120 + Math.random() * 130;
          sx = apBox.cx + Math.cos(ang) * r;
          sy = apBox.cy + Math.sin(ang) * r;
        } else {
          sx = brokerBox.cx + (Math.random() - 0.5) * 260;
          sy = brokerBox.cy + (Math.random() - 0.5) * 260;
        }
      } else {
        const angle = (born / Math.max(1, state.devices.length)) * Math.PI * 2
                      + (Math.random() - 0.5) * 0.4;
        const born_r = physParams("device").brokerRest * 0.45;
        sx = brokerBox.cx + Math.cos(angle) * born_r;
        sy = brokerBox.cy + Math.sin(angle) * born_r;
      }
      box = {
        kind: "device",
        key: dev.host,
        host: dev.host,
        cx: sx,
        cy: sy,
        vx: 0,
        vy: 0,
        w: BOX_W,
        seed: Math.random() * 7,
        bornAt: performance.now(),
        rows: [],
        h: 80,
        lastTs: {},
        flash: {},
        activity: 0,
        hoverEase: 0,
        lastMsgcount: dev.msgcount,
        ledLocal: {},
        ledPublishAt: 0,
        drag: null,
        pinned: false,
        pin: null,
        ap: null,
      };
      boxes.push(box);
      born++;
    }

    box.dev = dev;
    box.ap = topo ? deviceBssid(dev) : null;
    box.rows = boxRows(dev);
    box.h = rowHeight(box.rows);

    /* A panel placed by hand stays where it was put -- but only in the
     * topology view, whose picture is places; the mesh view is links. */
    const pos = positions["panel:" + dev.host];
    if (topo && pos) {
      box.pinned = true;
      box.pin = pos;
    } else {
      box.pinned = false;
    }

    /* Anything with a new timestamp just changed on the panel: flash the
     * field, wake the link, and let a particle carry the news home. */
    let any = false;
    for (const suffix in dev.topics) {
      const ts = dev.topics[suffix].ts;
      if (box.lastTs[suffix] !== ts) {
        box.lastTs[suffix] = ts;
        box.flash[suffix] = 1;
        any = true;
      }
    }
    if (any) box.activity = 1;

    const delta = dev.msgcount - box.lastMsgcount;
    if (delta > 0) {
      box.lastMsgcount = dev.msgcount;
      box.activity = 1;

      /* A message a panel publishes travels its uplink: to its access
       * point in the topology view, straight home in the mesh view. The
       * access point answers by carrying the news on to the broker. */
      const apBox = box.ap ? apBoxFor(box.ap) : null;
      const rssi = Number(topicValue(dev, "system/rssi"));
      if (apBox) apBox.activity = Math.max(apBox.activity, 0.8);
      for (let i = 0; i < Math.min(3, delta); i++)
        spawnParticle(apBox || null, box,
                      isFinite(rssi) && rssi !== 0 ? rssi : null);
    }
  }

  /* Beacons are only turned into boxes while they are switched on -- the
   * boxes vanish from the canvas and the seats the moment they are not,
   * and the beacons keep being tracked behind the switch. */
  if (state.settings.show_beacons !== false) {
    /* A line is only a line while its hearing is fresh and strong
     * enough: one whose panel has not reported the beacon for the line
     * timeout, or whose signal is weaker than the minimum, is gone --
     * and a beacon with no lines left is gone with it. A hearing that
     * never carried a signal reading counts as the floor. */
    const params = physParams("beacon");

    for (const beacon of state.beacons) {
      const lines = beacon.devices.filter(
        (d) => d.age <= params.lineTimeout
          && (d.rssi == null ? -100 : d.rssi) >= params.minSignal);
      if (!lines.length) continue;

      const key = "beacon:" + beacon.addr;
      live.add(key);

      let box = boxes.find((b) => b.key === key);
      if (!box) {
        /* A beacon is born where its lines already point -- the middle
         * of the panels that hear it -- so it never has to cross the
         * whole mesh to find its place; one nobody has placed yet waits
         * on the outer band, where the beacons end up anyway. */
        let sx = 0, sy = 0, heard = 0;
        for (const seen of lines) {
          const panelBox = boxFor(seen.host);
          if (panelBox) { sx += panelBox.cx; sy += panelBox.cy; heard++; }
        }
        if (heard) {
          sx /= heard; sy /= heard;
        } else {
          const ang = Math.random() * Math.PI * 2;
          const band = physParams("device").brokerRest + 150;
          sx = stage.w / 2 + Math.cos(ang) * band;
          sy = stage.h / 2 + Math.sin(ang) * band;
        }
        box = {
          kind: "beacon",
          key,
          host: beacon.addr,
          cx: sx + (Math.random() - 0.5) * 60,
          cy: sy + (Math.random() - 0.5) * 60,
          vx: 0,
          vy: 0,
          w: BEACON_W,
          seed: Math.random() * 7,
          bornAt: performance.now(),
          rows: [],
          h: 60,
          lastCounts: {},
          flash: {},
          activity: 0,
          hoverEase: 0,
          trail: [],
          drag: null,
        };
        boxes.push(box);
      }

      /* The box keeps only the live lines: the drawing, the springs and
       * the particles all follow what is still connected. */
      box.dev = { ...beacon, devices: lines };
      box.rows = beaconRows(box.dev);
      box.h = beaconHeight(box.rows);

      /* A panel hearing the beacon again is a particle along that link:
       * the count says which link, so the flow follows who is listening. */
      for (const seen of lines) {
        const delta = seen.count - (box.lastCounts[seen.host] || 0);
        if (delta > 0) {
          box.lastCounts[seen.host] = seen.count;
          box.flash["*"] = 1;
          box.activity = 1;
          const deviceBox = boxFor(seen.host);
          if (deviceBox) {
            for (let i = 0; i < Math.min(2, delta); i++) {
              spawnParticle(deviceBox, box, seen.rssi);
            }
          }
        }
      }

      /* A panel that stopped seeing the beacon does not take the box with
       * it -- others may still hear it -- but the flow from it stops. */
      for (const host in box.lastCounts) {
        if (!lines.some((d) => d.host === host)) {
          delete box.lastCounts[host];
        }
      }
    }
  }

  /* What the poll no longer knows is on its way out: the box fades and
   * shrinks over the next few hundred milliseconds, its links leave
   * ghosts, and then it is really gone. */
  for (let i = boxes.length - 1; i >= 0; i--) {
    if (!live.has(boxes[i].key)) {
      startDeath(boxes[i]);
      boxes.splice(i, 1);
    }
  }

  /* The dead finish dying on their own clock. */
  const now = performance.now();
  for (let i = dyingBoxes.length - 1; i >= 0; i--) {
    if (now - dyingBoxes[i].dyingAt > 350) dyingBoxes.splice(i, 1);
  }
}

/* The rows of an access point box: a header with the network's name,
 * the BSSID, the best of its panels' signals, and who is on it. */
function apHeight(rows) {
  let h = 2 * AP_PAD;
  for (const row of rows) h += AP_ROWS[row];
  return h;
}

/* A particle travels from its box to the box it is heard by, or --
 * with no destination box -- from a panel to the broker hub. */
function spawnParticle(to, from, rssi) {
  if (particles.length > 120) particles.shift();

  /* A particle's speed says how the link is doing: a beacon heard at
   * -40 dBm sprints across, one barely scraping -90 dawdles. Broker
   * links have no radio, they are as fast as the network. */
  let speed = 1;
  if (rssi != null) {
    speed = 0.4 + 0.6 * Math.max(0, Math.min(1, (rssi + 100) / 70));
  }

  particles.push({
    from: from || null,
    to,
    t: 0,
    dur: (0.7 + Math.random() * 0.5) / speed,
    off: (Math.random() - 0.5) * 120,
  });
}

/* ---------------------------------------------------------------- physics */

/* The physics sliders. Every one is a whole number in its range -- the
 * page owns what the numbers mean, the server only keeps them. The
 * defaults are the thick oil the canvas ships with; physParams() turns
 * slider positions into the constants the motion actually uses,
 * falling back to the defaults until the first poll brings the
 * settings in. The sliders are the whole truth: what they say is what
 * the canvas does, with nothing hidden blending underneath.
 *
 * The panels and the beacons float on the same canvas but are set
 * apart: each kind reads its own sliders, so the beacons can hang on
 * looser lines than the panels, or drift on a lazier swell. */
const PHYS_DEFAULTS = {
  drag: 60,
  tether: 30,
  length: 50,
  homing: 35,
  convection: 50,
  convection_speed: 50,
  shove: 16,
  wall: 30,
  sag: 50,
  gravity: 0,
  signal_pull: 100,
  line_timeout: 90,
  min_signal: -100,
  metre_px: 60,
};

/* What each slider is and says, in the order the panel shows them. The
 * value readout prints the effective constant, not the position: the
 * number the canvas is really using. The label and the help text are
 * language keys -- the panel shows them in the language of the page,
 * and the help text is the hover of the row. Gravity is the one slider
 * that reaches below zero. */
const PHYS_SPEC = [
  { key: "drag", label: "phys.drag", help: "help.drag",
    fmt: (v) => "×" + (1 - v / 100 * 0.25).toFixed(2) + " " + t("unit.perFrame") },
  { key: "tether", label: "phys.tether", help: "help.tether",
    fmt: (v) => (v / 100 * 0.006).toFixed(4) },
  { key: "length", label: "phys.length", help: "help.length",
    fmt: (v) => "≈" + Math.round(150 + v / 100 * 250) + " px" },
  { key: "homing", label: "phys.homing", help: "help.homing",
    fmt: (v) => (v / 100 * 0.01).toFixed(4) },
  { key: "convection", label: "phys.convection", help: "help.convection",
    fmt: (v) => "×" + (v / 50).toFixed(2) },
  { key: "convection_speed", label: "phys.convection_speed",
    help: "help.convection_speed",
    fmt: (v) => "×" + (v / 50).toFixed(2) },
  { key: "shove", label: "phys.shove", help: "help.shove",
    fmt: (v) => (v / 100 * 0.5).toFixed(2) },
  { key: "wall", label: "phys.wall", help: "help.wall",
    fmt: (v) => (v / 100 * 0.02).toFixed(3) },
  { key: "sag", label: "phys.sag", help: "help.sag",
    fmt: (v) => (v / 100 * 22).toFixed(0) + t("unit.ofLength") },
  { key: "gravity", label: "phys.gravity", help: "help.gravity",
    min: -100, max: 100,
    fmt: (v) => (v >= 0 ? "+" : "") + (v / 100 * 0.08).toFixed(4)
           + " " + t("unit.grav100") },
];

/* The broker is an object in the picture too -- the hub every line
 * starts from. It never moves, so it has no drag and no tether: its
 * one setting is the gravity it carries, pulling on the whole mesh. */
const BROKER_SPEC = [
  { key: "gravity", label: "phys.gravity", help: "help.brokerGravity",
    min: -100, max: 100,
    fmt: (v) => (v >= 0 ? "+" : "") + (v / 100 * 0.08).toFixed(4)
           + " " + t("unit.grav100") },
];

/* Three settings the beacons have to themselves, appended to the ones
 * every floating thing shares: how hard a line's hearing pulls on its
 * leash, how weak a hearing may be before its line is gone, how long a
 * hearing keeps its line alive -- and the metre scale of the topology
 * view, how many pixels a reported metre is worth. The timeout is
 * measured in seconds, the minimum signal in dBm, the scale in px/m. */
const BEACON_EXTRA = [
  { key: "signal_pull", label: "phys.signalPull", help: "help.signalPull",
    fmt: (v) => "×" + (v / 100).toFixed(2) },
  { key: "min_signal", label: "phys.minSignal", help: "help.minSignal",
    min: -100, max: -30,
    fmt: (v) => v + " dBm" },
  { key: "line_timeout", label: "phys.lineTimeout", help: "help.lineTimeout",
    min: 30, max: 300,
    fmt: (v) => v + " s" },
  { key: "metre_px", label: "phys.metrePx", help: "help.metrePx",
    min: 10, max: 200,
    fmt: (v) => v + " px/m" },
];

/* The access points float on the same sliders as the panels -- they are
 * pinned or they settle, and the settling is the panels' physics. */
const AP_SPEC = PHYS_SPEC;

function physSpecFor(kind) {
  if (kind === "broker") return BROKER_SPEC;
  if (kind === "ap") return AP_SPEC;
  return kind === "beacon" ? PHYS_SPEC.concat(BEACON_EXTRA) : PHYS_SPEC;
}

const PHYS_SETTINGS_KEY = {
  device: "phys_nodes",
  ap: "phys_aps",
  beacon: "phys_beacons",
  broker: "phys_broker",
};

const PHYS_KINDS = ["device", "ap", "beacon", "broker"];

function physRowsId(kind) {
  return kind === "beacon" ? "physics-rows-beacons"
       : kind === "ap" ? "physics-rows-aps"
       : kind === "broker" ? "physics-rows-broker"
       : "physics-rows-nodes";
}

function physParams(kind) {
  const sliders = state.settings[PHYS_SETTINGS_KEY[kind] || "phys_nodes"]
                  || PHYS_DEFAULTS;
  const value = (key, low, high) => {
    const v = sliders[key];
    return typeof v === "number" ? Math.max(low, Math.min(high, v))
                                 : PHYS_DEFAULTS[key];
  };
  const gravity = value("gravity", -100, 100);

  return {
    damp: 1 - value("drag", 0, 100) / 100 * 0.25,
    /* The links are springs: this is how hard each one holds the length
     * it wants. */
    spring: value("tether", 0, 100) / 100 * 0.006,
    /* How long the links want to be: the base radius of the ring around
     * the broker, and the scale of every beacon line. */
    brokerRest: 150 + value("length", 0, 100) / 100 * 250,
    lenScale: value("length", 0, 100) / 50,
    /* A gentle pull toward the middle for whatever the links do not
     * hold -- a stray beacon, a box thrown far. */
    homePull: value("homing", 0, 100) / 100 * 0.01,
    driftAmp: value("convection", 0, 100) / 50,
    driftSpeed: value("convection_speed", 0, 100) / 50,
    ease: value("shove", 0, 100) / 100 * 0.5,
    wall: value("wall", 0, 100) / 100 * 0.02,
    sag: value("sag", 0, 100) / 100 * 0.22,
    /* Gravity of each object's own: every box carries a pull that acts
     * on every other box -- positive attracts, negative repels, zero
     * leaves each where its links hold it. The constant is the strength
     * at 100 px; the pull itself fades with distance, so a far
     * neighbour barely tugs and a near one draws the box off its
     * springs. */
    grav: gravity / 100 * 8,
    /* How much the radio says about a beacon's leashes: the spread of
     * their rest lengths by signal quality, scaled to nothing at zero
     * and to the full word of the radio at one. Beacons only. */
    beaconPull: value("signal_pull", 0, 100) / 100,
    /* The weakest hearing a line may carry, in dBm: a panel whose
     * signal falls below it is not connected. At the floor of -100
     * every line stays. Beacons only. */
    minSignal: value("min_signal", -100, -30),
    /* How long a panel's last hearing keeps its line alive, in seconds.
     * A line whose hearing is older is no line, and a beacon left with
     * no lines is no beacon. Beacons only. */
    lineTimeout: value("line_timeout", 30, 300),
    /* How many pixels a reported metre is worth, in the topology view:
     * the beacon's distance estimate is a leash in metres, and this is
     * the scale that turns it into one on the canvas. Beacons only. */
    metrePx: value("metre_px", 10, 200),
  };
}

/* How long a beacon's line to a panel wants to be. In the topology
 * view the panel's distance estimate has the first word -- a metre is
 * metrePx pixels, clamped to what the canvas can show -- and the
 * hearing's strength speaks only where the beacon reports no distance.
 * The pull factor scales how much the hearing gets to say: at zero
 * every leash wants the same length, at one the radio alone places the
 * beacon. */
function beaconRest(q, pull) {
  return 170 + (1 - q) * 300 * pull;
}

function beaconRestFor(seen, p) {
  const distance = Number(seen.distance);
  if (isTopology() && isFinite(distance) && distance > 0) {
    return Math.max(40, Math.min(900, distance * p.metrePx));
  }
  const q = seen.rssi == null ? 0
            : Math.max(0, Math.min(1, (seen.rssi + 100) / 70));
  return beaconRest(q, p.beaconPull);
}

function physicsStep(dt, time) {
  const params = { device: physParams("device"),
                   ap: physParams("ap"),
                   beacon: physParams("beacon"),
                   broker: physParams("broker") };
  const topo = isTopology();
  const bp = brokerBox;
  const cx = bp.cx, cy = bp.cy;

  const paramsFor = (box) =>
    box.kind === "beacon" ? params.beacon
    : box.kind === "ap" ? params.ap
    : box.kind === "broker" ? params.broker : params.device;

  /* The panels share the ring around the broker, and the ring must
   * have room for them all: the broker link's rest length is never
   * shorter than the radius a full circle of boxes -- with their air
   * margin -- would need. The access points have a ring of their own
   * in the topology view, and the panels a smaller one around each
   * access point. */
  let panels = 0, aps = 0;
  const perAp = {};
  for (const box of boxes) {
    if (box.kind === "device") {
      panels++;
      if (topo && box.ap) perAp[box.ap] = (perAp[box.ap] || 0) + 1;
    } else if (box.kind === "ap") {
      aps++;
    }
  }
  const brokerRest = Math.max(params.device.brokerRest,
                              panels * (BOX_W + 50) / (Math.PI * 2));
  const apRest = Math.max(params.ap.brokerRest,
                         aps * (AP_W + 50) / (Math.PI * 2));

  for (const box of boxes) {
    const p = paramsFor(box);

    if (box.drag) {
      box.vx = box.drag.cx - box.cx;
      box.vy = box.drag.cy - box.cy;
      box.cx = box.drag.cx;
      box.cy = box.drag.cy;
      continue;
    }

    /* A pinned box is where it was put. It eases toward its pin --
     * firmly enough to feel fixed, gently enough that a mode switch or
     * a window resize is a glide and not a jump. */
    if (box.pinned && box.pin) {
      const tx = box.pin.x * stage.w, ty = box.pin.y * stage.h;
      const k = 1 - Math.exp(-dt * 6);
      box.vx = 0;
      box.vy = 0;
      box.cx += (tx - box.cx) * k;
      box.cy += (ty - box.cy) * k;
      continue;
    }

    /* The broker never floats: it is pinned, above, and done. */
    if (box.kind === "broker") continue;

    /* The links are springs, and they are the layout. A beacon hangs on
     * leashes to the panels that hear it -- a strong hearing a short
     * leash, a distance estimate a leash of metres scaled to pixels. An
     * access point is held by its line to the broker; a panel by its
     * line to its access point, whose rest length follows the WLAN -- a
     * strong signal a short line, a weak one a long one. A leash only
     * ever pulls; a line whose ends are closer than it wants goes slack
     * rather than shoving, so a crowded mesh never grinds. */
    if (box.kind === "beacon" && box.dev) {
      for (const seen of box.dev.devices) {
        const deviceBox = boxFor(seen.host);
        if (!deviceBox || deviceBox === box) continue;

        const rest = beaconRestFor(seen, p) * p.lenScale;
        const dx = deviceBox.cx - box.cx, dy = deviceBox.cy - box.cy;
        const dist = Math.hypot(dx, dy);
        if (!dist) continue;

        const force = p.spring * (dist - rest) / dist * dt;
        box.vx += dx * force;
        box.vy += dy * force;
        if (dist > rest && !deviceBox.drag && !deviceBox.pinned) {
          deviceBox.vx -= dx * force;
          deviceBox.vy -= dy * force;
        }
      }
    } else if (box.kind === "ap") {
      const dx = cx - box.cx, dy = cy - box.cy;
      const dist = Math.hypot(dx, dy) || 1;
      const force = p.spring * (dist - apRest) / dist * dt;
      box.vx += dx * force;
      box.vy += dy * force;
    } else {
      let target = null;
      let rest = brokerRest;

      if (topo && box.ap) {
        const apBox = apBoxFor(box.ap);
        if (apBox) {
          target = apBox;
          const q = box.dev ? deviceWifiQ(box.dev) : 0;
          const ring = (perAp[box.ap] || 1) * (BOX_W + 50) / (Math.PI * 2);
          rest = Math.max(params.device.brokerRest * (0.55 + 0.9 * (1 - q)),
                          ring);
        }
      }

      const tx = target ? target.cx : cx;
      const ty = target ? target.cy : cy;
      const dx = tx - box.cx, dy = ty - box.cy;
      const dist = Math.hypot(dx, dy) || 1;
      const force = p.spring * (dist - rest) / dist * dt;
      box.vx += dx * force;
      box.vy += dy * force;
      if (target && dist > rest && !target.drag && !target.pinned) {
        target.vx -= dx * force;
        target.vy -= dy * force;
      }
    }

    /* The gravity of the broker itself: the hub never moves, but its
     * pull acts on every object in the picture, the same falloff as
     * any other gravity -- positive gathers the mesh toward the
     * middle, negative blows it outward, and the links answer either
     * way. */
    if (params.broker.grav !== 0) {
      const dx = cx - box.cx, dy = cy - box.cy;
      const d = Math.max(Math.hypot(dx, dy), 80) || 80;
      box.vx += dx / d * (params.broker.grav / d) * dt;
      box.vy += dy / d * (params.broker.grav / d) * dt;
    }

    /* The home pull: a whisper toward the hub, so nothing the links
     * have lost ever drifts off the canvas for good. */
    const hdx = cx - box.cx, hdy = cy - box.cy;
    const hDist = Math.hypot(hdx, hdy) || 1;
    box.vx += hdx / hDist * p.homePull * dt;
    box.vy += hdy / hDist * p.homePull * dt;

    /* The idle drift: a single slow convection, every box on its own
     * phase. Oil damps the ripples out -- what remains is one long,
     * heavy swell, and heavy oil barely convects at all. */
    box.vx += Math.cos(time * 0.07 * p.driftSpeed + box.seed * 2.1)
              * 0.0065 * p.driftAmp * dt;
    box.vy += Math.sin(time * 0.06 * p.driftSpeed + box.seed)
              * 0.0075 * p.driftAmp * dt;

    /* Soft walls: a box settles against the edge of the canvas the way
     * things in oil settle anywhere -- slowly, without bouncing. */
    const marginX = box.w / 2 + 6, marginY = box.h / 2 + 6;
    if (box.cx < marginX) box.vx += (marginX - box.cx) * p.wall * dt;
    if (box.cx > stage.w - marginX) box.vx -= (box.cx - stage.w + marginX) * p.wall * dt;
    if (box.cy < marginY) box.vy += (marginY - box.cy) * p.wall * dt;
    if (box.cy > stage.h - marginY) box.vy -= (box.cy - stage.h + marginY) * p.wall * dt;

    /* Heavy oil: drag beats momentum almost the moment it appears --
     * a shoved box travels a few of its own widths and stops, forced
     * motion creeps, and nothing ever swings, it all eases. The motion
     * is always overdamped. */
    const damp = p.damp;
    box.vx *= Math.pow(damp, dt);
    box.vy *= Math.pow(damp, dt);
    box.cx += box.vx * dt;
    box.cy += box.vy * dt;
  }

  /* Gravity between the objects, and the separation of the ones that
   * touch: a pass of its own, so a pinned box -- which skipped its
   * springs above -- is still a mass the others feel and still a wall
   * they are pushed off. */
  for (let i = 0; i < boxes.length; i++) {
    const box = boxes[i];
    const p = paramsFor(box);
    const stillBox = box.drag || box.pinned;

    for (let j = i + 1; j < boxes.length; j++) {
      const other = boxes[j];
      const q = paramsFor(other);

      const needX = (box.w + other.w) / 2 + 20;
      const needY = (box.h + other.h) / 2 + 20;
      const dx = other.cx - box.cx, dy = other.cy - box.cy;
      const overlapX = needX - Math.abs(dx);
      const overlapY = needY - Math.abs(dy);

      /* Every object carries a gravity of its own, and it acts on every
       * other object, whatever kind either is: each pulls the other
       * toward itself -- or, at minus, pushes it away. The pull fades
       * with distance and never grows past arm's length, so a clump
       * settles as boxes that lean toward each other and rest, held by
       * their springs, not a collapse into one point. */
      if (p.grav !== 0 || q.grav !== 0) {
        const d = Math.max(Math.hypot(dx, dy), 80) || 80;
        const ax = dx / d, ay = dy / d;      // unit vector, box -> other
        if (!other.drag && !other.pinned) {
          other.vx -= ax * (p.grav / d) * dt;  // this box's pull on it
          other.vy -= ay * (p.grav / d) * dt;
        }
        if (!stillBox) {
          box.vx += ax * (q.grav / d) * dt;    // its pull on this box
          box.vy += ay * (q.grav / d) * dt;
        }
      }

      if (overlapX > 0 && overlapY > 0) {
        /* A held or pinned box is carried or fixed, never pushed;
         * everything else shares the work. */
        const weightBox = stillBox ? 0 : 1;
        const weightOther = (other.drag || other.pinned) ? 0 : 1;
        const total = weightBox + weightOther;
        if (!total) continue;

        /* Heavy oil gives way almost reluctantly: the separation is a
         * slow, heavy shove -- two boxes that touch ooze apart, taking
         * their time about it. */
        const ease = Math.min(1, dt) * p.ease;
        const dirX = dx >= 0 ? 1 : -1;
        const dirY = dy >= 0 ? 1 : -1;
        const pushX = dirX * overlapX * ease;
        const pushY = dirY * overlapY * ease;

        box.cx -= pushX * weightBox / total;
        box.cy -= pushY * weightBox / total;
        other.cx += pushX * weightOther / total;
        other.cy += pushY * weightOther / total;

        /* A nudge in the same direction, so the flow keeps some of it. */
        if (weightBox) {
          box.vx -= dirX * 0.12 * ease * weightBox / total;
          box.vy -= dirY * 0.12 * ease * weightBox / total;
        }
        if (weightOther) {
          other.vx += dirX * 0.12 * ease * weightOther / total;
          other.vy += dirY * 0.12 * ease * weightOther / total;
        }
      }
    }
  }

  /* A mobile thing leaves a trail: the last few positions of every
   * beacon, remembered so the canvas can draw where it has been. */
  for (const box of boxes) {
    if (box.kind !== "beacon" || !box.trail) continue;
    const speed = Math.hypot(box.vx, box.vy);
    box.trail.push({ x: box.cx, y: box.cy, s: speed });
    if (box.trail.length > 16) box.trail.shift();
  }
}

/* ------------------------------------------------------------------ paint */

function rr(x, y, w, h, r) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.closePath();
}

function chip(x, y, w, h, fill, edge) {
  rr(x, y, w, h, Math.min(6, h / 2));
  ctx.fillStyle = fill;
  ctx.fill();
  if (edge) {
    ctx.strokeStyle = edge;
    ctx.lineWidth = 1;
    ctx.stroke();
  }
}

function text(str, x, y, font, color, align) {
  ctx.font = font;
  ctx.fillStyle = color;
  ctx.textAlign = align || "left";
  ctx.textBaseline = "middle";
  ctx.fillText(str, x, y);
}

/* The lines are not taut: gravity gets a say, and every link hangs below
 * the straight path between its two ends, the way a cable does -- more
 * slack the longer the cable, clamped so that a short hop still looks
 * pulled tight. The curve is a quadratic whose control point sits below
 * the midpoint; its deepest point is half the sag, at half the way. */
function hangControl(x0, y0, x1, y1, kind) {
  const len = Math.hypot(x1 - x0, y1 - y0);
  const p = physParams(kind === "beacon" ? "beacon"
                          : kind === "ap" ? "ap" : "device");
  const sag = Math.min(55, len * p.sag);
  return { x: (x0 + x1) / 2, y: (y0 + y1) / 2 + sag };
}

const MONO = (px) => px + "px ui-monospace, 'Cascadia Mono', monospace";
const SANS = (px, weight) => (weight ? weight + " " : "")
  + px + "px 'Segoe UI', system-ui, sans-serif";

const PALETTE = {
  bg: "#0b0d12",
  panel: "#12161f",
  edge: "#2a3442",
  text: "#dfe6f0",
  dim: "#8fa1b8",
  orange: "#ff9c00",
  orangeDark: "#d47700",
  peri: "#99c2ff",
  teal: "#5fc9b0",
  good: "#7ec97e",
  bad: "#e06a5f",
};

function topicValue(dev, suffix) {
  const record = dev.topics[suffix];
  return record ? record.value : null;
}

function isOn(value) {
  return value != null
    && ["ON", "TRUE", "YES", "1", "ONLINE"].includes(String(value).toUpperCase());
}

/* ------------------------------------------------------- the fx pipeline */

/* A box's birth: from nothing to full size in four hundred milliseconds,
 * with a little overshoot -- the way things settle, not the way they
 * snap. */
function birthT(box, time) {
  if (!box.bornAt) return 1;
  return Math.max(0, Math.min(1, (time - box.bornAt) / 400));
}

function easeOut(t) {
  return 1 - Math.pow(1 - t, 3);
}

/* Which end of a new link reaches out, and how far it has come: the
 * younger of the two boxes grows the line from itself. */
function linkGrow(a, b, time) {
  const younger = (a.bornAt || 0) >= (b.bornAt || 0) ? a : b;
  return { t: birthT(younger, time), reverse: younger === b };
}

/* A hanging quadratic, drawn whole or as the first part of it -- a
 * link that is being born reaches out from its origin. */
function linkPath(x0, y0, c, x1, y1, t) {
  ctx.moveTo(x0, y0);
  if (t >= 1) {
    ctx.quadraticCurveTo(c.x, c.y, x1, y1);
    return;
  }
  const q = (a, b, u) => a + (b - a) * u;
  const cx1 = q(x0, c.x, t), cy1 = q(y0, c.y, t);
  const ex = q(q(x0, c.x, t), q(c.x, x1, t), t);
  const ey = q(q(y0, c.y, t), q(c.y, y1, t), t);
  ctx.quadraticCurveTo(cx1, cy1, ex, ey);
}

/* One link, the way the canvas draws them all: a soft additive glow
 * under a thin bright core, growing out of whichever end is younger. */
function drawLink(x0, y0, x1, y1, kind, opts) {
  if (opts.grow <= 0) return;
  const c = hangControl(x0, y0, x1, y1, kind);

  const paint = () => {
    ctx.beginPath();
    linkPath(x0, y0, c, x1, y1, opts.grow);
    ctx.strokeStyle = opts.color;
    ctx.lineWidth = opts.width || 1;
    ctx.setLineDash(opts.dash || []);
    ctx.lineDashOffset = opts.dashOffset || 0;
    ctx.stroke();
    ctx.setLineDash([]);
  };

  if (opts.glow) {
    ctx.save();
    ctx.globalCompositeOperation = "lighter";
    ctx.beginPath();
    linkPath(x0, y0, c, x1, y1, opts.grow);
    ctx.strokeStyle = opts.glow;
    ctx.lineWidth = (opts.width || 1) * 3.2;
    ctx.setLineDash(opts.dash || []);
    ctx.lineDashOffset = opts.dashOffset || 0;
    ctx.stroke();
    ctx.restore();
  }

  paint();
}

function drawBackground(time) {
  if (!fxOn() || !bgFill) return;
  const cell = 28;
  const ox = (time * 0.004) % cell, oy = (time * 0.0023) % cell;
  ctx.save();
  ctx.translate(-ox, -oy);
  ctx.fillStyle = bgFill;
  ctx.fillRect(0, 0, stage.w + cell, stage.h + cell);
  ctx.restore();
  if (vignette) ctx.drawImage(vignette, 0, 0);
}

/* A radar sweep around the hub: faint, slow, and only there when the
 * effects are on and the broker is in the picture. */
function drawSweep(time) {
  if (!fxOn() || !ctx.createConicGradient || !brokerBox) return;
  const r = Math.max(stage.w, stage.h) * 0.55;
  const grad = ctx.createConicGradient(time * 0.00035,
                                       brokerBox.cx, brokerBox.cy);
  grad.addColorStop(0, "rgba(255, 156, 0, 0)");
  grad.addColorStop(0.9, "rgba(255, 156, 0, 0)");
  grad.addColorStop(0.97, "rgba(255, 156, 0, 0.045)");
  grad.addColorStop(1, "rgba(255, 156, 0, 0)");
  ctx.fillStyle = grad;
  ctx.beginPath();
  ctx.moveTo(brokerBox.cx, brokerBox.cy);
  ctx.arc(brokerBox.cx, brokerBox.cy, r, 0, Math.PI * 2);
  ctx.fill();
}

function drawGhosts(time) {
  for (let i = ghostLinks.length - 1; i >= 0; i--) {
    const gh = ghostLinks[i];
    const u = (time - gh.t0) / 350;
    if (u >= 1) {
      ghostLinks.splice(i, 1);
      continue;
    }
    const [r, g, b] = hexRgb(gh.hex);
    ctx.beginPath();
    ctx.moveTo(gh.x0, gh.y0);
    ctx.quadraticCurveTo(gh.cx, gh.cy, gh.x1, gh.y1);
    ctx.strokeStyle = "rgba(" + r + "," + g + "," + b + ","
                      + (0.35 * (1 - u)) + ")";
    ctx.lineWidth = 1;
    ctx.stroke();
  }
}

/* Where a mobile thing has been: the last positions of every beacon,
 * stamped additively and fading behind it as it moves. */
function drawTrails() {
  if (!fxOn()) return;
  ctx.save();
  ctx.globalCompositeOperation = "lighter";
  const [r, g, b] = hexRgb(PALETTE.teal);
  for (const box of boxes) {
    if (box.kind !== "beacon" || !box.trail) continue;
    for (let i = 0; i < box.trail.length; i++) {
      const pt = box.trail[i];
      if (pt.s < 0.03) continue;
      const u = (i + 1) / box.trail.length;
      ctx.fillStyle = "rgba(" + r + "," + g + "," + b + ","
                      + (0.22 * u * Math.min(1, pt.s * 3)) + ")";
      ctx.beginPath();
      ctx.arc(pt.x, pt.y, 1 + 2.2 * u, 0, Math.PI * 2);
      ctx.fill();
    }
  }
  ctx.restore();
}

/* The halo under every box: a soft light in the colour of its kind,
 * breathing on its own phase, brighter while it talks or lifts. */
function drawHalos(time) {
  if (!fxOn()) return;
  for (const box of boxes) {
    if (box.kind === "broker") continue;          // the hub glows on its own
    const hex = box.kind === "beacon" ? PALETTE.teal
              : box.kind === "ap" ? PALETTE.peri : PALETTE.orange;
    const size = Math.max(box.w, box.h) * 1.55
                 * (1 + 0.05 * Math.sin(time * 0.0011 + box.seed * 6));
    const alpha = (0.10 + 0.22 * box.activity + 0.12 * box.hoverEase)
                  * (0.3 + 0.7 * birthT(box, time));
    stampGlow(box.cx, box.cy, size, alpha, hex);
  }
}

function drawWaves(time) {
  for (let i = waves.length - 1; i >= 0; i--) {
    const wv = waves[i];
    const u = (time - wv.t0) / wv.dur;
    if (u >= 1) {
      waves.splice(i, 1);
      continue;
    }
    const [r, g, b] = hexRgb(wv.color);
    ctx.beginPath();
    ctx.arc(wv.x, wv.y, 4 + wv.r1 * easeOut(u), 0, Math.PI * 2);
    ctx.strokeStyle = "rgba(" + r + "," + g + "," + b + ","
                      + (0.55 * (1 - u)) + ")";
    ctx.lineWidth = 1.5;
    ctx.stroke();
  }
}

/* A box that left the poll finishes leaving on its own clock: it fades
 * and shrinks for a few hundred milliseconds before it is really
 * gone. Its links are ghosts by then; the box follows. */
function drawDying(time) {
  const liveHits = hits;
  hits = [];

  for (const box of dyingBoxes) {
    const u = Math.min(1, (time - box.dyingAt) / 350);
    ctx.save();
    ctx.globalAlpha = 1 - u;
    const s = 1 - 0.3 * u;
    ctx.translate(box.cx, box.cy);
    ctx.scale(s, s);
    ctx.translate(-box.cx, -box.cy);
    if (box.kind === "beacon") drawBeacon(box, time);
    else if (box.kind === "ap") drawAp(box, time);
    else drawBox(box, time);
    ctx.restore();
  }

  hits = liveHits;
}

/* Everything a box does to the canvas happens through its own scale:
 * born overshooting, breathing on its own phase, lifting under the
 * cursor. The scale never moves the box -- physics owns the position. */
function boxTransform(box, time, fn) {
  let s = easeOutBack(birthT(box, time));
  if (fxOn()) s *= 1 + 0.008 * Math.sin(time * 0.0012 + box.seed * 6);
  s *= 1 + 0.03 * (box.hoverEase || 0);
  if (Math.abs(s - 1) < 0.001) {
    fn();
    return;
  }
  ctx.save();
  ctx.translate(box.cx, box.cy);
  ctx.scale(s, s);
  ctx.translate(-box.cx, -box.cy);
  fn();
  ctx.restore();
}

/* A pinned box carries a small reticle: the mark of a place kept, and
 * the thing to click to let it go again. */
function drawPinGlyph(box, x, y) {
  if (!isTopology() || !box.pinned) return;
  const gx = x + box.w - 16, gy = y + 4;
  ctx.save();
  ctx.strokeStyle = "rgba(255, 170, 40, 0.9)";
  ctx.lineWidth = 1.2;
  ctx.beginPath();
  ctx.arc(gx + 5, gy + 5, 4.2, 0, Math.PI * 2);
  ctx.stroke();
  for (const [dx, dy] of [[0, -1], [0, 1], [-1, 0], [1, 0]]) {
    ctx.beginPath();
    ctx.moveTo(gx + 5 + dx * 5.4, gy + 5 + dy * 5.4);
    ctx.lineTo(gx + 5 + dx * 8, gy + 5 + dy * 8);
    ctx.stroke();
  }
  ctx.restore();
  hits.push({ type: "pin", box, x: gx - 3, y: gy - 3, w: 16, h: 16 });
}

function bssidText(bssid) {
  return String(bssid).replace(/(..)(?=.)/g, "$1:");
}

/* ------------------------------------------------------------------ draw */

function draw(time) {
  const topo = isTopology();
  const showBroker = state.settings.show_broker !== false;

  /* -- the camera ------------------------------------------------------- */

  /* The viewport clears whole, and then looks at the workspace through
   * the camera: everything below is drawn in workspace pixels, and the
   * pointer finds its way back through the same transform. */
  const dpr = window.devicePixelRatio || 1;
  const dtSec = Math.max(0, Math.min(0.05, (time - frameTime) / 1000));
  camStep(dtSec);
  const v = camView();
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, view.w, view.h);
  ctx.setTransform(dpr * v.zoom, 0, 0, dpr * v.zoom,
                   -dpr * v.zoom * v.x, -dpr * v.zoom * v.y);

  hits = [];

  /* -- the breath of the picture ------------------------------------ */

  for (const box of boxes) {
    box.activity = Math.max(0, box.activity - 0.008);
    for (const suffix in box.flash)
      box.flash[suffix] = Math.max(0, box.flash[suffix] - 0.015);
    box.hoverEase = (box.hoverEase || 0)
      + (((box === hovered && !box.drag) ? 1 : 0) - (box.hoverEase || 0))
        * 0.12;
  }

  drawBackground(time);
  drawGhosts(time);

  /* -- links ---------------------------------------------------------- */

  /* The radio links, beacon to panel. The strength of the line is the
   * strength of the hearing: -40 dBm is a fat, bright, steady stroke,
   * -90 dBm a thread that is barely there. A slow pulse on the strong
   * ones keeps the mesh alive without shouting. */
  for (const box of boxes) {
    if (box.kind !== "beacon" || !box.dev) continue;

    for (const seen of box.dev.devices) {
      const deviceBox = boxFor(seen.host);
      if (!deviceBox) continue;

      const rssi = seen.rssi;
      const q = rssi == null ? 0
                : Math.max(0, Math.min(1, (rssi + 100) / 70));
      const grow = linkGrow(box, deviceBox, time);
      let x0 = box.cx, y0 = box.cy;
      let x1 = deviceBox.cx, y1 = deviceBox.cy;
      if (grow.reverse) {
        x0 = x1; y0 = y1;
        x1 = box.cx; y1 = box.cy;
      }

      const mix = (a, b) => Math.round(a + (b - a) * q);
      const pulse = 0.06 * q * (1 + Math.sin(time * 0.003 + box.seed * 4));
      const asleep = box.dev.active ? 1 : 0.4;
      const alpha = (0.10 + 0.55 * q + pulse) * asleep;
      drawLink(x0, y0, x1, y1, "beacon", {
        color: "rgba(" + mix(90, 95) + "," + mix(107, 201) + ","
               + mix(128, 176) + "," + alpha + ")",
        glow: fxOn() ? "rgba(95, 201, 176," + (0.12 * q * asleep) + ")" : null,
        width: 0.6 + 2.6 * q,
        grow: grow.t,
      });
    }
  }

  /* The broker's own lines. In the mesh view every panel hangs on the
   * hub; in the topology view the LAN reaches out to the access
   * points, and a panel that reports no access point hangs on the hub
   * directly -- links to nothing are a picture of the wrong thing. */
  if (showBroker) {
    const uplinkOf = (box) =>
      (topo && box.kind === "ap") || (topo && box.kind === "device"
                                      && !(box.ap && apBoxFor(box.ap)))
        || (!topo && box.kind === "device");

    for (const box of boxes) {
      if (!uplinkOf(box)) continue;

      const grow = linkGrow(brokerBox, box, time);
      const lan = topo && box.kind === "ap";
      const alpha = lan ? 0.10 + box.activity * 0.25
                         : 0.05 + box.activity * 0.22;
      drawLink(brokerBox.cx, brokerBox.cy, box.cx, box.cy,
               lan ? "ap" : "device", {
        color: "rgba(153, 194, 255," + alpha + ")",
        glow: fxOn() ? "rgba(153, 194, 255,0.05)" : null,
        width: lan ? 1.4 : 1,
        /* The LAN is a cable with a current in it: the dashes walk
         * toward the broker, the way the messages do. */
        dash: lan && fxOn() ? [3, 9] : [],
        dashOffset: lan ? time * 0.02 : 0,
        grow: grow.t,
      });
    }
  }

  /* The WLAN links, access point to panel: the strength of the line is
   * the strength of the connection, and the energy in it flows toward
   * the access point, the way the messages do. */
  if (topo) {
    for (const box of boxes) {
      if (box.kind !== "ap") continue;

      for (const panel of box.panels) {
        const deviceBox = boxFor(panel.host);
        if (!deviceBox) continue;

        const grow = linkGrow(box, deviceBox, time);
        let x0 = box.cx, y0 = box.cy;
        let x1 = deviceBox.cx, y1 = deviceBox.cy;
        if (grow.reverse) {
          x0 = x1; y0 = y1;
          x1 = box.cx; y1 = box.cy;
        }

        const alpha = 0.10 + 0.45 * panel.q + deviceBox.activity * 0.20;
        drawLink(x0, y0, x1, y1, "device", {
          color: "rgba(153, 194, 255," + alpha + ")",
          glow: fxOn() ? "rgba(153, 194, 255,"
                        + (0.05 + 0.10 * panel.q) + ")" : null,
          width: 0.7 + 1.8 * panel.q,
          dash: fxOn() && panel.q > 0.25 ? [2, 8] : [],
          dashOffset: fxOn() && panel.q > 0.25 ? time * 0.02 : 0,
          grow: grow.t,
        });
      }
    }
  }

  /* -- light ---------------------------------------------------------- */

  drawTrails();
  drawHalos(time);
  if (showBroker) drawSweep(time);

  /* -- particles ------------------------------------------------------ */

  const dt = Math.min(0.05, (time - frameTime) / 1000) * 60;
  frameTime = time;

  for (let i = particles.length - 1; i >= 0; i--) {
    const p = particles[i];
    p.t += dt / 60 / p.dur;

    if (p.t >= 1 || (p.from && !boxes.includes(p.from))
        || (p.to && !boxes.includes(p.to))) {
      /* A broker-bound particle has no box to arrive at, so it wakes
       * the box it set out from instead. An access point carries the
       * news on: a second particle walks its LAN line home. */
      const arrived = p.to || p.from;
      arrived.activity = 1;
      if (fxOn()) {
        wave(arrived.cx, arrived.cy,
             p.to && p.to.kind === "device" ? PALETTE.teal : PALETTE.peri,
             24, 450);
      }
      if (p.to && p.to.kind === "ap" && showBroker) {
        spawnParticle(null, p.to);
      }
      particles.splice(i, 1);
      continue;
    }

    /* A broker particle is part of the broker picture. */
    if (!p.to && !showBroker) continue;

    const fx = p.from.cx;
    const fy = p.from.cy;
    const tx = p.to ? p.to.cx : brokerBox.cx;
    const ty = p.to ? p.to.cy : brokerBox.cy;
    /* The particle rides the line that hangs there, with a little slack
     * of its own, so a busy link is a bundle of threads and not one. */
    const hang = hangControl(fx, fy, tx, ty,
                             p.to && p.to.kind === "device" ? "beacon"
                             : "device");
    const cx1 = hang.x, cy1 = hang.y + p.off * 0.15;
    const tint = p.to && p.to.kind === "device" ? "95, 201, 176"
                 : "153, 194, 255";

    ctx.save();
    if (fxOn()) ctx.globalCompositeOperation = "lighter";
    for (let trail = 0; trail < 5; trail++) {
      const t = Math.max(0, p.t - trail * 0.04);
      const u = 1 - t;
      const px = u * u * fx + 2 * u * t * cx1 + t * t * tx;
      const py = u * u * fy + 2 * u * t * cy1 + t * t * ty;
      ctx.beginPath();
      ctx.arc(px, py, 2.6 - trail * 0.5, 0, Math.PI * 2);
      ctx.fillStyle = "rgba(" + tint + "," + (0.9 - trail * 0.18) + ")";
      ctx.fill();
    }
    ctx.restore();
  }

  /* -- boxes ---------------------------------------------------------- */

  for (const box of boxes) {
    if (box.kind === "broker") {
      if (showBroker)
        boxTransform(box, time, () => drawBroker(box, time));
    } else if (box.kind === "ap") {
      boxTransform(box, time, () => drawAp(box, time));
    } else if (box.kind === "beacon") {
      boxTransform(box, time, () => drawBeacon(box, time));
    } else {
      boxTransform(box, time, () => drawBox(box, time));
    }
  }

  drawDying(time);
  drawWaves(time);

  /* -- physics and repeat ---------------------------------------------- */

  applyDrags();
  physicsStep(Math.min(2.5, dt), time / 1000);
  requestAnimationFrame(draw);
}

/* The hub: a chip like the others, in the orange of the broker links,
 * with a core that pulses with the traffic and a ring that turns --
 * the one object that never drifts, drawn as the still point it is. */
function drawBroker(box, time) {
  const b = state.broker;
  const label = b.connected ? b.host + ":" + b.port : t("broker.noBroker");
  const sub = b.connected
    ? "in " + b.messages_in + "  ·  out " + b.messages_out
    : t("broker.connectionOff");

  const x = Math.round(box.cx - box.w / 2);
  const y = Math.round(box.cy - box.h / 2);

  if (fxOn()) {
    const alive = b.connected ? 1 : 0.3;
    stampGlow(box.cx, box.cy, 150 + 18 * Math.sin(time * 0.002),
              (0.12 + 0.20 * box.activity) * alive, PALETTE.orange);

    ctx.save();
    ctx.strokeStyle = "rgba(255, 156, 0, "
                      + ((0.08 + 0.18 * box.activity) * alive) + ")";
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 7]);
    ctx.lineDashOffset = -time * 0.02;
    ctx.beginPath();
    ctx.arc(box.cx, box.cy, 36, 0, Math.PI * 2);
    ctx.stroke();
    ctx.restore();
  }

  ctx.save();
  ctx.shadowColor = "rgba(255, 156, 0, " + (b.connected ? 0.5 : 0.15) + ")";
  ctx.shadowBlur = 18;
  chip(x, y, box.w, box.h, b.connected ? "#1a2030" : "#191521",
       b.connected ? PALETTE.orange : PALETTE.bad);
  ctx.restore();

  ctx.fillStyle = b.connected ? PALETTE.orange : PALETTE.bad;
  rr(x - 8, y + 6, 8, box.h - 12, 3);
  ctx.fill();

  const pulse = b.connected ? 1 + 0.25 * Math.sin(time * 0.004) : 1;
  ctx.beginPath();
  ctx.arc(x + 22, y + box.h / 2, 5 * pulse, 0, Math.PI * 2);
  ctx.fillStyle = b.connected ? PALETTE.good : PALETTE.bad;
  ctx.shadowColor = b.connected ? PALETTE.good : PALETTE.bad;
  ctx.shadowBlur = 8;
  ctx.fill();
  ctx.shadowBlur = 0;

  text(trunc(label, 22), x + 36, y + box.h / 2 - 8, SANS(13, 600),
       PALETTE.text);
  text(sub, x + 36, y + box.h / 2 + 10, MONO(10), PALETTE.dim);

  hits.push({ type: "broker-box", box, x, y, w: box.w, h: box.h });
  drawPinGlyph(box, x, y);

  if (!state.devices.length) {
    text(tf("broker.waiting",
            { topic: state.settings.base_topic || "oheztouch" }),
         box.cx, box.cy + box.h / 2 + 34, SANS(13), PALETTE.dim, "center");
  }
}

/* An access point box: a compact card in the blue of the LAN links,
 * one per BSSID the panels report. The header says the network's name
 * and how many panels are on it, the sub says the BSSID, the bar says
 * the best of its panels' signals, and the footer says who is on it. */
function drawAp(box, time) {
  ctx.save();
  const x = Math.round(box.cx - box.w / 2);
  const y = Math.round(box.cy - box.h / 2);
  let cursorY = y + AP_PAD;

  const glow = Math.max(0, ...Object.values(box.flash), box.activity * 0.6);
  if (glow > 0.02) {
    ctx.save();
    ctx.shadowColor = "rgba(153, 194, 255, " + glow * 0.45 + ")";
    ctx.shadowBlur = 22;
    chip(x, y, box.w, box.h, PALETTE.panel, PALETTE.edge);
    ctx.restore();
  } else {
    chip(x, y, box.w, box.h, PALETTE.panel, PALETTE.edge);
  }

  /* -- header -------------------------------------------------------- */

  const grad = ctx.createLinearGradient(x, y, x + box.w, y);
  grad.addColorStop(0, "#99c2ff");
  grad.addColorStop(1, "#5f83c9");
  rr(x, y, box.w, AP_ROWS.header, 8);
  ctx.fillStyle = grad;
  ctx.fill();
  ctx.fillRect(x + 5, y + AP_ROWS.header - 9, box.w - 10, 9);

  /* The antenna glyph: arcs whose number follows the best of its
   * panels' connections, breathing so the card never sits still. */
  const gy = y + AP_ROWS.header / 2;
  const best = box.panels.reduce((m, p) => Math.max(m, p.q), 0);
  const arcCount = best > 0.66 ? 3 : best > 0.33 ? 2 : best > 0 ? 1 : 0;

  ctx.strokeStyle = "#101a2e";
  ctx.fillStyle = "#101a2e";
  ctx.lineWidth = 1.4;
  ctx.beginPath();
  ctx.moveTo(x + 14, gy + 7);
  ctx.lineTo(x + 14, gy - 1);
  ctx.stroke();
  ctx.beginPath();
  ctx.arc(x + 14, gy + 7, 1.8, 0, Math.PI * 2);
  ctx.fill();
  for (let i = 0; i < arcCount; i++) {
    const breath = 0.55 + 0.45 * Math.sin(time * 0.004 + i * 1.3 + box.seed);
    ctx.globalAlpha = 0.4 + 0.6 * breath;
    ctx.beginPath();
    ctx.arc(x + 14, gy + 7, 4 + i * 4, -2.25, -0.9);
    ctx.stroke();
  }
  ctx.globalAlpha = 1;

  text(trunc(box.ssid || "AP", 18), x + 26, gy + 1, SANS(12, 700),
       "#101a2e");

  const count = String(box.panels.length);
  ctx.font = MONO(10);
  const cw = ctx.measureText(count).width + 12;
  rr(x + box.w - cw - 6, y + 6, cw, AP_ROWS.header - 12, 7);
  ctx.fillStyle = "#101a2e";
  ctx.fill();
  text(count, x + box.w - 12, gy + 1, MONO(10), "#99c2ff", "right");

  cursorY += AP_ROWS.header;
  hits.push({ type: "ap-box", box, x, y, w: box.w, h: box.h });
  drawPinGlyph(box, x, y);

  /* -- sub: the BSSID ------------------------------------------------- */

  text(bssidText(box.bssid), x + AP_PAD, cursorY + 7, MONO(9), PALETTE.dim);
  cursorY += AP_ROWS.sub;

  /* -- the best of its panels' signals --------------------------------- */

  const bestPanel = box.panels.reduce((m, p) => (p.q > m.q ? p : m),
                                      { q: 0, rssi: null });
  chip(x + AP_PAD, cursorY + 3, 52, 6, "#171c27", PALETTE.edge);
  ctx.fillStyle = best >= 0.6 ? PALETTE.good
                  : best >= 0.3 ? PALETTE.orange : PALETTE.bad;
  rr(x + AP_PAD, cursorY + 3, 52 * best, 6, 3);
  ctx.fill();
  text(bestPanel.rssi != null && isFinite(bestPanel.rssi)
       ? Math.round(bestPanel.rssi) + " dBm" : "-",
       x + AP_PAD + 58, cursorY + 7, MONO(10), PALETTE.text);
  cursorY += AP_ROWS.rssi;

  /* -- footer: who is on it --------------------------------------------- */

  const names = box.panels.map((p) => p.host);
  const heard = names.length
    ? (names.length <= 2
        ? trunc(names.join(", "), 22)
        : tf("beacon.heardByPanels", { n: names.length }))
    : t("beacon.nobody");
  text(heard, x + AP_PAD, cursorY + 6, MONO(9), PALETTE.dim);

  ctx.restore();
}

/* A beacon box: a smaller card in the teal of the radio links, one per
 * address, however many panels hear it. The header says who it is, the
 * bar says how well it is heard, the sparkline says how that has been
 * going, and the footer says who is listening. */
function drawBeacon(box, time) {
  ctx.save();
  const beacon = box.dev;
  const x = Math.round(box.cx - box.w / 2);
  const y = Math.round(box.cy - box.h / 2);
  const active = !!beacon.active;
  let cursorY = y + BEACON_PAD;

  const glow = Math.max(0, ...Object.values(box.flash), 0);
  if (glow > 0.02) {
    ctx.save();
    ctx.shadowColor = "rgba(95, 201, 176, " + glow * 0.45 + ")";
    ctx.shadowBlur = 22;
    chip(x, y, box.w, box.h, PALETTE.panel, PALETTE.edge);
    ctx.restore();
  } else {
    chip(x, y, box.w, box.h, PALETTE.panel, PALETTE.edge);
  }

  /* -- header -------------------------------------------------------- */

  const grad = ctx.createLinearGradient(x, y, x + box.w, y);
  if (active) {
    grad.addColorStop(0, "#5fc9b0");
    grad.addColorStop(1, "#3d9c88");
  } else {
    grad.addColorStop(0, "#5a6774");
    grad.addColorStop(1, "#47525f");
  }
  rr(x, y, box.w, BEACON_ROWS.header, 8);
  ctx.fillStyle = grad;
  ctx.fill();
  ctx.fillRect(x + 5, y + BEACON_ROWS.header - 9, box.w - 10, 9);

  /* The beacon glyph: a dot emitting -- asleep, the dot alone. */
  const gy = y + BEACON_ROWS.header / 2;
  ctx.fillStyle = "#0d2520";
  ctx.beginPath();
  ctx.arc(x + 14, gy, 2.6, 0, Math.PI * 2);
  ctx.fill();

  if (active) {
    ctx.strokeStyle = "#0d2520";
    ctx.lineWidth = 1.4;
    for (const radius of [5, 8.5]) {
      ctx.beginPath();
      ctx.arc(x + 14, gy, radius, -0.85, 0.85);
      ctx.stroke();
    }
  }

  const label = beacon.name
    || (beacon.addr.slice(0, 4) + "…" + beacon.addr.slice(-4));
  text(trunc(label, 18), x + 26, gy + 1, SANS(12, 700), "#0d2520");

  if (beacon.rssi != null) {
    const value = String(beacon.rssi);
    ctx.font = MONO(10);
    const cw = ctx.measureText(value).width + 12;
    rr(x + box.w - cw - 6, y + 6, cw, BEACON_ROWS.header - 12, 7);
    ctx.fillStyle = "#0d2520";
    ctx.fill();
    text(value, x + box.w - 12, gy + 1, MONO(10), "#5fc9b0", "right");
  }

  cursorY += BEACON_ROWS.header;
  hits.push({ type: "beacon-box", box, x, y, w: box.w, h: box.h });

  const hasRow = (row) => box.rows.includes(row);
  const dim = active ? PALETTE.dim : "#5a6b80";

  /* -- sub: kind and id ----------------------------------------------- */

  if (hasRow("sub")) {
    const line = [beacon.type, beacon.id].filter(Boolean).join("  ·  ");
    if (line) text(trunc(line, 36), x + BEACON_PAD, cursorY + 7,
                   MONO(9), dim);
    cursorY += BEACON_ROWS.sub;
  }

  /* -- the hearing: bar, number, distance -------------------------------- */

  if (hasRow("rssi")) {
    const rssi = beacon.rssi;
    const q = rssi == null ? 0
              : Math.max(0, Math.min(1, (rssi + 100) / 70));
    chip(x + BEACON_PAD, cursorY + 3, 52, 6, "#171c27", PALETTE.edge);
    ctx.fillStyle = q >= 0.6 ? PALETTE.teal
                    : q >= 0.3 ? PALETTE.orange : PALETTE.bad;
    rr(x + BEACON_PAD, cursorY + 3, 52 * q, 6, 3);
    ctx.fill();
    text(rssi == null ? "-" : rssi + " dBm", x + BEACON_PAD + 58,
         cursorY + 7, MONO(10), active ? PALETTE.text : dim);

    const distance = Number(beacon.fields.distance
                            && beacon.fields.distance.value);
    if (isFinite(distance)) {
      text((distance < 10 ? distance.toFixed(1) : Math.round(distance)) + " m",
           x + box.w - BEACON_PAD, cursorY + 7, MONO(10), PALETTE.teal,
           "right");
    }
    cursorY += BEACON_ROWS.rssi;
  }

  /* -- sparkline: the strongest hearing over the last minutes ------------ */

  if (hasRow("spark")) {
    const sw = box.w - 2 * BEACON_PAD;
    const sh = BEACON_ROWS.spark - 5;
    const slice = beacon.history.slice(-40);
    let lo = Infinity, hi = -Infinity;
    for (const [, v] of slice) {
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    if (hi - lo < 4) hi = lo + 4;

    ctx.beginPath();
    slice.forEach(([, v], idx) => {
      const px = x + BEACON_PAD + (sw * idx) / (slice.length - 1);
      const py = cursorY + 2 + sh - ((v - lo) / (hi - lo)) * sh;
      if (idx) ctx.lineTo(px, py); else ctx.moveTo(px, py);
    });
    ctx.strokeStyle = active ? PALETTE.teal : "#3a4b5e";
    ctx.lineWidth = 1.3;
    ctx.stroke();
    cursorY += BEACON_ROWS.spark;
  }

  /* -- telemetry chips ---------------------------------------------------- */

  if (hasRow("chips")) {
    let cx0 = x + BEACON_PAD;
    const chips = [];

    if (beacon.fields.battery)
      chips.push([beacon.fields.battery, "battery"]);
    if (beacon.fields.temperature)
      chips.push([beacon.fields.temperature, "temperature"]);
    if (beacon.fields.power)
      chips.push([beacon.fields.power, "power"]);

    for (const [record, field] of chips) {
      let label = record.value;
      if (field === "temperature") label = Number(label).toFixed(1) + "°C";
      ctx.font = MONO(9);
      const cw = ctx.measureText(label).width + 12;
      chip(cx0, cursorY, cw, BEACON_ROWS.chips - 6,
           box.flash[field] > 0.3 ? "rgba(95, 201, 176, 0.3)" : "#171c27",
           PALETTE.edge);
      text(label, cx0 + 6, cursorY + (BEACON_ROWS.chips - 6) / 2 + 1,
           MONO(9), active ? PALETTE.teal : dim);
      cx0 += cw + 4;
    }
    cursorY += BEACON_ROWS.chips;
  }

  /* -- footer: who hears it, and when it was last heard ------------------- */

  const names = beacon.devices.map((d) => d.host);
  const heard = names.length
    ? t("beacon.heardBy") + (names.length <= 2
        ? trunc(names.join(", "), 20)
        : tf("beacon.heardByPanels", { n: names.length }))
    : t("beacon.nobody");
  text(heard, x + BEACON_PAD, cursorY + 6, MONO(9), dim);

  if (beacon.age != null) {
    text(tf("age.agoSeconds", { n: beacon.age }),
         x + box.w - BEACON_PAD, cursorY + 6, MONO(9), "#5a6b80", "right");
  }

  ctx.restore();
}

function drawBox(box, time) {
  ctx.save();
  const dev = box.dev;
  const x = Math.round(box.cx - box.w / 2);
  const y = Math.round(box.cy - box.h / 2);
  const online = !!dev.online;
  let cursorY = y + PAD;

  /* Soft glow while the box is talking. */
  const glow = Math.max(0, ...Object.values(box.flash), 0);
  if (glow > 0.02) {
    ctx.save();
    ctx.shadowColor = "rgba(255, 156, 0, " + glow * 0.45 + ")";
    ctx.shadowBlur = 22;
    chip(x, y, box.w, box.h, PALETTE.panel, PALETTE.edge);
    ctx.restore();
  } else {
    chip(x, y, box.w, box.h, PALETTE.panel, PALETTE.edge);
  }

  /* -- header -------------------------------------------------------- */

  const grad = ctx.createLinearGradient(x, y, x + box.w, y);
  if (online) {
    grad.addColorStop(0, "#ff9c00");
    grad.addColorStop(1, "#d47700");
  } else {
    grad.addColorStop(0, "#5a6774");
    grad.addColorStop(1, "#47525f");
  }
  rr(x, y, box.w, ROWS.header, 9);
  ctx.fillStyle = grad;
  ctx.fill();
  ctx.fillRect(x + 6, y + ROWS.header - 10, box.w - 12, 10);

  ctx.beginPath();
  ctx.arc(x + 16, y + ROWS.header / 2, 4.5, 0, Math.PI * 2);
  ctx.fillStyle = online ? "#12330f" : "#3d1210";
  ctx.fill();
  ctx.strokeStyle = online ? PALETTE.good : PALETTE.bad;
  ctx.lineWidth = 1.5;
  ctx.stroke();

  text(trunc(box.host, 20), x + 28, y + ROWS.header / 2 + 1,
       SANS(13, 700), "#1a1200");

  const version = topicValue(dev, "system/version");
  if (version) {
    const vw = ctx.measureText("v" + version).width;
    rr(x + box.w - vw - 22, y + 7, vw + 14, ROWS.header - 14, 8);
    ctx.fillStyle = "#1a1200";
    ctx.fill();
    text("v" + version, x + box.w - 15, y + ROWS.header / 2 + 1, MONO(10),
         "#ff9c00", "right");
  }

  cursorY += ROWS.header;
  hits.push({ type: "box", box, x, y, w: box.w, h: box.h });
  drawPinGlyph(box, x, y);

  const hasRow = (row) => box.rows.includes(row);
  const t = (suffix) => dev.topics[suffix];
  const flashFill = (suffix) => {
    const f = box.flash[suffix] || 0;
    return f > 0.02 ? "rgba(255, 170, 40, " + (f * 0.18).toFixed(3) + ")"
                    : null;
  };

  /* -- sub: board and version ---------------------------------------- */

  if (hasRow("sub")) {
    const target = topicValue(dev, "system/target") || "";
    const name = topicValue(dev, "system/name") || "";
    const line = [target, name && name !== box.host ? name : ""]
      .filter(Boolean).join("  ·  ");
    if (line) text(trunc(line, 46), x + PAD, cursorY + 7, MONO(10),
                   PALETTE.dim);
    cursorY += ROWS.sub;
  }

  /* -- system chips ---------------------------------------------------- */

  if (hasRow("sys")) {
    let cx0 = x + PAD;
    const uptime = topicValue(dev, "system/uptime");
    const heap = topicValue(dev, "system/heap");
    const fps = topicValue(dev, "system/fps");

    for (const [suffix, label] of [["system/uptime", fmtUptime(uptime)],
                                   ["system/heap", fmtBytes(heap)],
                                   ["system/fps", fps ? fps + "fps" : null]]) {
      if (!label) continue;
      ctx.font = MONO(10);
      const cw = ctx.measureText(label).width + 16;
      const fill = flashFill(suffix) || "#171c27";
      chip(cx0, cursorY, cw, ROWS.sys - 6, fill, PALETTE.edge);
      text(label, cx0 + 8, cursorY + (ROWS.sys - 6) / 2 + 1, MONO(10),
           PALETTE.text);
      cx0 += cw + 6;
    }
    cursorY += ROWS.sys;
  }

  /* -- network row ------------------------------------------------------ */

  if (hasRow("net")) {
    const ip = topicValue(dev, "system/ip");
    if (ip) text(trunc(ip, 26), x + PAD, cursorY + 7, MONO(11),
                 flashFill("system/ip") ? "#ffd9a0" : PALETTE.text);

    const rssi = Number(topicValue(dev, "system/rssi"));
    const quality = Number(topicValue(dev, "system/quality")
                           || (isFinite(rssi)
                               ? Math.max(0, Math.min(100, 2 * (rssi + 100)))
                               : NaN));
    if (isFinite(rssi) && rssi !== 0) {
      const bw = 46, bxx = x + box.w - PAD - bw;
      chip(bxx, cursorY + 3, bw, 6, "#171c27", PALETTE.edge);
      if (isFinite(quality) && quality > 0) {
        ctx.fillStyle = quality >= 60 ? PALETTE.good
                        : quality >= 30 ? PALETTE.orange : PALETTE.bad;
        rr(bxx, cursorY + 3, bw * quality / 100, 6, 3);
        ctx.fill();
      }
      text(String(rssi), bxx + bw + 8, cursorY + 6, MONO(10), PALETTE.dim);
    }
    cursorY += ROWS.net;
  }

  /* -- UI chips ----------------------------------------------------------- */

  if (hasRow("ui")) {
    let cx0 = x + PAD;
    const parts = [["ui/night", t("chip.night")],
                   ["ui/backlight", t("chip.backlight")],
                   ["ui/activity", t("chip.active")]];

    for (const [suffix, label] of parts) {
      const value = topicValue(dev, suffix);
      if (value == null) continue;
      const on = isOn(value);
      ctx.font = SANS(9, 600);
      const cw = ctx.measureText(label).width + 18;
      chip(cx0, cursorY, cw, ROWS.ui - 8,
           box.flash[suffix] > 0.3 ? "rgba(255,170,40,0.4)"
           : on ? "rgba(153,194,255,0.25)" : "#171c27",
           on ? PALETTE.peri : PALETTE.edge);
      text(label, cx0 + 9, cursorY + (ROWS.ui - 8) / 2 + 1, SANS(9, 600),
           on ? PALETTE.peri : PALETTE.dim);
      cx0 += cw + 5;
    }

    const brightness = topicValue(dev, "ui/brightness");
    if (brightness != null) {
      ctx.font = MONO(10);
      const label = t("chip.brightness") + brightness;
      const cw = ctx.measureText(label).width + 16;
      chip(cx0, cursorY, cw, ROWS.ui - 8, "#171c27", PALETTE.edge);
      text(label, cx0 + 8, cursorY + (ROWS.ui - 8) / 2 + 1, MONO(10),
           PALETTE.text);
    }
    cursorY += ROWS.ui;
  }

  /* -- sensors ------------------------------------------------------------ */

  if (hasRow("sens")) {
    const temp = topicValue(dev, "sensor/temperature");
    const hum = topicValue(dev, "sensor/humidity");
    const press = topicValue(dev, "sensor/pressure");
    let cx0 = x + PAD;
    const show = (suffix, label) => {
      ctx.font = MONO(12);
      const tw = ctx.measureText(label).width;
      text(label, cx0, cursorY + ROWS.sens / 2,
           MONO(12), flashFill(suffix) ? "#ffd9a0" : PALETTE.teal);
      cx0 += tw + 14;
    };

    if (temp) show("sensor/temperature", Number(temp).toFixed(1) + "°C");
    if (hum) show("sensor/humidity", Number(hum).toFixed(0) + "%");
    if (press) show("sensor/pressure", Math.round(Number(press)) + "hPa");

    /* Temperature sparkline, when there is history to draw one from. */
    const history = (dev.history || {})["sensor/temperature"];
    if (history && history.length > 1) {
      const sw = 60, sh = ROWS.sens - 6;
      const sx = x + box.w - PAD - sw, sy = cursorY + 3;
      const slice = history.slice(-40);
      let lo = Infinity, hi = -Infinity;
      for (const [, v] of slice) {
        if (v < lo) lo = v;
        if (v > hi) hi = v;
      }
      if (hi - lo < 0.5) hi = lo + 0.5;

      ctx.beginPath();
      slice.forEach(([ts, v], idx) => {
        const px = sx + (sw * idx) / (slice.length - 1);
        const py = sy + sh - ((v - lo) / (hi - lo)) * sh;
        if (idx) ctx.lineTo(px, py); else ctx.moveTo(px, py);
      });
      ctx.strokeStyle = PALETTE.teal;
      ctx.lineWidth = 1.4;
      ctx.stroke();
    }
    cursorY += ROWS.sens;
  }

  /* -- outputs: relays and LEDs ------------------------------------------ */

  if (hasRow("out")) {
    let cx0 = x + PAD;

    for (let relay = 1; relay <= 3; relay++) {
      const suffix = "relay/" + relay;
      if (!t(suffix)) continue;
      const on = isOn(topicValue(dev, suffix));
      chip(cx0, cursorY, 20, 20, on ? "rgba(255,156,0,0.25)" : "#171c27",
           on ? PALETTE.orange : PALETTE.edge);
      text(String(relay), cx0 + 10, cursorY + 11, SANS(11, 700),
           on ? PALETTE.orange : PALETTE.dim, "center");
      hits.push({ type: "relay", box, suffix,
                  x: cx0, y: cursorY, w: 20, h: 20 });
      cx0 += 26;
    }

    cx0 += 6;
    for (const channel of ["red", "green", "blue"]) {
      const suffix = "led/" + channel;
      if (!t(suffix)) continue;

      const local = box.ledLocal[suffix];
      const value = local != null ? local
                    : Math.max(0, Math.min(100, Number(topicValue(dev, suffix))
                                           || 0));
      const color = LED_COLORS[channel] || PALETTE.peri;

      chip(cx0, cursorY + 6, 52, 8, "#171c27", PALETTE.edge);
      if (value > 0) {
        ctx.fillStyle = color;
        ctx.globalAlpha = 0.25 + 0.75 * value / 100;
        rr(cx0, cursorY + 6, 52 * value / 100, 8, 4);
        ctx.fill();
        ctx.globalAlpha = 1;
      }
      ctx.beginPath();
      ctx.arc(cx0 + 52 * value / 100, cursorY + 10, 5, 0, Math.PI * 2);
      ctx.fillStyle = color;
      ctx.fill();

      hits.push({ type: "led", box, suffix, channel,
                  x: cx0, y: cursorY, w: 52, h: 20 });
      cx0 += 60;
    }
    cursorY += ROWS.out;
  }

  /* -- footer -------------------------------------------------------------- */

  const ble = topicValue(dev, "ble/count");
  text(tf("age.seen",
          { n: dev.last_age != null ? fmtAge(dev.last_age) : "-" }),
       x + PAD, cursorY + 7, MONO(10), PALETTE.dim);

  if (ble != null) {
    const label = "BLE " + ble;
    ctx.font = MONO(9);
    const bw = ctx.measureText(label).width + 14;
    chip(x + box.w - PAD - bw, cursorY, bw, 14, "rgba(153,194,255,0.12)",
         PALETTE.peri);
    text(label, x + box.w - PAD - bw / 2, cursorY + 8, MONO(9), PALETTE.peri,
         "center");
  }

  ctx.restore();
}

/* ------------------------------------------------------------ interactions */

let pointer = { down: null, boxDrag: null, ledDrag: null, moved: 0,
                downAt: 0 };

function hitAt(px, py) {
  const candidates = hits.filter((r) => px >= r.x && px < r.x + r.w
                                  && py >= r.y && py < r.y + r.h);
  if (!candidates.length) return null;
  /* Controls sit above the whole-box region; later hits are drawn later
   * and are on top. */
  const controls = candidates.filter((r) => r.type !== "box");
  return controls.length ? controls[controls.length - 1] : candidates[0];
}

canvas.addEventListener("pointerdown", (event) => {
  const { x: px, y: py } = eventWorld(event);
  const hit = hitAt(px, py);

  pointer.down = { x: px, y: py };
  pointer.moved = 0;
  pointer.downAt = performance.now();

  if (hit && hit.type === "led") {
    pointer.ledDrag = hit;
    canvas.setPointerCapture(event.pointerId);
    ledSetValue(hit, px);
    return;
  }

  if (hit && hit.type !== "led") {
    /* Everything but a slider is a grab: a quick tap on a relay fires it,
     * a quick tap anywhere else opens the detail, a drag moves the box. */
    pointer.boxDrag = { box: hit.box, cx: hit.box.cx, cy: hit.box.cy,
                        startHit: hit };
    canvas.setPointerCapture(event.pointerId);
    canvas.classList.add("grabbing");
    return;
  }

  /* Nothing under the pointer, and the picture can be moved: the hand
   * is on the workspace itself. */
  if (isTopology()) {
    pointer.camDrag = { x: cam.x, y: cam.y, px, py };
    canvas.setPointerCapture(event.pointerId);
    canvas.classList.add("grabbing");
  }
});

canvas.addEventListener("pointermove", (event) => {
  const { x: px, y: py } = eventWorld(event);

  if (pointer.ledDrag) {
    ledSetValue(pointer.ledDrag, px);
    return;
  }

  if (pointer.boxDrag) {
    const drag = pointer.boxDrag;
    pointer.moved = Math.max(pointer.moved,
                             Math.hypot(px - pointer.down.x,
                                        py - pointer.down.y));
    drag.cx = px;
    drag.cy = py;
    return;
  }

  if (pointer.camDrag) {
    const drag = pointer.camDrag;
    pointer.moved = Math.max(pointer.moved,
                             Math.hypot(px - drag.px, py - drag.py));
    cam.x = cam.tx = drag.x - (px - drag.px) / stage.w;
    cam.y = cam.ty = drag.y - (py - drag.py) / stage.h;
    saveCamSoon();
    return;
  }

  /* A hover over anything clickable should say so. */
  const hit = hitAt(px, py);
  hovered = hit && hit.box ? hit.box : null;
  canvas.style.cursor = hit ? "pointer" : "grab";
});

/* A stationary object dropped in the topology view keeps the place it
 * was given: the position is pinned, saved as a fraction of the canvas,
 * and confirmed by a ring. In the mesh view the same drop is a throw
 * like any other -- the links gather the box back in. */
function savePin(box) {
  const x = Math.max(0, Math.min(1, box.cx / Math.max(1, stage.w)));
  const y = Math.max(0, Math.min(1, box.cy / Math.max(1, stage.h)));

  box.pinned = true;
  box.pin = { x, y };

  state.settings.positions = { ...(state.settings.positions || {}) };
  state.settings.positions[pinKeyFor(box)] = box.pin;

  if (fxOn()) wave(box.cx, box.cy, PALETTE.orange, 46, 550);
  api("/api/position", { key: pinKeyFor(box), x, y })
    .catch((error) => toast(error.message, "err"));
}

/* Letting a pinned object go again: it floats on its links, and the
 * place it kept is forgotten. */
function unpinBox(box) {
  box.pinned = false;

  state.settings.positions = { ...(state.settings.positions || {}) };
  delete state.settings.positions[pinKeyFor(box)];

  if (fxOn()) wave(box.cx, box.cy, PALETTE.peri, 40, 500);
  api("/api/position/delete", { key: pinKeyFor(box) })
    .catch((error) => toast(error.message, "err"));
}

async function publish(box, suffix, payload) {
  try {
    await api("/api/publish",
              { device: box.host, suffix, payload: String(payload) });
  } catch (error) {
    toast(error.message, "err");
  }
}

function ledSetValue(region, px) {
  const { box, suffix } = region;
  const value = Math.max(0, Math.min(100,
    Math.round((px - region.x) / region.w * 100)));
  box.ledLocal[suffix] = value;

  /* While dragging, publish at a steady trickle; the panel's state topic
   * has the final word anyway. */
  const now = performance.now();
  if (now - box.ledPublishAt > 250) {
    box.ledPublishAt = now;
    publish(box, suffix + "/set", value);
  }
}

canvas.addEventListener("pointerup", (event) => {
  const quick = performance.now() - pointer.downAt < 350
                && pointer.moved < 6;

  if (pointer.ledDrag) {
    const { box, suffix } = pointer.ledDrag;
    const value = box.ledLocal[suffix];
    if (value != null) publish(box, suffix + "/set", value);
    box.ledLocal[suffix] = null;
    pointer.ledDrag = null;
  } else if (pointer.boxDrag) {
    const drag = pointer.boxDrag;
    if (quick) {
      const hit = drag.startHit;
      if (hit.type === "relay") {
        publish(hit.box, hit.suffix + "/set", "TOGGLE");
      } else if (hit.type === "pin") {
        unpinBox(hit.box);
      } else if (drag.box.kind === "beacon") {
        openBeaconDetail(drag.box.host);
      } else if (drag.box.kind === "ap") {
        openApDetail(drag.box.bssid);
      } else if (drag.box.kind === "broker") {
        /* the hub has nothing more to say for itself */
      } else {
        openDetail(drag.box.host);
      }
    } else if (isTopology() && drag.box.kind !== "beacon") {
      /* A stationary object dropped in the topology view keeps its
       * place; the broker does too, in the topology view alone. */
      savePin(drag.box);
    }
    /* A thrown box keeps its throw, and the links gather it back into
     * the mesh -- the network heals the same way it was made. */
  }

  pointer.boxDrag = null;
  pointer.camDrag = null;
  pointer.down = null;
  canvas.classList.remove("grabbing");
});

canvas.addEventListener("pointercancel", () => {
  if (pointer.boxDrag) pointer.boxDrag.box.drag = null;
  pointer.boxDrag = null;
  pointer.camDrag = null;
  pointer.ledDrag = null;
  canvas.classList.remove("grabbing");
});

/* The dragged box follows the pointer through its drag record; physics
 * skips boxes that are being held. */
function applyDrags() {
  if (pointer.boxDrag) {
    const drag = pointer.boxDrag;
    drag.box.drag = drag;
  } else {
    for (const box of boxes) box.drag = null;
  }
}

/* ------------------------------------------------------------- view switches */

/* The two switches in the top bar say what the canvas draws -- the broker
 * node with its links, and the beacon boxes with theirs. They are view
 * switches, not stop buttons: the devices keep tracking, the beacons keep
 * learning, and everything comes back exactly as it was. */
function renderViewToggles() {
  $("btn-view-broker").classList.toggle("off",
                                        state.settings.show_broker === false);
  $("btn-view-beacons").classList.toggle("off",
                                         state.settings.show_beacons === false);
  $("btn-view-fx").classList.toggle("off",
                                    state.settings.show_fx === false);
  $("btn-mode-mesh").classList.toggle("active", !isTopology());
  $("btn-mode-topology").classList.toggle("active", isTopology());

  /* The zoom belongs to the topology view alone -- the mesh is its own
   * fixed picture. */
  $("zoom-controls").classList.toggle("hidden", !isTopology());
}

async function toggleView(key, button) {
  const value = state.settings[key] === false;
  state.settings[key] = value;               // the picture moves at once;
  renderViewToggles();                       // the server keeps the choice

  try {
    await api("/api/settings", { [key]: value });
  } catch (error) {
    state.settings[key] = !value;
    renderViewToggles();
    toast(error.message, "err");
  }
}

/* Which picture the canvas draws of the same facts: the classic mesh,
 * or the topology of broker, access points and panels. The boxes stay
 * where they are and the springs do the moving -- a switch is the mesh
 * re-forming itself, not the page starting over. */
async function switchViewMode(mode) {
  const before = state.settings.view_mode;
  if (before === mode) return;
  state.settings.view_mode = mode;

  renderViewToggles();
  renderPhysicsTabs();
  syncBoxes();

  try {
    await api("/api/settings", { view_mode: mode });
  } catch (error) {
    state.settings.view_mode = before;
    renderViewToggles();
    renderPhysicsTabs();
    syncBoxes();
    toast(error.message, "err");
  }
}

$("btn-view-broker").addEventListener("click", () =>
  toggleView("show_broker"));
$("btn-view-beacons").addEventListener("click", () =>
  toggleView("show_beacons"));
$("btn-view-fx").addEventListener("click", () =>
  toggleView("show_fx"));
$("btn-mode-mesh").addEventListener("click", () =>
  switchViewMode("mesh"));
$("btn-mode-topology").addEventListener("click", () =>
  switchViewMode("topology"));

/* ---------------------------------------------------------------- physics */

/* The panel works like the rest of the tool's dialogs: a draft the
 * page owns, made true all at once by save. The difference is that a
 * slider acts on the canvas the moment it is touched -- the feel is the
 * thing being edited, and it has to be felt -- so the draft is what
 * the motion reads while the panel is open. Closing without saving
 * puts the saved feel back, exactly like every other dialog.
 *
 * The panels, the beacons and the broker are set apart: three tabs,
 * three drafts, one save. The canvas reads each kind's own sliders, so
 * the beacons can be tuned while the panels sit still, and the hub can
 * gather the whole mesh without touching anything else. */
let physicsDraft = null;          // { device: {...}, beacon: {...}, broker: {...} }
let physicsSaved = null;          // the same shape, as the server has it
let physicsKind = "device";       // which half the panel is showing

function buildPhysicsPanel() {
  for (const kind of PHYS_KINDS) {
    const rows = $(physRowsId(kind));
    rows.textContent = "";

    for (const spec of physSpecFor(kind)) {
      const row = document.createElement("div");
      row.className = "phys-row";
      /* The help text is the hover of the whole row: what the slider
       * does to the canvas, in the language of the page. */
      row.title = t(spec.help);
      row.innerHTML =
        '<span class="p-label">' + esc(t(spec.label)) + "</span>"
        + '<input type="range" min="' + (spec.min || 0)
        + '" max="' + (spec.max || 100) + '" step="1" data-key="'
        + spec.key + '">'
        + '<span class="p-value" data-readout="' + spec.key + '"></span>';
      rows.appendChild(row);
    }
  }
}

/* The rows are rebuilt whenever the language changes; the containers
 * are permanent, so their listeners are attached exactly once. */
for (const kind of PHYS_KINDS) {
  $(physRowsId(kind)).addEventListener("input", (event) => {
    const key = event.target.dataset.key;
    if (!key || !physicsDraft) return;
    physicsDraft[kind][key] = Number(event.target.value);
    state.settings[PHYS_SETTINGS_KEY[kind]] = physicsDraft[kind];
    syncPhysicsReadouts();                  // the canvas feels it now
  });
}

function syncPhysicsReadouts() {
  for (const kind of PHYS_KINDS) {
    const draft = physicsDraft
      ? physicsDraft[kind]
      : state.settings[PHYS_SETTINGS_KEY[kind]] || PHYS_DEFAULTS;
    const container = $(physRowsId(kind));

    for (const spec of physSpecFor(kind)) {
      const slider = container.querySelector(
        '[data-key="' + spec.key + '"]');
      const readout = container.querySelector(
        '[data-readout="' + spec.key + '"]');
      if (slider) slider.value = draft[spec.key];
      if (readout) readout.textContent = spec.fmt(draft[spec.key]);
    }
  }
}

function renderPhysicsTabs() {
  /* The access points are a kind of the topology view; the mesh has
   * none, so its slider tab is not offered there. */
  $("phys-tab-aps").classList.toggle("hidden", !isTopology());
  if (!isTopology() && physicsKind === "ap") physicsKind = "device";

  for (const [kind, id] of [["device", "phys-tab-nodes"],
                            ["ap", "phys-tab-aps"],
                            ["beacon", "phys-tab-beacons"],
                            ["broker", "phys-tab-broker"]]) {
    $(id).classList.toggle("active", physicsKind === kind);
    $(physRowsId(kind)).classList.toggle("hidden", physicsKind !== kind);
  }
}

$("phys-tab-nodes").addEventListener("click", () => {
  physicsKind = "device";
  renderPhysicsTabs();
});
$("phys-tab-aps").addEventListener("click", () => {
  physicsKind = "ap";
  renderPhysicsTabs();
});
$("phys-tab-beacons").addEventListener("click", () => {
  physicsKind = "beacon";
  renderPhysicsTabs();
});
$("phys-tab-broker").addEventListener("click", () => {
  physicsKind = "broker";
  renderPhysicsTabs();
});

function openPhysics() {
  physicsSaved = {};
  for (const kind of PHYS_KINDS) {
    physicsSaved[kind] = JSON.parse(JSON.stringify(
      state.settings[PHYS_SETTINGS_KEY[kind]] || PHYS_DEFAULTS));
  }
  physicsDraft = JSON.parse(JSON.stringify(physicsSaved));
  for (const kind of PHYS_KINDS) {
    state.settings[PHYS_SETTINGS_KEY[kind]] = physicsDraft[kind];
  }
  syncPhysicsReadouts();
  renderPhysicsTabs();
  $("physics").classList.remove("hidden");
  $("btn-physics").classList.add("panel-open");
}

function closePhysics() {
  /* Unsaved sliders are an experiment that ends here. */
  for (const kind of PHYS_KINDS) {
    state.settings[PHYS_SETTINGS_KEY[kind]] = physicsSaved
      ? physicsSaved[kind] : PHYS_DEFAULTS;
  }
  physicsDraft = null;
  $("physics").classList.add("hidden");
  $("btn-physics").classList.remove("panel-open");
}

/* The button opens the side panel and closes it again; either way the
 * canvas keeps moving behind it. */
$("btn-physics").addEventListener("click", () => {
  if ($("physics").classList.contains("hidden")) openPhysics();
  else closePhysics();
});
$("physics-close").addEventListener("click", closePhysics);

$("physics-reset").addEventListener("click", () => {
  physicsDraft = {};
  for (const kind of PHYS_KINDS) {
    physicsDraft[kind] = JSON.parse(JSON.stringify(PHYS_DEFAULTS));
    state.settings[PHYS_SETTINGS_KEY[kind]] = physicsDraft[kind];
  }
  syncPhysicsReadouts();
});

$("physics-save").addEventListener("click", async () => {
  try {
    const body = {};
    for (const kind of PHYS_KINDS) {
      body[PHYS_SETTINGS_KEY[kind]] = physicsDraft[kind];
    }
    const doc = await api("/api/settings", body);
    physicsSaved = {};
    for (const kind of PHYS_KINDS) {
      physicsSaved[kind] = JSON.parse(JSON.stringify(
        doc.settings[PHYS_SETTINGS_KEY[kind]]));
    }
    toast(t("toast.physSaved"));
  } catch (error) {
    toast(error.message, "err");
  }
});

/* ------------------------------------------------------------ detail panel */

let detailTarget = null;        // {kind} | {kind: "device", host}
                               // | {kind: "beacon", addr}
                               // | {kind: "ap", bssid}

/* The pin button of the detail panel: it says what the box is -- pinned
 * or floating -- and does the other thing. */
function renderDetailPin(box) {
  const button = $("detail-pin");
  if (isTopology() && box && box.kind !== "beacon") {
    button.classList.remove("hidden");
    button.textContent = box.pinned ? t("detail.unpin") : t("detail.pin");
  } else {
    button.classList.add("hidden");
  }
}

function detailBox() {
  if (!detailTarget) return null;
  if (detailTarget.kind === "device") return boxFor(detailTarget.host);
  if (detailTarget.kind === "ap")
    return boxes.find((b) => b.kind === "ap" && b.bssid === detailTarget.bssid);
  return null;
}

$("detail-pin").addEventListener("click", () => {
  const box = detailBox();
  if (!box) return;
  if (box.pinned) unpinBox(box);
  else savePin(box);
  renderDetailPin(box);
});

function openDetail(host) {
  detailTarget = { kind: "device", host };
  const select = $("detail-sound");
  if (!select.options.length) {
    for (const name of SOUND_NAMES) {
      const option = document.createElement("option");
      option.value = name;
      option.textContent = name;
      select.appendChild(option);
    }
  }
  $("detail").classList.remove("hidden");
  renderDetail();
}

function openBeaconDetail(addr) {
  detailTarget = { kind: "beacon", addr };
  $("detail").classList.remove("hidden");
  renderDetail();
}

function openApDetail(bssid) {
  detailTarget = { kind: "ap", bssid };
  $("detail").classList.remove("hidden");
  renderDetail();
}

function closeDetail() {
  detailTarget = null;
  $("detail").classList.add("hidden");
}

function renderDetail() {
  if (!detailTarget) return closeDetail();
  if (detailTarget.kind === "beacon") return renderBeaconDetail();
  if (detailTarget.kind === "ap") return renderApDetail();

  const dev = state.byHost[detailTarget.host];
  if (!dev) return closeDetail();

  $("detail-seenby").classList.add("hidden");
  $("detail-delete").classList.remove("hidden");
  $("detail-title").textContent = dev.host;
  renderDetailPin(boxFor(dev.host));

  const target = topicValue(dev, "system/target") || "?";
  const version = topicValue(dev, "system/version") || "?";
  $("detail-status").innerHTML =
    (dev.online
      ? '<b style="color:var(--good)">' + t("word.online") + "</b>"
      : '<b style="color:var(--bad)">' + t("word.offline") + "</b>")
    + tf("detail.devStatus", {
      target: esc(target),
      version: esc(version),
      first: esc(dev.first_seen || "-"),
      age: dev.last_age != null ? fmtAge(dev.last_age) : "-",
    });

  /* relays */
  const relays = Object.keys(dev.topics).filter((s) => /^relay\/\d+$/.test(s))
    .sort();
  const relayBox = $("detail-relays");
  relayBox.classList.toggle("hidden", !relays.length);
  if (relays.length) {
    const existing = new Set([...relayBox.children].map((b) => b.dataset.suffix));
    for (const suffix of relays) {
      let button = relayBox.querySelector('[data-suffix="' + suffix + '"]');
      if (!button) {
        button = document.createElement("button");
        button.className = "relay-btn";
        button.dataset.suffix = suffix;
        button.addEventListener("click", () =>
          publish({ host: dev.host }, suffix + "/set", "TOGGLE"));
        relayBox.appendChild(button);
      }
      button.textContent = tf("detail.relay", { n: suffix.split("/")[1] })
        + (isOn(topicValue(dev, suffix)) ? "  " + t("word.on")
                                         : "  " + t("word.off"));
      button.classList.toggle("on", isOn(topicValue(dev, suffix)));
      existing.delete(suffix);
    }
    for (const stale of existing) relayBox.querySelector('[data-suffix="' + stale + '"]').remove();
  }

  /* leds */
  const leds = Object.keys(dev.topics).filter((s) => /^led\/[^/]+$/.test(s))
    .sort();
  const ledBox = $("detail-leds");
  ledBox.classList.toggle("hidden", !leds.length);
  const ledExisting = new Set([...ledBox.children].map((r) => r.dataset.suffix));
  for (const suffix of leds) {
    let row = ledBox.querySelector('[data-suffix="' + suffix + '"]');
    if (!row) {
      const name = suffix.split("/")[1];
      row = document.createElement("div");
      row.className = "led-row";
      row.dataset.suffix = suffix;
      const label = document.createElement("span");
      label.className = "led-name";
      label.textContent = name;
      const input = document.createElement("input");
      input.type = "range";
      input.min = 0;
      input.max = 100;
      input.addEventListener("change", () =>
        publish({ host: dev.host }, suffix + "/set", input.value));
      row.appendChild(label);
      row.appendChild(input);
      const value = document.createElement("span");
      value.className = "led-value";
      row.appendChild(value);
      ledBox.appendChild(row);
    }
    const input = row.querySelector("input");
    input.value = topicValue(dev, suffix) || 0;
    row.querySelector(".led-value").textContent = topicValue(dev, suffix) || "0";
    ledExisting.delete(suffix);
  }
  for (const stale of ledExisting) ledBox.querySelector('[data-suffix="' + stale + '"]').remove();

  $("detail-controls").classList.toggle("hidden",
                                         !relays.length && !leds.length
                                         && !dev.online);

  /* topics */
  const list = $("detail-topics");
  list.innerHTML = "";
  const suffixes = Object.keys(dev.topics).sort();
  for (const suffix of suffixes) {
    const record = dev.topics[suffix];
    const row = document.createElement("div");
    row.className = "topic-row";
    row.innerHTML =
      '<span class="t-suffix">' + esc(suffix)
      + (record.retain ? ' <span class="retain">▮</span>' : "")
      + '</span><span class="t-value" title="'
      + esc(record.value) + '">' + esc(record.value) + "</span>"
      + '<span class="t-age">' + fmtAge(record.age) + "</span>";
    list.appendChild(row);
  }
}

/* The beacon's detail: what it is, who hears it and how well, and every
 * field it advertised. Nothing to control -- a beacon is listened to,
 * not talked to. */
function renderBeaconDetail() {
  const beacon = state.byAddr[detailTarget.addr];
  if (!beacon) return closeDetail();

  $("detail-seenby").classList.remove("hidden");
  $("detail-controls").classList.add("hidden");
  $("detail-delete").classList.add("hidden");
  renderDetailPin(null);

  $("detail-title").textContent = beacon.name
    || (beacon.addr.slice(0, 4) + "…" + beacon.addr.slice(-4));

  $("detail-status").innerHTML =
    (beacon.active
      ? '<b style="color:var(--lcars-teal)">' + t("word.heard") + "</b>"
      : '<b style="color:var(--dim)">' + t("word.asleep") + "</b>")
    + tf("detail.beaconStatus", {
      what: [esc(beacon.type || "?"),
             beacon.id ? esc(beacon.id) : "",
             esc(beacon.addr)].filter(Boolean).join(" · "),
      first: esc(beacon.first_seen || "-"),
      age: beacon.age != null ? fmtAge(beacon.age) : "-",
    });

  const seenby = $("seenby-list");
  seenby.innerHTML = "";
  const heading = $("detail-seenby").querySelector("h3");
  if (heading) heading.textContent = t("detail.heardBy");
  for (const seen of beacon.devices) {
    const row = document.createElement("div");
    row.className = "topic-row seenby-row";
    row.innerHTML =
      '<span class="t-suffix">' + esc(seen.host) + "</span>"
      + '<span class="t-value">' + (seen.rssi != null ? esc(seen.rssi)
                                     + " dBm" : "-") + "</span>"
      + '<span class="t-age">' + fmtAge(seen.age) + "</span>";
    seenby.appendChild(row);
  }

  const list = $("detail-topics");
  list.innerHTML = "";
  for (const field of Object.keys(beacon.fields).sort()) {
    const record = beacon.fields[field];
    const row = document.createElement("div");
    row.className = "topic-row";
    row.innerHTML =
      '<span class="t-suffix">' + esc(field) + "</span>"
      + '<span class="t-value" title="' + esc(record.value) + '">'
      + esc(record.value) + "</span>"
      + '<span class="t-age">' + fmtAge(record.age) + "</span>";
    list.appendChild(row);
  }
}

/* The access point's detail: which panels are on it and how well it
 * hears each of them. Nothing to control -- an access point is
 * listened to, not talked to -- and the pin button, because a place
 * kept is a thing worth seeing here too. */
function renderApDetail() {
  const box = boxes.find((b) => b.kind === "ap"
                            && b.bssid === detailTarget.bssid);
  if (!box) return closeDetail();

  $("detail-seenby").classList.remove("hidden");
  $("detail-controls").classList.add("hidden");
  $("detail-delete").classList.add("hidden");

  $("detail-title").textContent = box.ssid
    || (box.bssid.slice(0, 4) + "…" + box.bssid.slice(-4));

  const bestPanel = box.panels.reduce((m, p) => (p.q > m.q ? p : m),
                                      { q: 0, rssi: null });
  $("detail-status").innerHTML =
    tf("detail.apStatus", {
      bssid: esc(bssidText(box.bssid)),
      n: box.panels.length,
      best: bestPanel.rssi != null && isFinite(bestPanel.rssi)
        ? Math.round(bestPanel.rssi) + " dBm" : "-",
    });

  const heading = $("detail-seenby").querySelector("h3");
  if (heading) heading.textContent = t("detail.panels");

  const seenby = $("seenby-list");
  seenby.innerHTML = "";
  for (const panel of box.panels) {
    const deviceBox = boxFor(panel.host);
    const row = document.createElement("div");
    row.className = "topic-row seenby-row";
    row.innerHTML =
      '<span class="t-suffix">' + esc(panel.host) + "</span>"
      + '<span class="t-value">'
      + (panel.rssi != null ? esc(Math.round(panel.rssi)) + " dBm" : "-")
      + "</span>"
      + '<span class="t-age">'
      + (deviceBox ? (deviceBox.dev && deviceBox.dev.online
                      ? t("word.online") : t("word.offline")) : "-")
      + "</span>";
    seenby.appendChild(row);
  }

  const list = $("detail-topics");
  list.innerHTML = "";

  renderDetailPin(box);
}

$("detail-close").addEventListener("click", closeDetail);
$("detail-sound-play").addEventListener("click", () => {
  if (!detailTarget || detailTarget.kind !== "device") return;
  publish({ host: detailTarget.host }, "sound/set", $("detail-sound").value);
});
$("detail-delete").addEventListener("click", async () => {
  if (!detailTarget || detailTarget.kind !== "device") return;
  try {
    await api("/api/device/" + encodeURIComponent(detailTarget.host) + "/delete");
    closeDetail();
  } catch (error) {
    toast(error.message, "err");
  }
});

/* --------------------------------------------------------------- settings */

/* The dialog works on its own copy of the profile list: adding,
 * removing and editing happen in the page, and one save makes them all
 * true at once. Which profile the tool is connected to is the top bar's
 * selector, not this dialog.
 *
 * The list is alphabetical everywhere it is shown. The entry being
 * edited is followed by object, not by position, so re-sorting -- a
 * rename that moves a profile up the list -- never yanks the editor
 * onto the wrong row. */
const brokerDialog = {
  brokers: [],          // the draft, in the server's (alphabetical) order
  editing: null,        // the profile object the form is showing
};

function activeProfileIndex() {
  const brokers = state.settings.brokers || [];
  const index = state.settings.broker_index || 0;
  return Math.max(0, Math.min(index, brokers.length - 1));
}

function renderBrokerChip() {
  const b = state.broker;
  const profile = (state.settings.brokers || [])[activeProfileIndex()] || {};
  const label = b.connected
    ? profile.mqtt_host + ":" + b.port
      + (profile.base_topic ? "  ·  " + profile.base_topic + "/#" : "")
    : t("broker.chipOffline");
  $("broker-dot").className = "dot" + (b.connected ? " on" : "");
  $("broker-label").textContent = label;
}

/* The selector in the top bar lists the profiles by name; picking one
 * switches the tool to it, which reconnects with that profile's
 * connection. It is rebuilt only when the list actually changes, so a
 * poll never fights the open dropdown. */
let brokerSelectSignature = "";

function renderBrokerSelect() {
  const brokers = state.settings.brokers || [];
  const select = $("broker-select");
  const signature = brokers.map((p) => p.name).join("|")
                    + "#" + activeProfileIndex();

  if (signature === brokerSelectSignature) return;
  brokerSelectSignature = signature;

  select.textContent = "";
  brokers.forEach((profile, index) => {
    const option = document.createElement("option");
    option.value = index;
    option.textContent = profile.name;
    option.selected = index === activeProfileIndex();
    select.appendChild(option);
  });
}

$("broker-select").addEventListener("change", async () => {
  const index = Number($("broker-select").value);
  try {
    await api("/api/settings", { broker_index: index });
    toast(tf("toast.switching",
             { name: ((state.settings.brokers || [])[index] || {}).name }));
    pollState();
  } catch (error) {
    toast(error.message, "err");
  }
});

function renderBrokerList() {
  const list = $("broker-list");
  list.textContent = "";

  const active = (state.settings.brokers || [])[activeProfileIndex()] || {};
  const ordered = [...brokerDialog.brokers].sort(
    (a, b) => a.name.localeCompare(b.name));

  for (const profile of ordered) {
    const row = document.createElement("div");
    row.className = "broker-row"
                     + (profile === brokerDialog.editing ? " editing" : "");
    row.innerHTML =
      '<span class="b-name">' + esc(profile.name) + "</span>"
      + '<span class="b-host">' + esc(profile.mqtt_host + ":"
                                       + profile.mqtt_port) + "</span>"
      + (profile.id && profile.id === active.id
         ? '<span class="b-active">' + t("broker.inUse") + "</span>" : "");
    row.addEventListener("click", () => {
      brokerDialog.editing = profile;
      renderBrokerList();
      fillBrokerForm();
    });
    list.appendChild(row);
  }
}

function fillBrokerForm() {
  const profile = brokerDialog.editing || {};
  $("set-name").value = profile.name || "";
  $("set-host").value = profile.mqtt_host || "";
  $("set-port").value = profile.mqtt_port || 1883;
  $("set-user").value = profile.mqtt_user || "";
  $("set-pass").value = profile.mqtt_pass || "";
  $("set-base").value = profile.base_topic || "oheztouch";
}

/* Editing a field writes back into the draft immediately, so switching
 * the edited profile and back loses nothing. A rename re-sorts the
 * list, and the edited entry keeps its highlight wherever it lands. */
function editBrokerField(field, input) {
  const profile = brokerDialog.editing;
  if (!profile) return;
  profile[field] = input.value;
  if (field === "name") renderBrokerList();
}

for (const [field, id] of [["name", "set-name"],
                          ["mqtt_host", "set-host"],
                          ["mqtt_port", "set-port"],
                          ["mqtt_user", "set-user"],
                          ["mqtt_pass", "set-pass"],
                          ["base_topic", "set-base"]]) {
  $(id).addEventListener("input", (event) =>
    editBrokerField(field, event.target));
}

function uniqueBrokerName(base) {
  const names = new Set(brokerDialog.brokers.map((p) => p.name));
  if (!names.has(base)) return base;
  let n = 2;
  while (names.has(base + " " + n)) n++;
  return base + " " + n;
}

$("broker-add").addEventListener("click", () => {
  const profile = {
    id: "new-" + Math.random().toString(16).slice(2, 10),
    name: uniqueBrokerName("broker"),
    mqtt_host: "localhost",
    mqtt_port: 1883,
    mqtt_user: "",
    mqtt_pass: "",
    base_topic: "oheztouch",
  };
  brokerDialog.brokers.push(profile);
  brokerDialog.editing = profile;
  renderBrokerList();
  fillBrokerForm();
});

$("broker-remove").addEventListener("click", () => {
  if (brokerDialog.brokers.length <= 1) {
    toast(t("toast.lastBroker"), "err");
    return;
  }

  const index = brokerDialog.brokers.indexOf(brokerDialog.editing);
  brokerDialog.brokers.splice(index, 1);
  brokerDialog.editing = brokerDialog.brokers[
    Math.min(index, brokerDialog.brokers.length - 1)];
  renderBrokerList();
  fillBrokerForm();
});

function openSettings() {
  const s = state.settings;
  brokerDialog.brokers = JSON.parse(JSON.stringify(s.brokers || [{}]));
  const active = (s.brokers || [])[activeProfileIndex()] || {};
  brokerDialog.editing = brokerDialog.brokers.find((p) => p.id === active.id)
                         || brokerDialog.brokers[0];
  $("set-enabled").checked = !!s.mqtt_enabled;
  $("set-verbose").checked = !!s.verbose;
  $("set-lang").value = s.lang === "de" ? "de" : "en";
  renderBrokerList();
  fillBrokerForm();
  $("settings").classList.remove("hidden");
}

function closeSettings() {
  $("settings").classList.add("hidden");
}

$("btn-settings").addEventListener("click", openSettings);
$("settings-close").addEventListener("click", closeSettings);

$("settings-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  try {
    await api("/api/settings", {
      mqtt_enabled: $("set-enabled").checked,
      brokers: brokerDialog.brokers,
      verbose: $("set-verbose").checked,
      lang: $("set-lang").value,
    });
    /* The language acts at once, the way it acts on the server. */
    setLang($("set-lang").value);
    closeSettings();
    toast(t("toast.settingsSaved"));
    pollState();
  } catch (error) {
    toast(error.message, "err");
  }
});

/* ---------------------------------------------------------------- layouts */

/* The topology view's arrangements are worth keeping more than one of:
 * this dialog saves the canvas as it stands under a name, brings a
 * saved one back whole -- places and picture both, which is what a
 * layout is -- and hands the whole table over as a file. The server
 * keeps a backup file beside the state (mqttviz/data/layouts.json,
 * rewritten on every change, remembering what stood before the last
 * restore), and the button here writes the same content to a file of
 * the user's own; importing one adds, it never wipes. */

function layoutList() {
  return state.settings.layouts || {};
}

function openLayouts() {
  renderLayouts();
  $("layouts").classList.remove("hidden");
}

function closeLayouts() {
  $("layouts").classList.add("hidden");
}

$("btn-layouts").addEventListener("click", openLayouts);
$("layouts-close").addEventListener("click", closeLayouts);

function renderLayouts() {
  const list = $("layout-list");
  list.textContent = "";

  const layouts = layoutList();
  const names = Object.keys(layouts).sort((a, b) =>
    String(layouts[b].saved_at).localeCompare(String(layouts[a].saved_at)));

  if (!names.length) {
    const empty = document.createElement("p");
    empty.className = "hint";
    empty.textContent = t("lay.empty");
    list.appendChild(empty);
    return;
  }

  for (const name of names) {
    const layout = layouts[name];
    const pins = Object.keys(layout.positions || {}).length;
    const mode = t(layout.view_mode === "topology"
                   ? "view.topology" : "view.mesh");

    const row = document.createElement("div");
    row.className = "layout-row";

    const label = document.createElement("span");
    label.className = "l-name";
    label.textContent = name;

    const meta = document.createElement("span");
    meta.className = "l-meta";
    meta.textContent = tf("lay.meta",
                          { n: pins, mode, date: layout.saved_at || "" });

    const load = document.createElement("button");
    load.className = "lcars-btn small";
    load.textContent = t("lay.load");
    load.addEventListener("click", () => loadLayout(name));

    const del = document.createElement("button");
    del.className = "lcars-btn small danger";
    del.textContent = t("lay.delete");
    del.addEventListener("click", () => deleteLayout(name));

    row.append(label, meta, load, del);
    list.appendChild(row);
  }
}

/* Saving follows the arrangement the canvas has right now, not the
 * poll's copy of it: the settings the route answers with are the
 * truth the moment the name was given. */
async function saveLayout() {
  const input = $("layout-name");
  const name = input.value.trim();
  if (!name) return toast(t("lay.nameNeeded"), "err");

  try {
    const doc = await api("/api/layout/save", { name });
    state.settings = doc.settings;
    renderLayouts();
    input.value = "";
    toast(tf("toast.layoutSaved", { name }));
  } catch (error) {
    toast(error.message, "err");
  }
}

/* Restoring is the canvas re-forming itself: the springs move the
 * boxes to the places the layout kept, the way a view switch does --
 * and what stood before is in the backup file, so trying a layout on
 * costs nothing. */
async function loadLayout(name) {
  try {
    const doc = await api("/api/layout/load", { name });
    state.settings = doc.settings;
    syncBoxes();
    renderViewToggles();
    renderPhysicsTabs();
    renderLayouts();
    if (fxOn()) {
      const pos = (state.settings.positions || {})["broker"];
      if (pos) wave(pos.x * stage.w, pos.y * stage.h, PALETTE.orange,
                   46, 550);
    }
    toast(tf("toast.layoutLoaded", { name }));
  } catch (error) {
    toast(error.message, "err");
  }
}

async function deleteLayout(name) {
  try {
    const doc = await api("/api/layout/delete", { name });
    state.settings = doc.settings;
    renderLayouts();
    toast(tf("toast.layoutDeleted", { name }));
  } catch (error) {
    toast(error.message, "err");
  }
}

$("layout-save").addEventListener("click", saveLayout);

/* The file the page writes is the file the server keeps: current
 * arrangement, every saved layout -- the same content, in a place the
 * user chooses. */
$("layout-download").addEventListener("click", () => {
  const doc = {
    written: new Date().toISOString().slice(0, 19).replace("T", " "),
    current: {
      view_mode: state.settings.view_mode || "mesh",
      positions: state.settings.positions || {},
    },
    layouts: layoutList(),
  };

  const blob = new Blob([JSON.stringify(doc, null, 2) + "\n"],
                        { type: "application/json" });
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  link.download = "mqttviz-layouts.json";
  link.click();
  URL.revokeObjectURL(link.href);
  toast(t("toast.backupWritten"));
});

/* Reading one back: the downloaded file or the server's backup file,
 * or a bare table of layouts -- anything with layouts in it. The
 * server checks every entry, so a file that is not one says so by
 * importing nothing. */
$("layout-import").addEventListener("change", async (event) => {
  const file = event.target.files[0];
  event.target.value = "";        // picking the same file again counts
  if (!file) return;

  let doc;
  try {
    doc = JSON.parse(await file.text());
  } catch (error) {
    return toast(t("toast.layoutImportFail"), "err");
  }

  const incoming = (doc && typeof doc === "object" && doc.layouts)
                   || (doc && typeof doc === "object" ? doc : null);
  if (!incoming || typeof incoming !== "object") {
    return toast(t("toast.layoutImportFail"), "err");
  }

  try {
    const result = await api("/api/layout/import", { layouts: incoming });
    state.settings = result.settings;
    renderLayouts();
    toast(tf("toast.layoutImported", { n: result.imported }));
  } catch (error) {
    toast(error.message, "err");
  }
});

/* ---------------------------------------------------------------- console */

function pollConsole() {
  const level = consoleState.paused ? "none" : "debug";
  if (consoleState.paused) return scheduleConsole();

  api("/api/log?since=" + consoleState.since)
    .then((doc) => {
      for (const entry of doc.entries) {
        consoleState.since = Math.max(consoleState.since, entry.seq);

        if (!$("console").classList.contains("hidden")
            || consoleState.levels.has(entry.level)) {
          consoleState.entries.push(entry);
        }
        if (entry.level === "warn" || entry.level === "error") {
          consoleState.unseen++;
          renderConsoleBadge();
        }
      }
      if (doc.entries.length) renderConsole();
    })
    .catch(() => {});

  scheduleConsole();
}

function scheduleConsole() {
  setTimeout(pollConsole, 2000);
}

function renderConsole() {
  const box = $("console-entries");
  box.innerHTML = "";

  const visible = consoleState.entries
    .filter((e) => consoleState.levels.has(e.level))
    .slice(-300);

  for (const entry of visible) {
    const line = document.createElement("div");
    line.className = "log-line log-" + entry.level;
    line.innerHTML =
      '<span class="l-ts">' + esc(entry.ts) + "</span>"
      + '<span class="l-level">' + esc(entry.level) + "</span>"
      + '<span class="l-source">' + esc(entry.source) + "</span>"
      + '<span class="l-msg">' + esc(entry.message) + "</span>";
    box.appendChild(line);
  }
  box.scrollTop = box.scrollHeight;
}

function renderConsoleBadge() {
  const badge = $("console-badge");
  badge.classList.toggle("hidden", consoleState.unseen === 0);
  badge.textContent = consoleState.unseen;
}

$("btn-console").addEventListener("click", () => {
  const panel = $("console");
  const opening = panel.classList.contains("hidden");
  panel.classList.toggle("hidden");
  if (opening) {
    consoleState.unseen = 0;
    renderConsoleBadge();
    renderConsole();
  }
});

$("console-close").addEventListener("click", () => {
  $("console").classList.add("hidden");
});

$("console-clear").addEventListener("click", async () => {
  await api("/api/log/clear").catch(() => {});
  consoleState.entries = [];
  consoleState.since = 0;
  renderConsole();
});

$("console-copy").addEventListener("click", () => {
  const text = consoleState.entries
    .map((e) => e.ts + " " + e.level + " " + e.source + " " + e.message)
    .join("\n");
  navigator.clipboard.writeText(text).then(
    () => toast(t("toast.copied")),
    () => toast(t("toast.copyFail"), "err"));
});

for (const button of document.querySelectorAll("#console-filters .filter")) {
  button.addEventListener("click", () => {
    const level = button.dataset.level;
    if (consoleState.levels.has(level)) consoleState.levels.delete(level);
    else consoleState.levels.add(level);
    button.classList.toggle("active", consoleState.levels.has(level));
    renderConsole();
  });
}

/* ------------------------------------------------------------- language */

/* When the language turns over, the built pieces have to say their
 * lines again: the physics rows carry their labels and help texts,
 * and whatever panel is open re-renders. The static chrome was already
 * rewritten by applyLang() before this event fired. */
document.addEventListener("langchange", () => {
  buildPhysicsPanel();
  syncPhysicsReadouts();
  renderPhysicsTabs();
  renderBrokerChip();
  if (!$("settings").classList.contains("hidden")) renderBrokerList();
  if (!$("detail").classList.contains("hidden")) renderDetail();
});

/* ------------------------------------------------------------------- init */

/* Debug hook: with #debug in the URL, the live box geometry is written
 * into the page every half second, where a headless dump can read it. */
if (location.hash === "#debug") {
  setInterval(() => {
    $("debug-geom").textContent = JSON.stringify({
      boxes: boxes.map((b) => ({ kind: b.kind, host: b.host,
                                 cx: Math.round(b.cx), cy: Math.round(b.cy),
                                 w: b.w, h: b.h })),
      cam: { zoom: Math.round(cam.zoom * 100) / 100,
             x: Math.round(camView().x), y: Math.round(camView().y) },
      view: { w: Math.round(view.w), h: Math.round(view.h) },
      stage: { w: Math.round(stage.w), h: Math.round(stage.h) },
      brokerSelectValue: $("broker-select").value,
      brokerSelectOptions: [...$("broker-select").options]
        .map((o) => o.textContent),
    });
  }, 500);
}

requestAnimationFrame(function tick(time) {
  frameTime = time;
  applyDrags();
  requestAnimationFrame(draw);
});

resize();
buildPhysicsPanel();
/* The hub exists before the first poll: the physics and the particles
 * both reference it from their very first frame. */
ensureBrokerBox(new Set());
/* Deep link: #physics opens the physics panel, the way devmgr's
 * #config=<mac> opens a device's settings. */
if (location.hash === "#physics") openPhysics();
pollState();
pollConsole();
