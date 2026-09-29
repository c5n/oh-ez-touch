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
  byHost: {},
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
    state.failed = false;
  } catch (error) {
    if (!state.failed) {
      state.failed = true;
      toast("mqttviz not reachable: " + error.message, "err");
    }
  }

  syncBoxes();
  renderBrokerChip();
  if (!$("detail").classList.contains("hidden")) renderDetail();
  state.pollTimer = setTimeout(pollState, 1000);
}

/* ----------------------------------------------------------------- canvas */

const canvas = $("canvas");
const ctx = canvas.getContext("2d");

const BOX_W = 250;
const PAD = 8;
const ROWS = { header: 30, sub: 15, sys: 22, net: 18, ui: 22, sens: 30,
               out: 28, foot: 18 };

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

/* Turn the polled device list into box objects. A panel that appears,
 * appears on its seat: seats are computed once, after every box that is
 * going to exist this pass exists -- a seat worked out against a partial
 * fleet is the wrong seat, and a box placed on the wrong one has to
 * migrate across the ring through the boxes already sitting there. */
function syncBoxes() {
  const live = new Set();
  const fresh = [];

  for (const dev of state.devices) {
    live.add(dev.host);

    let box = boxes.find((b) => b.host === dev.host);
    if (!box) {
      box = {
        host: dev.host,
        cx: stage.w / 2,
        cy: stage.h / 2,
        vx: (Math.random() - 0.5) * 2,
        vy: (Math.random() - 0.5) * 2,
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
        homing: true,
        homingSince: 0,
      };
      boxes.push(box);
      fresh.push(box);
    }

    box.dev = dev;
    box.rows = boxRows(dev);
    box.h = rowHeight(box.rows);

    /* Anything with a new timestamp just changed on the panel: flash the
     * field, wake the link, and let a particle say where it came from. */
    const now = performance.now();
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

  if (fresh.length && stage.w) {
    const anchors = layoutAnchors();
    for (const box of fresh) {
      const seat = anchors[boxes.indexOf(box)];
      if (seat) {
        box.cx = seat.x + (Math.random() - 0.5) * 10;
        box.cy = seat.y + (Math.random() - 0.5) * 10;
      }
    }
  }

  for (let i = boxes.length - 1; i >= 0; i--) {
    if (!live.has(boxes[i].host)) boxes.splice(i, 1);
  }
}

function spawnParticle(box) {
  if (particles.length > 120) particles.shift();
  particles.push({
    box,
    t: 0,
    dur: 0.7 + Math.random() * 0.5,
    off: (Math.random() - 0.5) * 120,
  });
}

/* Anchors: seats on an orbit around the broker, one per device, sized
 * and nudged apart until every box has room of its own. A fleet too big
 * for the orbit -- which a home canvas is quick to be, at a quarter of
 * its width per panel -- falls back to a packed grid, which always
 * separates as long as the canvas has the space. Seats that overlap are
 * a standoff the boxes can never settle, so the layout, not the
 * physics, carries the guarantee.
 *
 * The seats are computed from the fleet and the canvas alone, never
 * from the time: an orbit that rotates re-solves its seating every
 * frame, and a solve that succeeds at one angle and fails at the next
 * flips the whole fleet between two arrangements, churning the boxes
 * each time it does. The flow in the picture comes from the boxes --
 * the drift, the springs, the throws -- not from the seats moving. */
function layoutAnchors() {
  const n = boxes.length;
  const cx = stage.w / 2, cy = stage.h / 2;
  if (!n) return [];

  let maxH = 0;
  for (const box of boxes) maxH = Math.max(maxH, box.h);

  const margin = 24;
  const capX = Math.max(120, (stage.w - BOX_W) / 2 - 16);
  const capY = Math.max(100, (stage.h - maxH) / 2 - 16);

  /* One push-apart attempt: every overlapping pair of seats moved apart
   * along the cheaper axis, one full sweep at a time, with the seats
   * clamped onto the canvas between sweeps -- a seat pushed off the
   * edge is pushed back in by the next, so an "all clear" here means
   * clear where the boxes can actually sit. Returns whether every pair
   * ended up clear. */
  function relax(anchors, passes) {
    for (let pass = 0; pass < passes; pass++) {
      let settled = true;

      for (let i = 0; i < n; i++) {
        for (let j = i + 1; j < n; j++) {
          const a = anchors[i], b = anchors[j];
          const needX = (a.box.w + b.box.w) / 2 + margin;
          const needY = (a.box.h + b.box.h) / 2 + margin;
          const dx = b.x - a.x, dy = b.y - a.y;
          const overlapX = needX - Math.abs(dx);
          const overlapY = needY - Math.abs(dy);

          if (overlapX > 0 && overlapY > 0) {
            settled = false;
            if (overlapX < overlapY) {
              const dir = dx >= 0 ? 1 : -1;
              a.x -= dir * overlapX / 2;
              b.x += dir * overlapX / 2;
            } else {
              const dir = dy >= 0 ? 1 : -1;
              a.y -= dir * overlapY / 2;
              b.y += dir * overlapY / 2;
            }
          }
        }
      }

      if (settled) return true;

      for (const a of anchors) {
        a.x = Math.max(a.box.w / 2 + 8, Math.min(stage.w - a.box.w / 2 - 8, a.x));
        a.y = Math.max(a.box.h / 2 + 8, Math.min(stage.h - a.box.h / 2 - 8, a.y));
      }
    }
    return false;
  }

  /* The orbit attempt: a ring per device, growing a step every time the
   * seats cannot be nudged apart onto it. It stays within the canvas,
   * because a ring larger than the canvas has its edge seats clamped
   * back into the crowd, which is the standoff this exists to avoid. */
  const byCount = (n * (BOX_W + 2 * margin)) / (2 * Math.PI);
  let ringX = Math.min(capX, Math.max(190, byCount));
  let ringY = Math.min(capY, Math.max(150, ringX * 0.72));

  let anchors = null;
  for (let attempt = 0; attempt < 6; attempt++) {
    const seats = boxes.map((box, i) => ({
      box,
      x: cx + Math.cos((i / n) * Math.PI * 2) * ringX,
      y: cy + Math.sin((i / n) * Math.PI * 2) * ringY,
    }));

    if (relax(seats, 60)) {
      anchors = seats;
      break;
    }

    ringX = Math.min(capX, ringX * 1.15);
    ringY = Math.min(capY, ringY * 1.15);
  }

  /* The grid fallback: rows of boxes, packed by the tallest in each row.
   * It does not rotate -- a grid that drifted would be a grid that
   * overlaps its neighbours' seats -- and it is only reached when the
   * canvas is fuller than the orbit can host, which is when a steady
   * grid matters more than a flowing ring anyway. */
  if (!anchors) {
    const cols = Math.max(1, Math.floor((stage.w - 16) / (BOX_W + margin)));
    const rows = Math.ceil(n / cols);
    const grid = [];

    let y = 8;
    for (let row = 0; row < rows; row++) {
      const rowBoxes = boxes.slice(row * cols, (row + 1) * cols);
      const rowH = Math.max(...rowBoxes.map((b) => b.h));
      const rowW = rowBoxes.length * BOX_W + (rowBoxes.length - 1) * margin;
      let x = (stage.w - rowW) / 2;

      for (const box of rowBoxes) {
        grid.push({ box, x: x + BOX_W / 2, y: y + rowH / 2 });
        x += BOX_W + margin;
      }
      y += rowH + margin;
    }

    /* Center the rows vertically, unless they do not fit -- then they
     * stand on the top edge, which is the fuller half of the mistake. */
    const overflow = y - margin + 8 - stage.h;
    if (overflow < 0) {
      for (const a of grid) a.y -= overflow / 2;
    }

    anchors = grid;
  }

  /* Every seat onto the canvas, then a settle pass for whatever the
   * clamping pushed together. */
  const clamp = () => {
    for (const a of anchors) {
      a.x = Math.max(a.box.w / 2 + 8, Math.min(stage.w - a.box.w / 2 - 8, a.x));
      a.y = Math.max(a.box.h / 2 + 8, Math.min(stage.h - a.box.h / 2 - 8, a.y));
    }
  };

  clamp();
  relax(anchors, 12);
  clamp();

  return anchors;
}

/* ---------------------------------------------------------------- physics */

function physicsStep(dt, time) {
  const anchors = layoutAnchors();

  for (let i = 0; i < boxes.length; i++) {
    const box = boxes[i];
    const anchor = anchors[i];

    /* A box on its way home -- freshly spawned, thrown, or pushed aside
     * by another -- takes the direct line and the others step aside.
     * It keeps that priority until it arrives: two boxes that would
     * otherwise have to trade seats push against each other forever,
     * while a box that knows it has right of way simply walks the other
     * one out of the way. The grant is not time-limited -- expiring it
     * only turns a blocked box into a slow oscillation -- so on a canvas
     * genuinely too small to give every box its seat, the fleet simply
     * parks, overlapping and calm, until the window grows. */
    if (box.homing) {
      if (Math.abs(anchor.x - box.cx) < 12
          && Math.abs(anchor.y - box.cy) < 12) {
        box.homing = false;
      }
    } else if (Math.hypot(anchor.x - box.cx, anchor.y - box.cy) > 24) {
      box.homing = true;
      box.homingSince = time;
    }

    if (box.drag) {
      box.vx = box.drag.cx - box.cx;
      box.vy = box.drag.cy - box.cy;
      box.cx = box.drag.cx;
      box.cy = box.drag.cy;
      continue;
    }

    const pull = box.homing ? 0.02 : 0.006;
    box.vx += (anchor.x - box.cx) * pull * dt;
    box.vy += (anchor.y - box.cy) * pull * dt;

    /* The idle drift: a box nobody touches does not sit perfectly still. */
    box.vx += Math.cos(time * 0.5 + box.seed * 1.7) * 0.010 * dt;
    box.vy += Math.sin(time * 0.4 + box.seed) * 0.012 * dt;

    /* Soft walls: a thrown box bounces off the canvas edges, it does not
     * leave the page. */
    const marginX = box.w / 2 + 6, marginY = box.h / 2 + 6;
    if (box.cx < marginX) box.vx += (marginX - box.cx) * 0.02 * dt;
    if (box.cx > stage.w - marginX) box.vx -= (box.cx - stage.w + marginX) * 0.02 * dt;
    if (box.cy < marginY) box.vy += (marginY - box.cy) * 0.02 * dt;
    if (box.cy > stage.h - marginY) box.vy -= (box.cy - stage.h + marginY) * 0.02 * dt;

    /* Overlap is resolved positionally, on both axes at once, so a box
     * can always slide around the box in its way instead of bouncing
     * back off it. A held or homing box stays on its line and pushes
     * the others out. */
    for (let j = i + 1; j < boxes.length; j++) {
      const other = boxes[j];

      const needX = (box.w + other.w) / 2 + 14;
      const needY = (box.h + other.h) / 2 + 14;
      const dx = other.cx - box.cx, dy = other.cy - box.cy;
      const overlapX = needX - Math.abs(dx);
      const overlapY = needY - Math.abs(dy);

      if (overlapX > 0 && overlapY > 0) {
        const weightBox = (box.drag || box.homing) ? 0 : 1;
        const weightOther = (other.drag || other.homing) ? 0 : 1;
        const total = weightBox + weightOther;
        if (!total) continue;

        const ease = Math.min(1, dt) * 0.3;
        const dirX = dx >= 0 ? 1 : -1;
        const dirY = dy >= 0 ? 1 : -1;
        const pushX = dirX * overlapX * ease;
        const pushY = dirY * overlapY * ease;

        box.cx -= pushX * weightBox / total;
        box.cy -= pushY * weightBox / total;
        other.cx += pushX * weightOther / total;
        other.cy += pushY * weightOther / total;

        /* A nudge in the same direction, so the flow keeps some of it. */
        box.vx -= dirX * 0.3 * ease * weightBox / total;
        box.vy -= dirY * 0.3 * ease * weightBox / total;
        other.vx += dirX * 0.3 * ease * weightOther / total;
        other.vy += dirY * 0.3 * ease * weightOther / total;
      }
    }

    box.vx *= Math.pow(0.86, dt);
    box.vy *= Math.pow(0.86, dt);
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

    ctx.beginPath();
    ctx.moveTo(bx, by);
    const mx = (bx + box.cx) / 2, my = (by + box.cy) / 2;
    const dx = box.cx - bx, dy = box.cy - by;
    const len = Math.hypot(dx, dy) || 1;
    ctx.quadraticCurveTo(mx - dy / len * 30, my + dx / len * 30,
                         box.cx, box.cy);
    ctx.strokeStyle = "rgba(153, 194, 255, " + (0.05 + box.activity * 0.22)
                      + ")";
    ctx.lineWidth = 1;
    ctx.stroke();
  }

  /* -- particles ---------------------------------------------------- */

  const dt = Math.min(0.05, (time - frameTime) / 1000) * 60;
  frameTime = time;

  for (let i = particles.length - 1; i >= 0; i--) {
    const p = particles[i];
    p.t += dt / 60 / p.dur;

    if (p.t >= 1 || !boxes.includes(p.box)) {
      if (p.box) p.box.activity = 1;
      particles.splice(i, 1);
      continue;
    }

    const target = p.box;
    const mx = (bx + target.cx) / 2, my = (by + target.cy) / 2;
    const dx = target.cx - bx, dy = target.cy - by;
    const len = Math.hypot(dx, dy) || 1;
    const cx1 = mx - dy / len * p.off, cy1 = my + dx / len * p.off;

    for (let trail = 0; trail < 3; trail++) {
      const t = Math.max(0, p.t - trail * 0.045);
      const u = 1 - t;
      const px = u * u * bx + 2 * u * t * cx1 + t * t * target.cx;
      const py = u * u * by + 2 * u * t * cy1 + t * t * target.cy;
      ctx.beginPath();
      ctx.arc(px, py, 2.4 - trail * 0.7, 0, Math.PI * 2);
      ctx.fillStyle = "rgba(153, 194, 255," + (0.9 - trail * 0.3) + ")";
      ctx.fill();
    }
  }

  /* -- broker node --------------------------------------------------- */

  drawBroker(bx, by);

  /* -- device boxes --------------------------------------------------- */

  for (const box of boxes) drawBox(box, time);

  /* -- physics and repeat ---------------------------------------------- */

  applyDrags();
  physicsStep(Math.min(2.5, dt), time / 1000);
  requestAnimationFrame(draw);
}

function drawBroker(bx, by) {
  const b = state.broker;
  const label = b.connected ? b.host + ":" + b.port : "no broker";
  const sub = b.connected
    ? "in " + b.messages_in + "  ·  out " + b.messages_out
    : "connection off";

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
    text("waiting for " + (state.settings.base_topic || "oheztouch")
         + "/#  —  nothing has published yet",
         bx, by + nh / 2 + 34, SANS(13), PALETTE.dim, "center");
  }
}

