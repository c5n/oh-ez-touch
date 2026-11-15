"use strict";

/* The fleet log page: a client of /api/fleet and /api/fleet/device
 * (devmgr/history.py). Plain DOM and SVG, no libraries. */

const LOW_BLOCK = 3072;      // largest free block below this: a starved heap
const WEAK_RSSI = -75;

const fleet = {
  hours: 24,
  sort: "state",
  dir: 1,
  selected: null,
  summary: null,
  detail: null,
  hoverT: null,
  ov: {
    view: "timeline",
    heapSource: "http",
    perf: "system/fps",
    data: null,
    cells: {},
  },
};

function $(id) { return document.getElementById(id); }

function esc(text) {
  return String(text ?? "").replace(/[&<>"']/g, c =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
}

async function api(path, options) {
  const response = await fetch(path, options);
  const body = await response.json();
  if (!response.ok) throw new Error(body.error || response.statusText);
  return body;
}

function fmtDuration(seconds) {
  if (seconds == null || isNaN(seconds)) return "–";
  seconds = Math.max(0, Math.round(seconds));
  const d = Math.floor(seconds / 86400), h = Math.floor(seconds % 86400 / 3600),
        m = Math.floor(seconds % 3600 / 60);
  if (d) return `${d}d ${h}h`;
  if (h) return `${h}h ${m}m`;
  if (m) return `${m}m`;
  return `${seconds}s`;
}

function fmtBytes(value) {
  if (value == null) return "–";
  return value >= 10240 ? `${(value / 1024).toFixed(1)} KB` : `${Math.round(value)} B`;
}

function fmtClock(t, withDate) {
  const d = new Date(t * 1000);
  const time = d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
  return withDate ? `${d.toLocaleDateString([], { month: "short", day: "numeric" })} ${time}` : time;
}

function apColor(index) {
  return index == null ? "var(--dim)" : `var(--ap-${index % 8})`;
}

function apLabel(index) {
  return index == null ? "–" : `AP ${index + 1}`;
}

/* ------------------------------------------------------------- health */

/* One verdict per panel, worst first: what the status column says, and
 * what the default sort orders by. */
function health(d, now) {
  const quiet = d.last_seen ? now - d.last_seen : Infinity;
  if (d.status === "offline" && d.http_ok === false)
    return { rank: 0, cls: "critical", text: "Offline" };
  if (quiet > 300 && d.http_ok !== true)
    return { rank: 0, cls: "critical", text: "Silent" };
  if (d.http_ok === false)
    return { rank: 1, cls: "serious", text: d.ping_ok ? "No HTTP" : "No ping" };
  if (d.status === "offline")
    return { rank: 1, cls: "serious", text: "MQTT down" };
  if (d.heapblock != null && d.heapblock < LOW_BLOCK)
    return { rank: 2, cls: "serious", text: "Low heap" };
  if (d.rssi != null && d.rssi < WEAK_RSSI)
    return { rank: 3, cls: "warn", text: "Weak signal" };
  if (d.reboots > 0)
    return { rank: 3, cls: "warn", text: "Restarted" };
  return { rank: 4, cls: "good", text: "OK" };
}

/* -------------------------------------------------------------- tiles */

function renderStats(s) {
  const now = s.now;
  const devs = s.devices.filter(d => now - (d.last_seen || 0) < 86400);
  const verdicts = devs.map(d => health(d, now));
  const online = devs.filter(d => d.status === "online").length;
  const http = devs.filter(d => d.http_ok === true).length;
  const starved = devs.filter(d => d.heapblock != null && d.heapblock < LOW_BLOCK).length;
  const restarts = devs.reduce((n, d) => n + (d.reboots || 0), 0);
  const offline = devs.reduce((n, d) => n + (d.offline_events || 0), 0);
  const problems = verdicts.filter(v => v.rank <= 2).length;
  const range = fleet.hours < 24 ? `${fleet.hours} h` : `${fleet.hours / 24} d`;

  const tile = (label, value, sub) =>
    `<div class="stat"><div class="label">${label}</div><div class="value">${value}</div><div class="sub">${sub}</div></div>`;

  $("fleet-stats").innerHTML =
    tile("Panels", devs.length, "seen in the last 24 h") +
    tile("MQTT online", `${online}/${devs.length}`, "last-will status") +
    tile("HTTP answers", `${http}/${devs.length}`, "last check") +
    tile("Need attention", problems, "offline, unreachable or low heap") +
    tile("Low heap", starved, `largest block &lt; ${LOW_BLOCK} B`) +
    tile("Restarts", restarts, `in the last ${range}`) +
    tile("Offline events", offline, `MQTT, last ${range}`);
}

/* -------------------------------------------------------------- table */

function sparkline(points, t0, t1) {
  const w = 90, h = 22;
  if (!points || points.length < 2) return `<svg class="spark" width="${w}" height="${h}"></svg>`;
  t0 = Math.max(t0, points[0][0]);
  const values = points.map(p => p[1]);
  const lo = Math.min(...values, LOW_BLOCK * 0.8), hi = Math.max(...values, LOW_BLOCK * 1.5);
  const x = t => (t - t0) / (t1 - t0) * (w - 2) + 1;
  const y = v => h - 2 - (v - lo) / (hi - lo || 1) * (h - 4);
  const d = points.map((p, i) => `${i ? "L" : "M"}${x(p[0]).toFixed(1)},${y(p[1]).toFixed(1)}`).join("");
  const warnY = y(LOW_BLOCK).toFixed(1);
  return `<svg class="spark" width="${w}" height="${h}" aria-label="largest block trend">
    <line x1="0" x2="${w}" y1="${warnY}" y2="${warnY}" stroke="var(--st-serious)" stroke-width="1" stroke-dasharray="2 2" opacity=".6"/>
    <path d="${d}" fill="none" stroke="var(--s-2)" stroke-width="1.5"/></svg>`;
}

function sortValue(d, key, now) {
  if (key === "state") return health(d, now).rank * 1e6 + (d.heapblock ?? 1e5);
  if (key === "ap") return d.ap ?? 99;
  const v = d[key];
  return v == null ? (typeof v === "string" ? "" : -Infinity) : v;
}

function renderTable(s) {
  const now = s.now;
  const rows = [...s.devices].sort((a, b) => {
    const va = sortValue(a, fleet.sort, now), vb = sortValue(b, fleet.sort, now);
    if (va < vb) return -fleet.dir;
    if (va > vb) return fleet.dir;
    return a.dev.localeCompare(b.dev);
  });

  $("fleet-empty").classList.toggle("hidden", rows.length > 0);
  const t1 = now, t0 = now - fleet.hours * 3600;

  $("fleet-rows").innerHTML = rows.map(d => {
    const v = health(d, now);
    const stale = now - (d.last_seen || 0) > 3600;
    const blockCls = d.heapblock == null ? "" : d.heapblock < LOW_BLOCK ? "lvl-critical"
                   : d.heapblock < LOW_BLOCK * 1.6 ? "lvl-warn" : "";
    const rssiCls = d.rssi == null ? "" : d.rssi < WEAK_RSSI ? "lvl-critical" : d.rssi < -67 ? "lvl-warn" : "";
    const uptime = d.uptime != null ? d.uptime + (now - d.uptime_t) : null;
    const http = d.http_rate == null ? "–"
               : `${Math.round(d.http_rate * 100)}%${d.http_ms != null ? ` · ${d.http_ms} ms` : ""}`;
    const reason = d.reason ? `${d.reason_name || "?"} (${d.reason})` : "–";
    const ap = d.ap == null ? "–"
             : `<span class="ap-chip"><i style="background:${apColor(d.ap)}"></i>${apLabel(d.ap)} · ch ${d.channel ?? "?"}</span>`;
    return `<tr data-dev="${esc(d.dev)}" class="${stale ? "stale" : ""} ${fleet.selected === d.dev ? "selected" : ""}">
      <td><span class="pill ${v.cls}">${v.text}</span></td>
      <td>${esc(d.dev)}</td>
      <td>${esc(d.target || "–")}</td>
      <td title="${esc(d.build)}">${esc(d.version || "")} ${esc(d.git || "")}</td>
      <td class="num">${fmtDuration(uptime)}</td>
      <td class="num ${d.reboots ? "lvl-warn" : ""}">${d.reboots}</td>
      <td class="num">${fmtBytes(d.heap)}</td>
      <td class="num ${blockCls}" title="lowest in range: ${fmtBytes(d.heapblock_min)}">${fmtBytes(d.heapblock)}</td>
      <td>${sparkline(d.spark, t0, t1)}</td>
      <td class="num ${rssiCls}">${d.rssi != null ? `${d.rssi} dBm` : "–"}</td>
      <td title="${esc(d.bssid)}">${ap}</td>
      <td class="num">${d.disconnects ?? "–"}</td>
      <td>${esc(reason)}</td>
      <td class="num">${d.roams ?? "–"}</td>
      <td class="num ${d.offline_events ? "lvl-warn" : ""}">${d.offline_events}</td>
      <td class="num ${d.http_ok === false ? "lvl-critical" : ""}">${http}</td>
      <td class="num">${d.last_seen ? fmtDuration(now - d.last_seen) + " ago" : "–"}</td>
    </tr>`;
  }).join("");

  document.querySelectorAll("#fleet th.sortable").forEach(th => {
    th.classList.toggle("sorted-asc", th.dataset.sort === fleet.sort && fleet.dir > 0);
    th.classList.toggle("sorted-desc", th.dataset.sort === fleet.sort && fleet.dir < 0);
  });
}

function renderRecording(s) {
  const r = s.recording;
  const el = $("rec-state");
  if (!r.mqtt_host) {
    el.innerHTML = `<span class="pill none">Not recording</span>`;
  } else {
    el.innerHTML = r.mqtt_connected
      ? `<span class="pill good">Recording</span> <span class="dim">${esc(r.mqtt_host)}</span>`
      : `<span class="pill critical">Broker unreachable</span> <span class="dim">${esc(r.mqtt_host)}</span>`;
  }
  $("rec-dir").textContent = `Files: ${r.dir}`;
}

/* ------------------------------------------------------------- charts */

function niceTicks(lo, hi, count) {
  const span = hi - lo || 1;
  const step0 = span / count;
  const mag = Math.pow(10, Math.floor(Math.log10(step0)));
  const step = [1, 2, 5, 10].map(m => m * mag).find(s => span / s <= count) || 10 * mag;
  const ticks = [];
  for (let v = Math.ceil(lo / step) * step; v <= hi + 1e-9; v += step) ticks.push(v);
  return ticks;
}

function timeTicks(t0, t1, width) {
  const steps = [60, 300, 600, 1800, 3600, 7200, 10800, 21600, 43200, 86400];
  const want = Math.max(2, Math.floor(width / 110));
  const step = steps.find(s => (t1 - t0) / s <= want) || 86400;
  const offset = new Date().getTimezoneOffset() * 60;
  const ticks = [];
  for (let t = Math.ceil((t0 - offset) / step) * step + offset; t <= t1; t += step) ticks.push(t);
  return { ticks, step };
}

const PAD = { l: 56, r: 12, t: 8, b: 20 };

/* A line chart: one measure, one axis. series: [{name, color, points}] */
function lineChart(opts) {
  const { width, height, t0, t1, series, unit, fmt, reboots, floor } = opts;
  const all = series.flatMap(s => s.points.map(p => p[1]));
  let lo = all.length ? Math.min(...all) : 0, hi = all.length ? Math.max(...all) : 1;
  if (floor != null) lo = Math.min(lo, floor);
  // A shared scale, so that small multiples compare at a glance.
  if (opts.lo != null) lo = opts.lo;
  if (opts.hi != null) hi = opts.hi;
  if (lo === hi) { lo -= 1; hi += 1; }
  const yt = niceTicks(lo, hi, 4);
  lo = Math.min(lo, yt[0]); hi = Math.max(hi, yt[yt.length - 1]);
  const iw = width - PAD.l - PAD.r, ih = height - PAD.t - PAD.b;
  const x = t => PAD.l + (t - t0) / (t1 - t0) * iw;
  const y = v => PAD.t + ih - (v - lo) / (hi - lo) * ih;
  const { ticks } = timeTicks(t0, t1, iw);

  let svg = `<svg viewBox="0 0 ${width} ${height}" height="${height}" data-t0="${t0}" data-t1="${t1}" data-pl="${PAD.l}" data-iw="${iw}">`;
  for (const v of yt)
    svg += `<line class="grid" x1="${PAD.l}" x2="${width - PAD.r}" y1="${y(v)}" y2="${y(v)}"/>` +
           `<text x="${PAD.l - 6}" y="${y(v) + 4}" text-anchor="end">${fmt ? fmt(v) : v}${unit && v === yt[yt.length - 1] ? " " + unit : ""}</text>`;
  for (const t of ticks)
    svg += `<text x="${x(t)}" y="${height - 4}" text-anchor="middle">${fmtClock(t).slice(0, 5)}</text>`;
  for (const t of reboots || [])
    svg += `<line class="reboot" x1="${x(t)}" x2="${x(t)}" y1="${PAD.t}" y2="${PAD.t + ih}"><title>restart ${fmtClock(t, true)}</title></line>`;
  for (const m of opts.markers || [])
    svg += `<line class="${m.cls}" x1="${x(m.t)}" x2="${x(m.t)}" y1="${PAD.t}" y2="${PAD.t + ih}"><title>${esc(m.title)}</title></line>`;
  if (opts.hline != null && opts.hline >= lo && opts.hline <= hi)
    svg += `<line class="hline" x1="${PAD.l}" x2="${width - PAD.r}" y1="${y(opts.hline)}" y2="${y(opts.hline)}"/>`;
  for (const s of series) {
    // A gap longer than five samples' worth breaks the line: no data is
    // not a straight line between two readings.
    const gap = Math.max(300, (t1 - t0) / 100);
    let d = "", prev = null;
    for (const p of s.points) {
      d += `${prev == null || p[0] - prev > gap ? "M" : "L"}${x(p[0]).toFixed(1)},${y(p[1]).toFixed(1)}`;
      prev = p[0];
    }
    svg += `<path class="line" d="${d}" stroke="${s.color}"/>`;
  }
  svg += `<line class="cross hidden" x1="0" x2="0" y1="${PAD.t}" y2="${PAD.t + ih}"/>`;
  svg += `</svg>`;
  return svg;
}

/* A band of coloured segments over time. segments: [{t0, t1, color, label}] */
function bandChart(opts) {
  const { width, t0, t1, segments } = opts;
  const height = 24;
  const iw = width - PAD.l - PAD.r;
  const x = t => PAD.l + (Math.max(t0, Math.min(t1, t)) - t0) / (t1 - t0) * iw;
  let svg = `<svg viewBox="0 0 ${width} ${height}" height="${height}" data-t0="${t0}" data-t1="${t1}" data-pl="${PAD.l}" data-iw="${iw}">`;
  svg += `<rect x="${PAD.l}" y="3" width="${iw}" height="16" rx="3" fill="var(--panel-2)"/>`;
  for (const s of segments) {
    const a = x(s.t0), b = x(s.t1);
    if (b - a < 0.5) continue;
    svg += `<rect x="${a.toFixed(1)}" y="3" width="${Math.max(1, b - a - 1).toFixed(1)}" height="16" rx="2" fill="${s.color}"><title>${esc(s.label)} ${fmtClock(s.t0, true)} – ${fmtClock(s.t1, true)}</title></rect>`;
    // A label only where it fits: about 6.5 px per character at 11 px.
    if (b - a > String(s.label).length * 6.5 + 12)
      svg += `<text x="${a + 6}" y="15" style="fill:#fff">${esc(s.label)}</text>`;
  }
  svg += `<line class="cross hidden" x1="0" x2="0" y1="0" y2="${height}"/></svg>`;
  return svg;
}

/* Turn a change list [[t, value], ...] into segments up to t1. */
function toSegments(changes, t0, t1, style) {
  const out = [];
  for (let i = 0; i < changes.length; i++) {
    const [t, v] = changes[i];
    const end = i + 1 < changes.length ? changes[i + 1][0] : t1;
    const s = style(v);
    if (s) out.push({ t0: Math.max(t, t0), t1: end, ...s });
  }
  return out;
}

function nearest(points, t) {
  let best = null, bestDist = Infinity;
  for (const p of points) {
    const dist = Math.abs(p[0] - t);
    if (dist < bestDist) { best = p; bestDist = dist; }
  }
  return best;
}

function renderDetail() {
  const d = fleet.detail;
  if (!d) return;
  const box = $("charts");
  const width = Math.max(480, box.clientWidth || 900);
  const t1 = d.t1;
  // The axis starts at the first recorded value, not at the start of the
  // range: a log a few minutes old would otherwise be a sliver at the edge.
  const firsts = [
    ...Object.values(d.series).map(p => p.length ? p[0][0] : Infinity),
    ...Object.values(d.changes).map(p => p.length ? p[0][0] : Infinity),
    d.http.length ? d.http[0][0] : Infinity,
  ];
  const first = Math.min(...firsts);
  const t0 = isFinite(first) ? Math.max(d.t0, first - (t1 - first) * 0.02) : d.t0;
  const S = topic => d.series[topic] || [];
  const C = topic => d.changes[topic] || [];

  const charts = [];

  charts.push({
    title: "<strong>Signal</strong> RSSI",
    svg: lineChart({ width, height: 110, t0, t1, unit: "dBm", reboots: d.reboots,
      series: [{ name: "RSSI", color: "var(--s-1)", points: S("system/rssi"), fmt: v => `${Math.round(v)} dBm` }] }),
    series: [{ name: "RSSI", color: "var(--s-1)", points: S("system/rssi"), fmt: v => `${Math.round(v)} dBm` }],
  });

  const heapSeries = [
    { name: "Free", color: "var(--s-1)", points: S("system/heap"), fmt: fmtBytes },
    { name: "Largest block", color: "var(--s-2)", points: S("system/heapblock"), fmt: fmtBytes },
  ];
  charts.push({
    title: `<strong>Heap</strong><span class="legend"><span><i style="background:var(--s-1)"></i>Free</span><span><i style="background:var(--s-2)"></i>Largest block</span></span><span>dashed: restart</span>`,
    svg: lineChart({ width, height: 140, t0, t1, series: heapSeries, floor: 0, reboots: d.reboots,
      fmt: v => v >= 1024 ? `${Math.round(v / 1024)}K` : Math.round(v) }),
    series: heapSeries,
  });

  const upSeries = [{ name: "Uptime", color: "var(--s-1)", points: S("system/uptime"), fmt: fmtDuration }];
  charts.push({
    title: "<strong>Uptime</strong> a drop is a restart",
    svg: lineChart({ width, height: 90, t0, t1, series: upSeries, floor: 0,
      fmt: v => fmtDuration(v) }),
    series: upSeries,
  });

  const statusSegs = toSegments(C("status"), t0, t1, v =>
    v === "online" ? { color: "var(--st-good)", label: "online" }
                   : { color: "var(--st-critical)", label: v || "?" });
  charts.push({ title: "<strong>MQTT</strong> last-will status", svg: bandChart({ width, t0, t1, segments: statusSegs }) });

  const probe = (fleet.summary?.recording?.probe_s || 60) * 1.5;
  const httpSegs = d.http.map(([t, ok, ms, ping]) => ({
    t0: t, t1: Math.min(t1, t + probe),
    color: ok ? "var(--st-good)" : ping ? "var(--st-serious)" : "var(--st-critical)",
    label: ok ? `HTTP ${ms} ms` : ping ? "ping only" : "no answer",
  }));
  charts.push({
    title: `<strong>Reachability</strong><span class="legend"><span><i class="box" style="background:var(--st-good)"></i>HTTP answers</span><span><i class="box" style="background:var(--st-serious)"></i>ping only</span><span><i class="box" style="background:var(--st-critical)"></i>no answer</span></span>`,
    svg: bandChart({ width, t0, t1, segments: httpSegs }),
  });

  const apSegs = toSegments(C("system/bssid"), t0, t1, v => {
    const i = d.aps[v];
    return v ? { color: apColor(i), label: `${apLabel(i)} ${v}` } : null;
  });
  const apsUsed = [...new Set(C("system/bssid").map(c => c[1]).filter(Boolean))];
  charts.push({
    title: `<strong>Access point</strong><span class="legend">${apsUsed.map(b =>
      `<span><i class="box" style="background:${apColor(d.aps[b])}"></i>${apLabel(d.aps[b])} ${esc(b)}</span>`).join("")}</span>`,
    svg: bandChart({ width, t0, t1, segments: apSegs }),
  });

  box.innerHTML = charts.map((c, i) =>
    `<div class="chart" data-index="${i}"><div class="chart-title">${c.title}</div>${c.svg}</div>`).join("");
  fleet.chartSeries = charts.map(c => c.series || []);
  fleet.bands = { status: C("status"), bssid: C("system/bssid"), http: d.http };

  renderEvents(d);
  $("detail-status").textContent = d.http_status ? JSON.stringify(d.http_status, null, 2) : "no answer recorded";
}

function renderEvents(d) {
  const ev = [];
  for (const t of d.reboots) ev.push([t, "Restart"]);
  for (const [t, v] of d.changes["status"] || []) if (t >= d.t0) ev.push([t, `MQTT ${v}`]);
  const bssids = d.changes["system/bssid"] || [];
  for (let i = 1; i < bssids.length; i++)
    ev.push([bssids[i][0], `AP ${apLabel(d.aps[bssids[i - 1][1]])} → ${apLabel(d.aps[bssids[i][1]])} (${bssids[i][1]})`]);
  for (const [t, v] of d.changes["system/git"] || []) if (t >= d.t0) ev.push([t, `Firmware ${v.slice(0, 7)}`]);
  const disc = d.series["system/wlan_disconnects"] || [];
  const reasons = d.series["system/wlan_reason"] || [];
  for (let i = 1; i < disc.length; i++) {
    if (disc[i][1] > disc[i - 1][1]) {
      const r = nearest(reasons, disc[i][0]);
      const code = r ? Math.round(r[1]) : 0;
      ev.push([disc[i][0], `WLAN lost${code ? `: ${d.reasons[code] || "?"} (${code})` : ""}`]);
    }
  }
  const roams = d.series["system/wlan_roams"] || [];
  for (let i = 1; i < roams.length; i++)
    if (roams[i][1] > roams[i - 1][1]) ev.push([roams[i][0], "Roamed to a stronger AP"]);
  let failing = false;
  for (const [t, ok, , ping] of d.http) {
    if (!ok && !failing) ev.push([t, ping ? "HTTP stopped answering (ping still answers)" : "HTTP and ping stopped answering"]);
    if (ok && failing) ev.push([t, "HTTP answers again"]);
    failing = !ok;
  }
  ev.sort((a, b) => b[0] - a[0]);
  $("event-rows").innerHTML = ev.length
    ? ev.slice(0, 200).map(([t, text]) => `<tr><td class="when">${fmtClock(t, true)}</td><td>${esc(text)}</td></tr>`).join("")
    : `<tr><td class="dim">No events in this range.</td></tr>`;
}

/* One crosshair for all charts, and one tooltip with every value at t. */
function onChartMove(event) {
  const svg = event.target.closest("svg");
  if (!svg || !fleet.detail) return hideTooltip();
  const rect = svg.getBoundingClientRect();
  const scale = rect.width / svg.viewBox.baseVal.width;
  const pl = +svg.dataset.pl, iw = +svg.dataset.iw;
  const xIn = (event.clientX - rect.left) / scale;
  if (xIn < pl || xIn > pl + iw) return hideTooltip();
  const t0 = +svg.dataset.t0, t1 = +svg.dataset.t1;
  const t = t0 + (xIn - pl) / iw * (t1 - t0);

  document.querySelectorAll("#charts svg").forEach(s => {
    const cross = s.querySelector(".cross");
    const x = +s.dataset.pl + (t - t0) / (t1 - t0) * +s.dataset.iw;
    cross.setAttribute("x1", x); cross.setAttribute("x2", x);
    cross.classList.remove("hidden");
  });

  const lines = [];
  for (const series of fleet.chartSeries)
    for (const s of series) {
      const p = nearest(s.points, t);
      if (p && Math.abs(p[0] - t) < Math.max(180, (t1 - t0) / 150))
        lines.push(`<div><i style="background:${s.color}"></i>${s.name}: <b>${s.fmt ? s.fmt(p[1]) : p[1]}</b></div>`);
    }
  const at = (changes) => { let v = null; for (const c of changes) { if (c[0] <= t) v = c[1]; else break; } return v; };
  const st = at(fleet.bands.status);
  if (st) lines.push(`<div>MQTT: <b>${esc(st)}</b></div>`);
  const ap = at(fleet.bands.bssid);
  if (ap) lines.push(`<div>AP: <b>${apLabel(fleet.detail.aps[ap])}</b> ${esc(ap)}</div>`);
  const h = nearest(fleet.bands.http, t);
  if (h && Math.abs(h[0] - t) < (fleet.summary?.recording?.probe_s || 60))
    lines.push(`<div>HTTP: <b>${h[1] ? `${h[2]} ms` : h[3] ? "ping only" : "no answer"}</b></div>`);

  const tip = $("tooltip");
  tip.innerHTML = `<div class="t">${fmtClock(t, true)}</div>${lines.join("") || "<div class='dim'>no data</div>"}`;
  tip.classList.remove("hidden");
  const x = Math.min(window.innerWidth - tip.offsetWidth - 12, event.clientX + 14);
  tip.style.left = `${x}px`;
  tip.style.top = `${event.clientY + 14}px`;
}

function hideTooltip() {
  $("tooltip").classList.add("hidden");
  document.querySelectorAll("#charts .cross").forEach(c => c.classList.add("hidden"));
}

/* ----------------------------------------------------------- overview */

/* The time axis of the overview starts at the first recorded value of any
 * panel, as the detail charts do. */
function overviewRange(data) {
  let first = Infinity;
  for (const d of data.devices) {
    for (const p of Object.values(d.series)) if (p.length) first = Math.min(first, p[0][0]);
    for (const c of Object.values(d.changes)) if (c.length) first = Math.min(first, c[0][0]);
    if (d.http.length) first = Math.min(first, d.http[0][0]);
  }
  const t1 = data.t1;
  const t0 = isFinite(first) ? Math.max(data.t0, first - (t1 - first) * 0.02) : data.t0;
  return { t0, t1 };
}

/* One row per panel on one time axis: MQTT status as the bar, failed HTTP
 * checks over it, and markers for restarts, firmware changes, WLAN losses
 * and roams. */
function timelineChart(data, width) {
  const { t0, t1 } = overviewRange(data);
  const rowH = 24, top = 4, labelW = 190, axisH = 18;
  const devs = data.devices;
  const height = top + devs.length * rowH + axisH;
  const iw = width - labelW - PAD.r;
  const x = t => labelW + (Math.max(t0, Math.min(t1, t)) - t0) / (t1 - t0) * iw;
  const { ticks } = timeTicks(t0, t1, iw);

  let svg = `<svg class="ov-timeline" viewBox="0 0 ${width} ${height}" height="${height}" data-t0="${t0}" data-t1="${t1}" data-pl="${labelW}" data-iw="${iw}">`;
  for (const t of ticks)
    svg += `<line class="grid" x1="${x(t)}" x2="${x(t)}" y1="${top}" y2="${height - axisH}"/>` +
           `<text x="${x(t)}" y="${height - 4}" text-anchor="middle">${fmtClock(t).slice(0, 5)}</text>`;

  devs.forEach((d, i) => {
    const y = top + i * rowH, barY = y + 6, barH = 12, mid = barY + barH / 2;
    svg += `<g class="row" data-dev="${esc(d.dev)}">`;
    svg += `<text class="row-label" x="0" y="${mid + 4}">${esc(d.dev)}</text>`;
    svg += `<rect class="row-bg" x="${labelW}" y="${barY}" width="${iw}" height="${barH}" rx="2"><title>no data</title></rect>`;

    const status = toSegments(d.changes["status"] || [], t0, t1, v =>
      v === "online" ? { color: "var(--st-good)", label: "MQTT online" }
                     : { color: "var(--st-critical)", label: `MQTT ${v || "?"}` });
    for (const sg of status) {
      const a = x(sg.t0), b = x(sg.t1);
      if (b - a < 0.3) continue;
      svg += `<rect x="${a.toFixed(1)}" y="${barY}" width="${Math.max(0.6, b - a).toFixed(1)}" height="${barH}" fill="${sg.color}" opacity=".75"><title>${esc(sg.label)} ${fmtClock(sg.t0, true)} – ${fmtClock(sg.t1, true)}</title></rect>`;
    }
    for (const [a0, b0, state] of d.http) {
      if (state === "ok") continue;
      const a = x(a0), b = x(b0);
      const color = state === "ping" ? "var(--st-serious)" : "var(--st-critical)";
      const label = state === "ping" ? "HTTP silent, ping answers" : "no HTTP, no ping";
      svg += `<rect x="${a.toFixed(1)}" y="${barY + 3}" width="${Math.max(1.5, b - a).toFixed(1)}" height="${barH - 6}" fill="${color}"><title>${label} ${fmtClock(a0, true)} – ${fmtClock(b0, true)}</title></rect>`;
    }
    for (const t of d.reboots)
      svg += `<line class="tl-reboot" x1="${x(t)}" x2="${x(t)}" y1="${y + 2}" y2="${y + rowH - 2}"><title>restart ${fmtClock(t, true)}</title></line>`;
    const git = d.changes["system/git"] || [];
    for (let k = 1; k < git.length; k++) {
      if (git[k][0] < t0) continue;
      const gx = x(git[k][0]);
      svg += `<path class="tl-fw" d="M${gx},${y + 1} l4,4 l-4,4 l-4,-4 z"><title>firmware ${esc(git[k][1].slice(0, 7))} at ${fmtClock(git[k][0], true)}</title></path>`;
    }
    for (const [t, code] of d.wlan_lost)
      svg += `<circle class="tl-lost" cx="${x(t)}" cy="${barY + barH + 3}" r="3"><title>WLAN lost ${fmtClock(t, true)}${code ? `: ${esc(data.reasons[code] || "?")} (${code})` : ""}</title></circle>`;
    for (const t of d.roams)
      svg += `<circle class="tl-roam" cx="${x(t)}" cy="${barY - 3}" r="2.5"><title>roamed ${fmtClock(t, true)}</title></circle>`;
    svg += `</g>`;
  });
  svg += `<line class="cross hidden" x1="0" x2="0" y1="${top}" y2="${height - axisH}"/></svg>`;
  return svg;
}

const HEAP_SOURCES = {
  http: { label: "HTTP /api/status", free: "http/free_heap", block: "http/largest_block" },
  mqtt: { label: "MQTT", free: "system/heap", block: "system/heapblock" },
};

const PERF_METRICS = {
  "system/fps": { label: "fps", fmt: v => v.toFixed(1), scale: 1, unit: "fps" },
  "system/render_us": { label: "render", fmt: v => `${v.toFixed(1)} ms`, scale: 1 / 1000, unit: "ms" },
  "system/wait_us": { label: "wait", fmt: v => `${v.toFixed(1)} ms`, scale: 1 / 1000, unit: "ms" },
};

/* The series each small multiple draws, for the selected view. */
function cellSeries(d) {
  const ov = fleet.ov;
  if (ov.view === "heap") {
    const src = HEAP_SOURCES[ov.heapSource];
    return [
      { name: "Free", color: "var(--s-1)", points: d.series[src.free] || [], fmt: fmtBytes },
      { name: "Largest block", color: "var(--s-2)", points: d.series[src.block] || [], fmt: fmtBytes },
    ];
  }
  if (ov.view === "wlan")
    return [{ name: "RSSI", color: "var(--s-1)", points: d.series["system/rssi"] || [], fmt: v => `${Math.round(v)} dBm` }];
  const m = PERF_METRICS[ov.perf];
  return [{ name: m.label, color: "var(--s-1)",
            points: (d.series[ov.perf] || []).map(p => [p[0], p[1] * m.scale]), fmt: m.fmt }];
}

/* One small chart per panel, all on one scale. */
function smallMultiples(data, width) {
  const { t0, t1 } = overviewRange(data);
  const ov = fleet.ov;
  const cols = width >= 1300 ? 4 : width >= 950 ? 3 : width >= 560 ? 2 : 1;
  const gap = 14;
  const cw = Math.floor((width - gap * (cols - 1)) / cols) - 8;
  const perDev = data.devices.map(d => ({ d, series: cellSeries(d) }));
  const values = perDev.flatMap(c => c.series.flatMap(s => s.points.map(p => p[1])));
  let lo = values.length ? Math.min(...values) : 0, hi = values.length ? Math.max(...values) : 1;
  if (ov.view === "heap" || ov.view === "perf") lo = 0;
  if (lo === hi) hi = lo + 1;

  const fmtAxis = ov.view === "heap" ? (v => v >= 1024 ? `${Math.round(v / 1024)}K` : Math.round(v))
                : ov.view === "wlan" ? (v => Math.round(v))
                : (v => +v.toFixed(1));
  fleet.ov.cells = {};

  const cells = perDev.map(({ d, series }) => {
    fleet.ov.cells[d.dev] = series;
    const markers = ov.view === "wlan"
      ? [...d.wlan_lost.map(([t, code]) => ({ t, cls: "mark-lost",
             title: `WLAN lost ${fmtClock(t, true)}${code ? `: ${data.reasons[code] || "?"} (${code})` : ""}` })),
         ...d.roams.map(t => ({ t, cls: "mark-roam", title: `roamed ${fmtClock(t, true)}` }))]
      : [];
    let svg = lineChart({ width: cw, height: 110, t0, t1, series, lo, hi, fmt: fmtAxis,
                          reboots: d.reboots, markers,
                          hline: ov.view === "heap" ? LOW_BLOCK : null });
    if (ov.view === "wlan") {
      const apSegs = toSegments(d.changes["system/bssid"] || [], t0, t1, v => {
        const i = data.aps[v];
        return v ? { color: apColor(i), label: `${apLabel(i)} ${v}` } : null;
      });
      svg += bandChart({ width: cw, t0, t1, segments: apSegs });
    }

    // The latest value, and for the heap the lowest largest block in range.
    const main = series[series.length - 1];
    const lastP = main.points[main.points.length - 1];
    let note = lastP ? main.fmt(lastP[1]) : "no data";
    let bad = false;
    if (ov.view === "heap" && main.points.length) {
      const min = Math.min(...main.points.map(p => p[1]));
      note = `now ${main.fmt(lastP[1])} · min ${main.fmt(min)}`;
      bad = min < LOW_BLOCK;
    }
    if (ov.view === "wlan") note += ` · lost ${d.wlan_lost.length} · roams ${d.roams.length}`;
    return `<div class="ov-cell" data-dev="${esc(d.dev)}">
      <div class="ov-cell-title"><b title="${esc(d.target)} ${esc(d.git)}">${esc(d.dev)}</b><span class="${bad ? "bad" : ""}">${esc(note)}</span></div>
      ${svg}</div>`;
  });
  return `<div class="ov-grid" style="grid-template-columns: repeat(${cols}, minmax(0, 1fr))">${cells.join("")}</div>`;
}

function renderOverviewControls() {
  const ov = fleet.ov;
  document.querySelectorAll("#ov-tabs button").forEach(b =>
    b.classList.toggle("active", b.dataset.view === ov.view));
  const metric = $("ov-metric");
  if (ov.view === "heap") {
    metric.innerHTML = Object.entries(HEAP_SOURCES).map(([k, v]) =>
      `<button data-heap="${k}" class="${k === ov.heapSource ? "active" : ""}">${v.label}</button>`).join("");
  } else if (ov.view === "perf") {
    metric.innerHTML = Object.entries(PERF_METRICS).map(([k, v]) =>
      `<button data-perf="${k}" class="${k === ov.perf ? "active" : ""}">${v.label}</button>`).join("");
  } else {
    metric.innerHTML = "";
  }
  metric.classList.toggle("hidden", !metric.innerHTML);

  const legends = {
    timeline: `<span><i class="box" style="background:var(--st-good)"></i>MQTT online</span>
      <span><i class="box" style="background:var(--st-critical)"></i>offline / no answer</span>
      <span><i class="box" style="background:var(--st-serious)"></i>HTTP silent, ping answers</span>
      <span><i class="tick" style="background:var(--st-critical)"></i>restart</span>
      <span><i class="box" style="background:var(--accent);transform:rotate(45deg)"></i>firmware</span>
      <span><i class="dot" style="background:var(--st-warn)"></i>WLAN lost</span>
      <span><i class="dot" style="border:1.5px solid var(--ap-6)"></i>roam</span>`,
    heap: `<span><i style="background:var(--s-1)"></i>Free</span><span><i style="background:var(--s-2)"></i>Largest block</span>
      <span><i style="background:var(--st-critical)"></i>${LOW_BLOCK} B</span><span>dashed: restart</span>
      ${ov.heapSource === "mqtt" ? "<span>MQTT block is sampled during the publish and reads low</span>" : ""}`,
    wlan: `<span><i style="background:var(--s-1)"></i>RSSI</span><span><i class="tick" style="background:var(--st-warn)"></i>WLAN lost</span>
      <span><i class="tick" style="background:var(--ap-6)"></i>roam</span><span>band: access point</span>`,
    perf: `<span>${PERF_METRICS[ov.perf].label} per panel, one scale</span><span>dashed: restart</span>`,
  };
  $("ov-legend").innerHTML = legends[ov.view];
}

function renderOverview() {
  const data = fleet.ov.data;
  renderOverviewControls();
  if (!data) return;
  const body = $("ov-body");
  if (!data.devices.length) {
    body.innerHTML = `<p class="dim">Nothing recorded in this range.</p>`;
    return;
  }
  const width = Math.max(480, body.clientWidth || 1000);
  body.innerHTML = fleet.ov.view === "timeline" ? timelineChart(data, width) : smallMultiples(data, width);
}

/* One crosshair over every overview chart, and the hovered panel's values. */
function onOverviewMove(event) {
  const svg = event.target.closest("svg");
  if (!svg || !fleet.ov.data) return hideOverviewCross();
  const rect = svg.getBoundingClientRect();
  const scale = rect.width / svg.viewBox.baseVal.width;
  const pl = +svg.dataset.pl, iw = +svg.dataset.iw;
  const xIn = (event.clientX - rect.left) / scale;
  if (xIn < pl || xIn > pl + iw) return hideOverviewCross();
  const t0 = +svg.dataset.t0, t1 = +svg.dataset.t1;
  const t = t0 + (xIn - pl) / iw * (t1 - t0);

  document.querySelectorAll("#ov-body svg").forEach(s => {
    const cross = s.querySelector(".cross");
    if (!cross) return;
    const x = +s.dataset.pl + (t - +s.dataset.t0) / (+s.dataset.t1 - +s.dataset.t0) * +s.dataset.iw;
    cross.setAttribute("x1", x); cross.setAttribute("x2", x);
    cross.classList.remove("hidden");
  });

  if (fleet.ov.view === "timeline") return;   // the bars carry their own titles
  const cell = event.target.closest(".ov-cell");
  const series = cell ? fleet.ov.cells[cell.dataset.dev] || [] : [];
  const lines = [];
  for (const s of series) {
    const p = nearest(s.points, t);
    if (p && Math.abs(p[0] - t) < Math.max(180, (t1 - t0) / 120))
      lines.push(`<div><i style="background:${s.color}"></i>${s.name}: <b>${s.fmt(p[1])}</b></div>`);
  }
  const tip = $("tooltip");
  tip.innerHTML = `<div class="t">${esc(cell ? cell.dataset.dev : "")} · ${fmtClock(t, true)}</div>${lines.join("") || "<div class='dim'>no data</div>"}`;
  tip.classList.remove("hidden");
  tip.style.left = `${Math.min(window.innerWidth - tip.offsetWidth - 12, event.clientX + 14)}px`;
  tip.style.top = `${event.clientY + 14}px`;
}

function hideOverviewCross() {
  $("tooltip").classList.add("hidden");
  document.querySelectorAll("#ov-body .cross").forEach(c => c.classList.add("hidden"));
}

async function loadOverview() {
  try {
    fleet.ov.data = await api(`/api/fleet/overview?hours=${fleet.hours}`);
    renderOverview();
  } catch (error) {
    $("ov-body").innerHTML = `<p class="dim">Could not load the overview: ${esc(error.message)}</p>`;
  }
}

/* ------------------------------------------------------------ loading */

async function loadSummary() {
  try {
    fleet.summary = await api(`/api/fleet?hours=${fleet.hours}`);
    renderStats(fleet.summary);
    renderTable(fleet.summary);
    renderRecording(fleet.summary);
  } catch (error) {
    $("rec-state").innerHTML = `<span class="pill critical">devmgr unreachable</span>`;
  }
}

async function loadDetail() {
  if (!fleet.selected) return;
  try {
    fleet.detail = await api(`/api/fleet/device?dev=${encodeURIComponent(fleet.selected)}&hours=${fleet.hours}`);
    const row = fleet.summary?.devices.find(d => d.dev === fleet.selected);
    $("detail-title").textContent = fleet.selected;
    $("detail-sub").textContent = row ? `${row.target || ""} · ${row.ip || ""} · ${row.version || ""} ${row.git || ""}` : "";
    $("detail").classList.remove("hidden");
    renderDetail();
  } catch (error) {
    $("detail-sub").textContent = `could not load: ${error.message}`;
  }
}

function select(dev) {
  fleet.selected = dev;
  location.hash = dev ? `dev=${encodeURIComponent(dev)}` : "";
  if (fleet.summary) renderTable(fleet.summary);
  if (!dev) { $("detail").classList.add("hidden"); return; }
  loadDetail().then(() => $("detail").scrollIntoView({ behavior: "smooth", block: "start" }));
}

async function loadSettings() {
  const st = await api("/api/state");
  $("rec-host").value = st.settings.mqtt_host || "";
  $("rec-port").value = st.settings.mqtt_port || 1883;
  $("rec-prefix").value = st.settings.mqtt_prefix || "oheztouch";
  $("rec-probe").value = String(st.settings.history_probe_s || 60);
}

function wire() {
  $("range").addEventListener("click", e => {
    const b = e.target.closest("button");
    if (!b) return;
    fleet.hours = +b.dataset.hours;
    document.querySelectorAll("#range button").forEach(x => x.classList.toggle("active", x === b));
    loadSummary().then(loadDetail);
    loadOverview();
  });

  $("ov-tabs").addEventListener("click", e => {
    const b = e.target.closest("button");
    if (!b) return;
    fleet.ov.view = b.dataset.view;
    renderOverview();
  });
  $("ov-metric").addEventListener("click", e => {
    const b = e.target.closest("button");
    if (!b) return;
    if (b.dataset.heap) fleet.ov.heapSource = b.dataset.heap;
    if (b.dataset.perf) fleet.ov.perf = b.dataset.perf;
    renderOverview();
  });
  $("ov-body").addEventListener("click", e => {
    const target = e.target.closest("[data-dev]");
    if (target) select(target.dataset.dev);
  });
  $("ov-body").addEventListener("mousemove", onOverviewMove);
  $("ov-body").addEventListener("mouseleave", hideOverviewCross);

  document.querySelectorAll("#fleet th.sortable").forEach(th => th.addEventListener("click", () => {
    if (fleet.sort === th.dataset.sort) fleet.dir = -fleet.dir;
    else { fleet.sort = th.dataset.sort; fleet.dir = 1; }
    renderTable(fleet.summary);
  }));

  $("fleet-rows").addEventListener("click", e => {
    const tr = e.target.closest("tr");
    if (tr) select(tr.dataset.dev === fleet.selected ? null : tr.dataset.dev);
  });

  $("detail-close").addEventListener("click", () => select(null));
  $("charts").addEventListener("mousemove", onChartMove);
  $("charts").addEventListener("mouseleave", hideTooltip);

  $("rec-btn").addEventListener("click", () => {
    $("rec-panel").classList.toggle("hidden");
    if (!$("rec-panel").classList.contains("hidden")) loadSettings();
  });
  $("rec-save").addEventListener("click", async () => {
    await api("/api/settings", {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        mqtt_host: $("rec-host").value, mqtt_port: +$("rec-port").value,
        mqtt_prefix: $("rec-prefix").value, history_probe_s: +$("rec-probe").value,
      }),
    });
    $("rec-panel").classList.add("hidden");
    setTimeout(loadSummary, 1500);
  });

  let resizeTimer = null;
  window.addEventListener("resize", () => {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(() => { renderDetail(); renderOverview(); }, 150);
  });
}

wire();
const fromHash = /dev=([^&]+)/.exec(location.hash);
// #ov=heap (or timeline, wlan, perf) opens the overview on that view.
const viewFromHash = /ov=(timeline|heap|wlan|perf)/.exec(location.hash);
if (viewFromHash) fleet.ov.view = viewFromHash[1];
loadSummary().then(() => { if (fromHash) select(decodeURIComponent(fromHash[1])); });
loadOverview();
setInterval(loadSummary, 15000);
setInterval(loadOverview, 60000);
setInterval(loadDetail, 30000);
