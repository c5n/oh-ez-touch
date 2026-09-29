/* OhEzTouch MQTT Visualizer -- the page's logic.
 *
 * Everything comes from /api/state, polled once a second; the console's
 * log comes from /api/log with a cursor, so entries are fetched once and
 * in order. The devices are objects on a canvas: boxes that spring to an
 * orbit around the broker, drift a little while idle, repel each other,
 * and can be picked up and thrown. Every message a panel publishes
 * becomes a particle flowing from the broker to its box.
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
const PAD = 8;
const ROWS = { header: 30, sub: 15, sys: 22, net: 18, ui: 22, sens: 30,
               out: 28, foot: 18 };
const BEACON_ROWS = { header: 26, sub: 15, rssi: 20, spark: 22, chips: 20,
                      foot: 15 };
const BEACON_PAD = 6;

const stage = { w: 0, h: 0 };
const boxes = [];
const particles = [];
let hits = [];            // the previous frame's clickable regions
let frameTime = performance.now();

function resize() {
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.round(rect.width * dpr);
  canvas.height = Math.round(rect.height * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  stage.w = rect.width;
  stage.h = rect.height;
}

new ResizeObserver(resize).observe(canvas);

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
  return boxes.find((b) => b.kind !== "beacon" && b.host === host);
}

/* Turn the polled device and beacon lists into box objects. A new box
 * is born near the middle of the canvas, where its links begin, and
 * the network grows from there: the springs pull, the shoves spread,
 * and a moment later the mesh has found its shape. */
function syncBoxes() {
  const live = new Set();

  let born = 0;
  for (const dev of state.devices) {
    live.add(dev.host);

    let box = boxes.find((b) => b.kind !== "beacon" && b.host === dev.host);
    if (!box) {
      /* Born on its own slice of the ring, pointing outward: every
       * panel then glides along its own angle to its place, and none of
       * them has to cross the fleet to get there. */
      const angle = (born / Math.max(1, state.devices.length)) * Math.PI * 2
                    + (Math.random() - 0.5) * 0.4;
      const born_r = physParams("device").brokerRest * 0.45;
      box = {
        kind: "device",
        key: dev.host,
        host: dev.host,
        cx: stage.w / 2 + Math.cos(angle) * born_r,
        cy: stage.h / 2 + Math.sin(angle) * born_r,
        vx: 0,
        vy: 0,
        w: BOX_W,
        seed: Math.random() * 7,
        rows: [],
        h: 80,
        lastTs: {},
        flash: {},
        activity: 0,
        lastMsgcount: dev.msgcount,
        ledLocal: {},
        ledPublishAt: 0,
        drag: null,
      };
      boxes.push(box);
      born++;
    }

    box.dev = dev;
    box.rows = boxRows(dev);
    box.h = rowHeight(box.rows);

    /* Anything with a new timestamp just changed on the panel: flash the
     * field, wake the link, and let a particle say where it came from. */
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
      for (let i = 0; i < Math.min(3, delta); i++) spawnParticle(box);
    }
  }

  /* Beacons are only turned into boxes while they are switched on -- the
   * boxes vanish from the canvas and the seats the moment they are not,
   * and the beacons keep being tracked behind the switch. */
  if (state.settings.show_beacons !== false) {
    for (const beacon of state.beacons) {
      const key = "beacon:" + beacon.addr;
      live.add(key);

      let box = boxes.find((b) => b.key === key);
      if (!box) {
        /* A beacon is born where its lines already point -- the middle
         * of the panels that hear it -- so it never has to cross the
         * whole mesh to find its place; one nobody has placed yet waits
         * on the outer band, where the beacons end up anyway. */
        let sx = 0, sy = 0, heard = 0;
        for (const seen of beacon.devices) {
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
          rows: [],
          h: 60,
          lastCounts: {},
          flash: {},
          activity: 0,
          drag: null,
        };
        boxes.push(box);
      }

      box.dev = beacon;
      box.rows = beaconRows(beacon);
      box.h = beaconHeight(box.rows);

      /* A panel hearing the beacon again is a particle along that link:
       * the count says which link, so the flow follows who is listening. */
      for (const seen of beacon.devices) {
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
        if (!beacon.devices.some((d) => d.host === host)) {
          delete box.lastCounts[host];
        }
      }
    }
  }

  for (let i = boxes.length - 1; i >= 0; i--) {
    if (!live.has(boxes[i].key)) boxes.splice(i, 1);
  }
}

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