function drawBox(box, time) {
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
    const parts = [["ui/night", "NIGHT"], ["ui/backlight", "BACKLIGHT"],
                   ["ui/activity", "ACTIVE"]];

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
      const label = "B " + brightness;
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
  text("seen " + (dev.last_age != null ? fmtAge(dev.last_age) : "-") + " ago",
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
      } else {
        openDetail(drag.box.host);
      }
    } else {
      /* A box that was moved returns home with right of way, so it does
       * not jam against the boxes it was thrown at. */
      drag.box.homing = true;
      drag.box.homingSince = performance.now() / 1000;
    }
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

/* -------------------------------------------------------------- broker chip */

function renderBrokerChip() {
  const b = state.broker;
  $("broker-dot").className = "dot" + (b.connected ? " on" : "");
  $("broker-label").textContent = b.connected
    ? b.host + ":" + b.port + (state.settings.base_topic
                              ? "  ·  " + state.settings.base_topic + "/#"
                              : "")
    : "broker not connected";
}

/* ------------------------------------------------------------ detail panel */

let detailHost = null;

function openDetail(host) {
  detailHost = host;
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

function detailDev() {
  return detailHost ? state.byHost[detailHost] : null;
}

function renderDetail() {
  const dev = detailDev();
  if (!dev) return closeDetail();

  $("detail-title").textContent = dev.host;

  const target = topicValue(dev, "system/target") || "?";
  const version = topicValue(dev, "system/version") || "?";
  $("detail-status").innerHTML =
    (dev.online
      ? '<b style="color:var(--good)">online</b>'
      : '<b style="color:var(--bad)">offline</b>')
    + " · " + esc(target) + " · v" + esc(version)
    + " · first seen " + esc(dev.first_seen || "-")
    + " · last message " + (dev.last_age != null ? fmtAge(dev.last_age)
                                                 : "-") + " ago";

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
      button.textContent = "relay " + suffix.split("/")[1]
        + (isOn(topicValue(dev, suffix)) ? "  ON" : "  OFF");
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

function closeDetail() {
  detailHost = null;
  $("detail").classList.add("hidden");
}

$("detail-close").addEventListener("click", closeDetail);
$("detail-sound-play").addEventListener("click", () => {
  if (!detailHost) return;
  publish({ host: detailHost }, "sound/set", $("detail-sound").value);
});
$("detail-delete").addEventListener("click", async () => {
  if (!detailHost) return;
  try {
    await api("/api/device/" + encodeURIComponent(detailHost) + "/delete");
    closeDetail();
  } catch (error) {
    toast(error.message, "err");
  }
});

/* --------------------------------------------------------------- settings */

function openSettings() {
  const s = state.settings;
  $("set-enabled").checked = !!s.mqtt_enabled;
  $("set-host").value = s.mqtt_host || "";
  $("set-port").value = s.mqtt_port || 1883;
  $("set-user").value = s.mqtt_user || "";
  $("set-pass").value = s.mqtt_pass || "";
  $("set-base").value = s.base_topic || "oheztouch";
  $("set-verbose").checked = !!s.verbose;
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
      mqtt_host: $("set-host").value.trim(),
      mqtt_port: Number($("set-port").value),
      mqtt_user: $("set-user").value.trim(),
      mqtt_pass: $("set-pass").value,
      base_topic: $("set-base").value.trim(),
      verbose: $("set-verbose").checked,
    });
    closeSettings();
    toast("saved — reconnecting to the broker");
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
    () => toast("console copied"),
    () => toast("could not copy", "err"));
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

/* ------------------------------------------------------------------- init */

/* Debug hook: with #debug in the URL, the live box geometry is written
 * into the page every half second, where a headless dump can read it. */
if (location.hash === "#debug") {
  setInterval(() => {
    $("debug-geom").textContent = JSON.stringify(
      boxes.map((b) => ({ host: b.host, cx: Math.round(b.cx),
                         cy: Math.round(b.cy), w: b.w, h: b.h })));
  }, 500);
}

requestAnimationFrame(function tick(time) {
  frameTime = time;
  applyDrags();
  requestAnimationFrame(draw);
});

resize();
pollState();
pollConsole();