function physSpecFor(kind) {
  return kind === "broker" ? BROKER_SPEC : PHYS_SPEC;
}

const PHYS_SETTINGS_KEY = {
  device: "phys_nodes",
  beacon: "phys_beacons",
  broker: "phys_broker",
};

const PHYS_KINDS = ["device", "beacon", "broker"];

function physRowsId(kind) {
  return kind === "beacon" ? "physics-rows-beacons"
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
  };
}

/* How long a beacon's line to a panel wants to be: the stronger the
 * hearing, the shorter the leash -- a beacon heard at -40 dBm sits
 * close enough to touch its panels, one scraped at -90 hangs out on a
 * long line at the far edge of the mesh. */
function beaconRest(q) {
  return 170 + (1 - q) * 300;
}

function physicsStep(dt, time) {
  const params = { device: physParams("device"),
                   beacon: physParams("beacon"),
                   broker: physParams("broker") };
  const cx = stage.w / 2, cy = stage.h / 2;

  /* The panels share the ring around the broker, and the ring must
   * have room for them all: the broker link's rest length is never
   * shorter than the radius a full circle of boxes -- with their air
   * margin -- would need. */
  let panels = 0;
  for (const box of boxes) if (box.kind !== "beacon") panels++;
  const brokerRest = Math.max(params.device.brokerRest,
                              panels * (BOX_W + 50) / (Math.PI * 2));

  for (let i = 0; i < boxes.length; i++) {
    const box = boxes[i];
    const p = box.kind === "beacon" ? params.beacon : params.device;

    if (box.drag) {
      box.vx = box.drag.cx - box.cx;
      box.vy = box.drag.cy - box.cy;
      box.cx = box.drag.cx;
      box.cy = box.drag.cy;
      continue;
    }

    /* The links are springs, and they are the layout: a panel is held
     * by its line to the broker -- a true spring, two-way, so the ring
     * spreads -- while a beacon hangs on leashes to the panels that
     * hear it: a strong hearing a short leash, a weak one a long one.
     * A leash only ever pulls; when a beacon sits closer to a panel
     * than its line wants, the line goes slack rather than shoving the
     * panel away, so a crowded mesh never grinds against itself. */
    if (box.kind === "beacon" && box.dev) {
      for (const seen of box.dev.devices) {
        const deviceBox = boxFor(seen.host);
        if (!deviceBox || deviceBox === box) continue;

        const q = seen.rssi == null ? 0
                  : Math.max(0, Math.min(1, (seen.rssi + 100) / 70));
        const rest = beaconRest(q) * p.lenScale;
        const dx = deviceBox.cx - box.cx, dy = deviceBox.cy - box.cy;
        const dist = Math.hypot(dx, dy);
        if (!dist) continue;

        /* Stretched, the leash pulls both ends. Slack, it only sends
         * the beacon itself back out to the length of its line -- the
         * panel never feels a crowded mesh leaning on it. */
        const force = p.spring * (dist - rest) / dist * dt;
        box.vx += dx * force;
        box.vy += dy * force;
        if (dist > rest && !deviceBox.drag) {
          deviceBox.vx -= dx * force;
          deviceBox.vy -= dy * force;
        }
      }
    } else {
      const dx = cx - box.cx, dy = cy - box.cy;
      const dist = Math.hypot(dx, dy) || 1;
      const force = p.spring * (dist - brokerRest) / dist * dt;
      box.vx += dx * force;
      box.vy += dy * force;
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

    /* The home pull: a whisper toward the middle, so nothing the links
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

    /* Overlap is resolved positionally, on both axes at once, so a box
     * can always slide around the box in its way instead of bouncing
     * back off it. A held box is carried, not pushed; everything else
     * shares the work. */
    for (let j = i + 1; j < boxes.length; j++) {
      const other = boxes[j];

      /* Nodes rest with a breath of air between them: the margin keeps
       * a pressed pair from ever visually overlapping, so a mesh that
       * leans together still reads as separate things. */
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
      const q = other.kind === "beacon" ? params.beacon : params.device;
      if (p.grav !== 0 || q.grav !== 0) {
        const d = Math.max(Math.hypot(dx, dy), 80) || 80;
        const ax = dx / d, ay = dy / d;      // unit vector, box -> other
        other.vx -= ax * (p.grav / d) * dt;  // this box's pull on it
        other.vy -= ay * (p.grav / d) * dt;
        box.vx += ax * (q.grav / d) * dt;    // its pull on this box
        box.vy += ay * (q.grav / d) * dt;
      }

      if (overlapX > 0 && overlapY > 0) {
        const weightBox = box.drag ? 0 : 1;
        const weightOther = other.drag ? 0 : 1;
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
        box.vx -= dirX * 0.12 * ease * weightBox / total;
        box.vy -= dirY * 0.12 * ease * weightBox / total;
        other.vx += dirX * 0.12 * ease * weightOther / total;
        other.vy += dirY * 0.12 * ease * weightOther / total;
      }
    }

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
  const p = physParams(kind === "beacon" ? "beacon" : "device");
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

function draw(time) {
  const w = stage.w, h = stage.h;
  ctx.clearRect(0, 0, w, h);

  hits = [];

  const bx = w / 2, by = h / 2;

  /* -- links ------------------------------------------------------ */

  for (const box of boxes) {
    box.activity = Math.max(0, box.activity - 0.008);
    for (const suffix in box.flash)
      box.flash[suffix] = Math.max(0, box.flash[suffix] - 0.015);
  }

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

      const hang = hangControl(box.cx, box.cy, deviceBox.cx, deviceBox.cy,
                               "beacon");
      ctx.beginPath();
      ctx.moveTo(box.cx, box.cy);
      ctx.quadraticCurveTo(hang.x, hang.y, deviceBox.cx, deviceBox.cy);

      const mix = (a, b) => Math.round(a + (b - a) * q);
      const pulse = 0.06 * q * (1 + Math.sin(time * 0.003 + box.seed * 4));
      const asleep = box.dev.active ? 1 : 0.4;
      ctx.strokeStyle = "rgba(" + mix(90, 95) + "," + mix(107, 201) + ","
                        + mix(128, 176) + ","
                        + ((0.10 + 0.55 * q + pulse) * asleep) + ")";
      ctx.lineWidth = 0.6 + 2.6 * q;
      ctx.stroke();
    }
  }

  /* The broker links: every panel hangs on the broker -- and the node
   * with them, because links to nothing are a picture of the wrong
   * thing. The panels themselves stay anchored to the middle of the
   * canvas either way. */
  const showBroker = state.settings.show_broker !== false;

  if (showBroker) {
    for (const box of boxes) {
      if (box.kind === "beacon") continue;

      const hang = hangControl(bx, by, box.cx, box.cy);
      ctx.beginPath();
      ctx.moveTo(bx, by);
      ctx.quadraticCurveTo(hang.x, hang.y, box.cx, box.cy);
      ctx.strokeStyle = "rgba(153, 194, 255, " + (0.05 + box.activity * 0.22)
                        + ")";
      ctx.lineWidth = 1;
      ctx.stroke();
    }
  }

  /* -- particles ---------------------------------------------------- */

  const dt = Math.min(0.05, (time - frameTime) / 1000) * 60;
  frameTime = time;

  for (let i = particles.length - 1; i >= 0; i--) {
    const p = particles[i];
    p.t += dt / 60 / p.dur;

    if (p.t >= 1 || !boxes.includes(p.to)
        || (p.from && !boxes.includes(p.from))) {
      p.to.activity = 1;
      particles.splice(i, 1);
      continue;
    }

    /* A broker particle is part of the broker picture. */
    if (!p.from && !showBroker) continue;

    const fx = p.from ? p.from.cx : bx;
    const fy = p.from ? p.from.cy : by;
    const target = p.to;
    /* The particle rides the line that hangs there, with a little slack
     * of its own, so a busy link is a bundle of threads and not one. */
    const hang = hangControl(fx, fy, target.cx, target.cy,
                             p.from ? "beacon" : "device");
    const cx1 = hang.x, cy1 = hang.y + p.off * 0.15;

    for (let trail = 0; trail < 3; trail++) {
      const t = Math.max(0, p.t - trail * 0.045);
      const u = 1 - t;
      const px = u * u * fx + 2 * u * t * cx1 + t * t * target.cx;
      const py = u * u * fy + 2 * u * t * cy1 + t * t * target.cy;
      ctx.beginPath();
      ctx.arc(px, py, 2.4 - trail * 0.7, 0, Math.PI * 2);
      const tint = p.from ? "95, 201, 176" : "153, 194, 255";
      ctx.fillStyle = "rgba(" + tint + "," + (0.9 - trail * 0.3) + ")";
      ctx.fill();
    }
  }

  /* -- broker node --------------------------------------------------- */

  if (showBroker) drawBroker(bx, by);

  /* -- device boxes --------------------------------------------------- */

  for (const box of boxes) {
    if (box.kind === "beacon") drawBeacon(box, time);
    else drawBox(box, time);
  }

  /* -- physics and repeat ---------------------------------------------- */

  applyDrags();
  physicsStep(Math.min(2.5, dt), time / 1000);
  requestAnimationFrame(draw);
}

function drawBroker(bx, by) {
  const b = state.broker;
  const label = b.connected ? b.host + ":" + b.port : t("broker.noBroker");
  const sub = b.connected
    ? "in " + b.messages_in + "  ·  out " + b.messages_out
    : t("broker.connectionOff");

  const nw = 190, nh = 48;
  const x = bx - nw / 2, y = by - nh / 2;

  ctx.save();
  ctx.shadowColor = "rgba(255, 156, 0, " + (b.connected ? 0.5 : 0.15) + ")";
  ctx.shadowBlur = 18;
  chip(x, y, nw, nh, b.connected ? "#1a2030" : "#191521",
       b.connected ? PALETTE.orange : PALETTE.bad);
  ctx.restore();

  ctx.fillStyle = b.connected ? PALETTE.orange : PALETTE.bad;
  rr(x - 8, y + 6, 8, nh - 12, 3);
  ctx.fill();

  ctx.beginPath();
  ctx.arc(x + 22, y + nh / 2, 5, 0, Math.PI * 2);
  ctx.fillStyle = b.connected ? PALETTE.good : PALETTE.bad;
  ctx.shadowColor = b.connected ? PALETTE.good : PALETTE.bad;
  ctx.shadowBlur = 8;
  ctx.fill();
  ctx.shadowBlur = 0;

  text(trunc(label, 22), x + 36, y + nh / 2 - 8, SANS(13, 600), PALETTE.text);
  text(sub, x + 36, y + nh / 2 + 10, MONO(10), PALETTE.dim);

  if (!state.devices.length) {
    text(tf("broker.waiting",
            { topic: state.settings.base_topic || "oheztouch" }),
         bx, by + nh / 2 + 34, SANS(13), PALETTE.dim, "center");
  }
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
  const rect = canvas.getBoundingClientRect();
  const px = event.clientX - rect.left, py = event.clientY - rect.top;
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
  }
});

canvas.addEventListener("pointermove", (event) => {
  const rect = canvas.getBoundingClientRect();
  const px = event.clientX - rect.left, py = event.clientY - rect.top;

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

  /* A hover over anything clickable should say so. */
  const hit = hitAt(px, py);
  canvas.style.cursor = hit ? "pointer" : "grab";
});

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
      } else if (drag.box.kind === "beacon") {
        openBeaconDetail(drag.box.host);
      } else {
        openDetail(drag.box.host);
      }
    }
    /* A thrown box keeps its throw, and the links gather it back into
     * the mesh -- the network heals the same way it was made. */
  }

  pointer.boxDrag = null;
  pointer.down = null;
  canvas.classList.remove("grabbing");
});

canvas.addEventListener("pointercancel", () => {
  if (pointer.boxDrag) pointer.boxDrag.box.drag = null;
  pointer.boxDrag = null;
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

$("btn-view-broker").addEventListener("click", () =>
  toggleView("show_broker"));
$("btn-view-beacons").addEventListener("click", () =>
  toggleView("show_beacons"));

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
  for (const [kind, id] of [["device", "phys-tab-nodes"],
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

let detailTarget = null;        // {kind: "device", host} | {kind: "beacon", addr}

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

function closeDetail() {
  detailTarget = null;
  $("detail").classList.add("hidden");
}

function renderDetail() {
  if (!detailTarget) return closeDetail();
  if (detailTarget.kind === "beacon") return renderBeaconDetail();

  const dev = state.byHost[detailTarget.host];
  if (!dev) return closeDetail();

  $("detail-seenby").classList.add("hidden");
  $("detail-delete").classList.remove("hidden");
  $("detail-title").textContent = dev.host;

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
/* Deep link: #physics opens the physics panel, the way devmgr's
 * #config=<mac> opens a device's settings. */
if (location.hash === "#physics") openPhysics();
pollState();
pollConsole();
