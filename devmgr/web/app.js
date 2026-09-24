/* OhEzTouch Device Manager -- the page's logic.
 *
 * Everything comes from /api/state, polled at the refresh interval; the
 * console's log comes from /api/log with a cursor so entries are fetched
 * once and in order. Nothing is rendered from memory that the server does
 * not also have: a reload of the page is never a loss of truth.
 */
"use strict";

/* ------------------------------------------------------------------ state */

const state = {
  settings: { subnet: "", interval_s: 10, refresh_enabled: true, log_level: "info" },
  devices: [],
  latestVersion: null,
  scan: { running: false, done: 0, total: 0 },
  refresh: null,
  refreshSeenAt: 0,
  updates: {},
  selected: new Set(),
  sort: { key: "hostname", dir: 1 },
  pollTimer: null,
};

const consoleState = {
  since: 0,
  paused: false,
  entries: [],          // what is shown, after filters
  unseenProblems: 0,    // warn+error while the drawer is closed
  sources: new Set(),
};

/* ----------------------------------------------------------------- helpers */

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

function fmtUptime(seconds) {
  if (!seconds) return "-";
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  if (d > 0) return d + "d " + h + "h";
  if (h > 0) return h + "h " + m + "m";
  return m + "m";
}

function esc(text) {
  const span = document.createElement("span");
  span.textContent = text == null ? "" : String(text);
  return span.innerHTML;
}

/* The RSSI bands, colour-coded wherever a signal is shown: green is
 * comfortable, yellow is workable, red is on the edge of dropping out. */
function rssiClass(d) {
  if (d.wired || d.rssi == null) return "";
  if (d.rssi >= -60) return "good";
  if (d.rssi >= -75) return "fair";
  return "weak";
}

function rssiText(d) {
  if (d.wired) return "wired";
  return d.rssi != null ? d.rssi + " dBm" : "-";
}

/* ------------------------------------------------------------- state poll */

async function pollState() {
  try {
    const doc = await api("/api/state");
    state.settings = doc.settings;
    state.devices = doc.devices;
    state.scan = doc.scan;
    state.updates = doc.updates || {};
    state.latestVersion = doc.latest_version || null;
    state.refresh = doc.refresh || null;
    state.refreshSeenAt = Date.now();
    refreshTick.was = -1;
    renderDevices();
    renderScan();
    renderRefresh();
    renderUpdateProgress();
  } catch (error) {
    /* The manager itself is gone -- say so once, not on every tick. */
    if (!pollState.failed) {
      pollState.failed = true;
      toast("devmgr not reachable: " + error.message, "err");
    }
    return;
  }
  pollState.failed = false;
  schedulePoll();
}

function schedulePoll() {
  clearTimeout(state.pollTimer);

  /* The page refreshes at the configured interval, but never slower than the
   * console wants while it is open; 2 s is the floor so an upload's percent
   * stays live. */
  const interval = Math.max(2, Math.min(state.settings.interval_s || 10, 10));
  state.pollTimer = setTimeout(() => { pollState(); pollConsole(); },
                               interval * 1000);
}

/* ------------------------------------------------------------ device table */

/* Clicking a column header sorts by it; clicking again flips the direction.
 * The arrow in the header is CSS, from the sorted-asc/desc classes. */
function versionTuple(version) {
  const match = String(version || "").match(/^(\d+)\.(\d+)$/);
  return match ? [parseInt(match[1], 10), parseInt(match[2], 10)] : [0, 0];
}

function compareTuples(a, b) {
  for (let i = 0; i < Math.max(a.length, b.length); i++) {
    const da = a[i] || 0, db = b[i] || 0;
    if (da !== db) return da - db;
  }
  return 0;
}

function compareIp(a, b) {
  const pa = String(a || "").split(".").map(Number);
  const pb = String(b || "").split(".").map(Number);

  for (let i = 0; i < 4; i++) {
    const da = pa[i] || 0, db = pb[i] || 0;
    if (da !== db) return da - db;
  }
  return 0;
}

/* Wired counts as the best signal there is; no reading as the worst. */
function rssiSortValue(d) {
  if (d.wired) return 1;
  return d.rssi == null ? -999 : d.rssi;
}

function compareDevices(a, b) {
  const key = state.sort.key;
  let result = 0;

  if (key === "ip") result = compareIp(a.ip, b.ip);
  else if (key === "version")
    result = compareTuples(versionTuple(a.version), versionTuple(b.version));
  else if (key === "rssi") result = rssiSortValue(a) - rssiSortValue(b);
  else if (key === "uptime") result = (a.uptime_s || 0) - (b.uptime_s || 0);
  else if (key === "target")
    result = String(a.target_name || "").localeCompare(String(b.target_name || ""));
  else if (key === "last_seen")
    result = String(a.last_seen || "").localeCompare(String(b.last_seen || ""));
  else result = String(a.hostname || a.ip).localeCompare(String(b.hostname || b.ip));

  /* The MAC fallback keeps the order stable across polls. */
  if (result === 0) result = String(a.mac).localeCompare(String(b.mac));

  return result * state.sort.dir;
}

function renderSortHeaders() {
  for (const th of document.querySelectorAll("#devices th.sortable")) {
    th.classList.toggle("sorted-asc",
      th.dataset.sort === state.sort.key && state.sort.dir > 0);
    th.classList.toggle("sorted-desc",
      th.dataset.sort === state.sort.key && state.sort.dir < 0);
  }
}

/* The badge a device under update wears in the version column. */
function updateBadgeClass(phase) {
  if (phase === "PASS") return "pass";
  if (phase === "FAIL") return "fail";
  if (phase === "waiting for reboot" || phase === "verifying") return "waiting";
  return "running";
}

function renderDevices() {
  const tbody = $("device-rows");
  tbody.innerHTML = "";

  const devices = [...state.devices].sort(compareDevices);

  $("empty-hint").classList.toggle("hidden", state.devices.length > 0);

  const online = state.devices.filter(d => d.online).length;
  $("device-summary").textContent =
    state.devices.length + " device" + (state.devices.length === 1 ? "" : "s")
    + ", " + online + " online";

  renderSortHeaders();

  for (const d of devices) {
    const tr = document.createElement("tr");
    tr.className = d.online ? "" : "offline";

    const name = d.hostname || "(unnamed)";
    const update = state.updates[d.mac];
    const outdated = state.latestVersion && d.version
      && compareTuples(versionTuple(d.version),
                       versionTuple(state.latestVersion)) < 0;

    tr.innerHTML =
      "<td><input type='checkbox' class='row-select' data-mac='" + esc(d.mac) + "'" +
        (state.selected.has(d.mac) ? " checked" : "") + "></td>" +
      "<td><span class='status-dot " + (d.online ? "on" : "off") + "'></span></td>" +
      "<td class='hostcell'><strong>" + esc(name) + "</strong>" +
        (d.comment ? "<span class='comment'>" + esc(d.comment) + "</span>" : "") + "</td>" +
      "<td>" + esc(d.ip) + "</td>" +
      "<td class='dim small'>" + esc(d.mac) + "</td>" +
      "<td>" + esc(d.target_name || "-") + "</td>" +
      "<td" + (outdated ? " class='outdated' title='newest firmware: "
                        + esc(state.latestVersion) + "'" : "") + ">" +
        esc(d.version || "-") +
        (update ? " <span class='upd " + updateBadgeClass(update.phase) + "'>" +
          esc(update.phase === "uploading"
              ? "uploading " + update.percent + "%" : update.phase) +
          "</span>" : "") + "</td>" +
      "<td class='rssi " + rssiClass(d) + "'>" + esc(rssiText(d)) + "</td>" +
      "<td>" + fmtUptime(d.uptime_s) + "</td>" +
      "<td class='dim small'>" + esc(d.last_seen || "-") + "</td>" +
      "<td class='actions'>" +
        "<button data-act='config'  title='Configure'>&#9881;</button>" +
        "<button data-act='chime'   title='Play door chime (locate)'" + (d.online ? "" : " disabled") + ">&#128276;</button>" +
        "<button data-act='restart' title='Restart'" + (d.online ? "" : " disabled") + ">&#10227;</button>" +
        "<button data-act='comment' title='Edit comment'>&#9998;</button>" +
        "<button data-act='delete'  title='Remove from list'>&times;</button>" +
      "</td>";

    for (const btn of tr.querySelectorAll("button[data-act]")) {
      btn.addEventListener("click", () => deviceAction(d, btn.dataset.act));
    }

    tbody.appendChild(tr);
  }

  for (const box of tbody.querySelectorAll(".row-select")) {
    box.addEventListener("change", () => {
      if (box.checked) state.selected.add(box.dataset.mac);
      else state.selected.delete(box.dataset.mac);
      renderBulkBar();
    });
  }

  renderBulkBar();
}

function renderBulkBar() {
  const count = [...state.selected].filter(mac =>
    state.devices.some(d => d.mac === mac)).length;

  /* The bar is permanent; only the actions come and go with the selection. */
  $("bulk-group").classList.toggle("hidden", count === 0);
  $("bulk-count").textContent = count + " selected";
}

/* The bottom bar's left side: how the fleet refresh is doing -- a countdown
 * to the next pass, or the pass itself with a progress bar. The countdown
 * is the server's number, decremented locally between polls so it moves
 * every second instead of every poll. */
function refreshCountdown() {
  const r = state.refresh;
  if (!r) return 0;
  const elapsed = Math.floor((Date.now() - state.refreshSeenAt) / 1000);
  return Math.max(0, r.next_in - elapsed);
}

function renderRefresh() {
  const label = $("refresh-label");
  const bar = $("refresh-progress");
  const r = state.refresh;

  if (r && r.running) {
    label.textContent = "refreshing " + r.done + " / " + r.total;
    bar.classList.remove("hidden");
    bar.firstElementChild.style.width =
      (r.total ? r.done * 100 / r.total : 0) + "%";
  } else if (r && r.enabled) {
    label.textContent = "next refresh in " + refreshCountdown() + " s";
    bar.classList.add("hidden");
  } else {
    label.textContent = "auto-refresh off";
    bar.classList.add("hidden");
  }
}

/* One tick a second between polls keeps the countdown moving; when it runs
 * out, one extra poll picks up the pass as it starts. */
function refreshTick() {
  const r = state.refresh;
  if (!r || !r.enabled || r.running) return;

  const remaining = refreshCountdown();
  renderRefresh();

  if (remaining === 0 && refreshTick.was > 0) pollState();
  refreshTick.was = remaining;
}

function renderScan() {
  const el = $("scan-progress");
  const scan = state.scan;

  el.classList.toggle("hidden", !scan.running);
  $("scan-btn").disabled = !!scan.running;

  if (scan.running && scan.total > 0) {
    $("scan-bar").style.width = (scan.done * 100 / scan.total) + "%";
    $("scan-text").textContent = "Scanning " + state.settings.subnet +
      " ... " + scan.done + " / " + scan.total;
  }
}

/* ---------------------------------------------------------- device actions */

async function deviceAction(d, action) {
  const name = d.hostname || d.ip;

  if (action === "config") {
    openConfigModal([d.mac], false);
    return;
  }

  if (action === "comment") {
    const comment = prompt("Comment for " + name + ":", d.comment || "");
    if (comment === null) return;
    await api("/api/device/" + d.mac + "/comment", { comment });
    pollState();
    return;
  }

  if (action === "delete") {
    if (!confirm("Remove " + name + " from the list?")) return;
    await api("/api/device/" + d.mac + "/delete", {});
    pollState();
    return;
  }

  if (action === "chime") {
    try {
      await api("/api/device/" + d.mac + "/chime", {});
      toast("Door chime queued on " + name, "ok");
    } catch (error) {
      toast("Chime failed on " + name + ": " + error.message, "err");
    }
    return;
  }

  if (action === "restart") {
    if (!confirm("Restart " + name + "?")) return;
    try {
      await api("/api/device/" + d.mac + "/restart", {});
      toast("Restart requested on " + name, "ok");
    } catch (error) {
      toast("Restart failed on " + name + ": " + error.message, "err");
    }
    return;
  }
}

/* ------------------------------------------------------------- config modal */

/* The modal is shared by the single-device form and the bulk editor: same
 * renderer, two submit behaviours. In bulk mode every field gets a checkbox
 * saying "change this on all selected devices"; in single mode the fields
 * are sent only when changed. */
const configModal = { macs: [], bulk: false, schema: null };

async function openConfigModal(macs, bulk) {
  configModal.macs = macs;
  configModal.bulk = bulk;

  const names = macs.map(mac => {
    const d = state.devices.find(x => x.mac === mac);
    return d ? (d.hostname || d.ip) : mac;
  });

  $("config-title").textContent = bulk
    ? "Bulk edit " + macs.length + " device(s): " + names.join(", ")
    : "Configure " + names[0];

  $("config-body").innerHTML = "<p class='dim'>Loading ...</p>";
  $("config-result").textContent = "";
  $("config-result").className = "result";
  $("config-modal").classList.remove("hidden");

  try {
    /* The schema comes from the first selected device: labels, kinds,
     * options, ranges and current values, straight off its firmware. */
    configModal.schema = await api("/api/device/" + macs[0] + "/config");
  } catch (error) {
    $("config-body").innerHTML = "";
    showConfigResult("Could not read settings: " + error.message, false);
    return;
  }

  renderConfigForm();
}

/* The form is grouped into the firmware's own tabs -- /api/config names a
 * section's tab, so the grouping here is the panel's grouping on the touch
 * screen, not a second copy of it. */
const configTabs = { active: null };

function renderConfigForm() {
  const body = $("config-body");
  body.innerHTML = "";

  const tabBar = document.createElement("div");
  tabBar.className = "tab-bar";
  body.appendChild(tabBar);

  const panels = document.createElement("div");
  panels.className = "tab-panels";
  body.appendChild(panels);

  const tabs = [];        // {name, button, panel}
  let currentPanel = null;
  let fieldset = null;

  for (const field of configModal.schema.fields) {
    if (field.section !== undefined) {
      const tabName = field.tab || field.section;
      let tab = tabs.find(t => t.name === tabName);

      if (!tab) {
        const button = document.createElement("button");
        button.type = "button";
        button.className = "tab-btn";
        button.textContent = tabName;
        button.addEventListener("click", () => activateConfigTab(tabName));
        tabBar.appendChild(button);

        const panel = document.createElement("div");
        panel.className = "tab-panel hidden";
        panels.appendChild(panel);

        tab = { name: tabName, button, panel };
        tabs.push(tab);
      }

      currentPanel = tab.panel;

      fieldset = document.createElement("fieldset");
      const legend = document.createElement("legend");
      legend.textContent = field.section;
      fieldset.appendChild(legend);
      currentPanel.appendChild(fieldset);
      continue;
    }

    if (fieldset === null) continue;

    const label = document.createElement("label");
    const title = field.label + (field.restart ? " *" : "");
    let input;

    if (field.kind === "bool") {
      label.className = "bool";
      input = document.createElement("input");
      input.type = "checkbox";
      input.checked = !!field.value;
      label.appendChild(input);
      label.appendChild(document.createTextNode(title));
    } else if (field.kind === "enum") {
      const span = document.createElement("span");
      span.textContent = title;
      label.appendChild(span);
      input = document.createElement("select");
      for (const option of field.options || []) {
        const el = document.createElement("option");
        el.textContent = option;
        el.selected = option === field.value;
        input.appendChild(el);
      }
      label.appendChild(input);
    } else if (field.kind === "int") {
      const span = document.createElement("span");
      span.textContent = title;
      label.appendChild(span);
      input = document.createElement("input");
      input.type = "number";
      input.min = field.min;
      input.max = field.max;
      input.value = field.value;
      label.appendChild(input);
    } else {
      const span = document.createElement("span");
      span.textContent = title;
      label.appendChild(span);
      input = document.createElement("input");
      input.type = field.secret ? "password" : "text";
      input.value = field.value;
      label.appendChild(input);
    }

    input.dataset.field = field.name;
    input.dataset.kind = field.kind;
    input.dataset.original = field.kind === "bool" ? String(!!field.value)
                                                   : String(field.value);

    if (field.restart) {
      const flag = document.createElement("span");
      flag.className = "restart-flag small";
      flag.textContent = " takes effect after a restart";
      label.appendChild(flag);
    }

    /* Bulk mode: a leading checkbox opts this field into the change set. */
    if (configModal.bulk) {
      label.classList.add("bulk");
      const include = document.createElement("input");
      include.type = "checkbox";
      include.className = "bulk-include";
      label.prepend(include);
      input.disabled = true;
      include.addEventListener("change", () => { input.disabled = !include.checked; });
    }

    /* Every change, from any field or include checkbox, re-evaluates whether
     * there is anything to save. */
    input.addEventListener("input", updateConfigSaveState);
    input.addEventListener("change", updateConfigSaveState);

    fieldset.appendChild(label);
  }

  for (const include of body.querySelectorAll(".bulk-include"))
    include.addEventListener("change", updateConfigSaveState);

  /* Keep the tab the user was on across a re-render (after a save), if it
   * still exists. */
  const names = tabs.map(t => t.name);
  activateConfigTab(names.includes(configTabs.active) ? configTabs.active
                                                      : names[0]);

  updateConfigSaveState();
}

function activateConfigTab(name) {
  configTabs.active = name;

  for (const btn of $("config-body").querySelectorAll(".tab-btn"))
    btn.classList.toggle("active", btn.textContent === name);

  const tabs = [];
  for (const panel of $("config-body").querySelectorAll(".tab-panel")) {
    panel.classList.add("hidden");
    tabs.push(panel);
  }

  const tabBtns = [...$("config-body").querySelectorAll(".tab-btn")];
  const index = tabBtns.findIndex(b => b.textContent === name);

  if (index >= 0 && tabs[index])
    tabs[index].classList.remove("hidden");
}

/* Whether the form holds anything worth sending: in single mode, a field
 * whose value differs from what the device reported; in bulk mode, a field
 * whose include checkbox is ticked. The Save button shades itself from
 * this, so "nothing to save" is visible before it is clickable. */
function configHasChanges() {
  for (const input of $("config-body").querySelectorAll("[data-field]")) {
    if (configModal.bulk) {
      const include = input.closest("label").querySelector(".bulk-include");
      if (include && include.checked) return true;
    } else {
      const current = input.dataset.kind === "bool" ? String(input.checked)
                                                    : input.value;
      if (current !== input.dataset.original) return true;
    }
  }

  return false;
}

function updateConfigSaveState() {
  $("config-save").disabled = !configHasChanges();
}

function collectConfigFields() {
  const fields = {};

  for (const input of $("config-body").querySelectorAll("[data-field]")) {
    const kind = input.dataset.kind;

    if (configModal.bulk) {
      const include = input.closest("label").querySelector(".bulk-include");
      if (!include || !include.checked) continue;
    } else {
      /* Single device: only what actually changed is sent. */
      const current = kind === "bool" ? String(input.checked) : input.value;
      if (current === input.dataset.original) continue;
    }

    if (kind === "bool") fields[input.dataset.field] = input.checked;
    else if (kind === "int") fields[input.dataset.field] = parseInt(input.value, 10);
    else fields[input.dataset.field] = input.value;
  }

  return fields;
}

function showConfigResult(text, ok) {
  const el = $("config-result");
  el.textContent = text;
  el.className = "result " + (ok ? "ok" : "err");
}

async function saveConfig() {
  const fields = collectConfigFields();

  if (Object.keys(fields).length === 0) {
    showConfigResult(configModal.bulk
      ? "Nothing selected to change." : "Nothing changed.", false);
    return;
  }

  if (configModal.bulk) {
    try {
      const doc = await api("/api/devices/config",
                            { macs: configModal.macs, fields });
      const lines = configModal.macs.map(mac => {
        const d = state.devices.find(x => x.mac === mac);
        const name = d ? (d.hostname || d.ip) : mac;
        const r = doc.results[mac];
        return name + ": " + (r && r.ok ? "ok" : "FAILED " + JSON.stringify(r && r.error));
      });
      const allOk = configModal.macs.every(mac => doc.results[mac] && doc.results[mac].ok);
      showConfigResult(lines.join("\n"), allOk);
    } catch (error) {
      showConfigResult("Bulk edit failed: " + error.message, false);
    }
    return;
  }

  try {
    await api("/api/device/" + configModal.macs[0] + "/config", fields);
    showConfigResult("Saved: " + Object.keys(fields).join(", "), true);
    /* Re-render from what the device reports now, so the dirty tracking
     * restarts from truth. */
    configModal.schema = await api("/api/device/" + configModal.macs[0] + "/config");
    renderConfigForm();
  } catch (error) {
    showConfigResult("Save failed: " + error.message, false);
  }
}

/* ------------------------------------------------------------- update modal */

const updateModal = { macs: [], uploaded: null, refreshed: new Set() };

/* An update counts as running until the worker has said PASS or FAIL. */
function updateInProgress(macs) {
  return macs.some(mac => {
    const u = state.updates[mac];
    return u && u.phase !== "PASS" && u.phase !== "FAIL";
  });
}

async function openUpdateModal(macs) {
  updateModal.macs = macs;
  updateModal.uploaded = null;
  updateModal.refreshed = new Set();

  const tbody = $("update-rows");
  tbody.innerHTML = "";

  for (const mac of macs) {
    const d = state.devices.find(x => x.mac === mac);
    if (!d) continue;
    const tr = document.createElement("tr");
    tr.dataset.mac = mac;
    tr.innerHTML =
      "<td class='name'>" + esc(d.hostname || d.ip) + "</td>" +
      "<td>" + esc(d.target_name || "?") + "</td>" +
      "<td class='version'>" + esc(d.version || "?") + "</td>" +
      "<td class='rssi " + rssiClass(d) + "'>" + esc(rssiText(d)) + "</td>" +
      "<td class='progress-cell'>" +
        "<span class='phase'></span>" +
        "<div class='progress'><div></div></div>" +
      "</td>";
    tbody.appendChild(tr);
  }

  /* Reopening mid-run shows the workers' progress, so the button shades
   * itself to match. */
  $("update-modal").classList.remove("hidden");
  $("update-start").disabled = updateInProgress(macs);
  renderUpdateProgress();

  try {
    const doc = await api("/api/update/images");
    const rows = [];

    for (const [target, name] of Object.entries(doc.targets)) {
      const image = doc.images[target];

      if (image && image.path) {
        const source = image.source === "build tree" && doc.tree_version
          ? "build tree (v" + doc.tree_version + ")"
          : image.source;
        rows.push("<tr><td>" + esc(name) + "</td>" +
                  "<td class='path'>" + esc(image.path) + "</td>" +
                  "<td class='src'>" + esc(source) + "</td></tr>");
      } else {
        rows.push("<tr class='missing'><td>" + esc(name) + "</td>" +
                  "<td colspan='2'>no image</td></tr>");
      }
    }

    $("update-images").innerHTML =
      "<table>" +
      "<thead><tr><th>Target</th><th>Image</th><th>Source</th></tr></thead>" +
      "<tbody>" + rows.join("") + "</tbody>" +
      "</table>";
  } catch (error) {
    $("update-images").textContent = "Could not list images: " + error.message;
  }
}

function renderUpdateProgress() {
  /* A device whose worker has finished gets one refresh of exactly that
   * device, so its version comes back current -- no fleet refresh, and
   * nothing poked mid-upload. Runs even when the dialog is closed. */
  for (const mac of updateModal.macs) {
    const u = state.updates[mac];

    if (!u || (u.phase !== "PASS" && u.phase !== "FAIL")) continue;
    if (updateModal.refreshed.has(mac)) continue;

    updateModal.refreshed.add(mac);
    api("/api/device/" + mac + "/refresh", {})
      .then(() => pollState())
      .catch(() => { /* the state poll reports the manager being gone */ });
  }

  if ($("update-modal").classList.contains("hidden")) return;

  for (const mac of updateModal.macs) {
    const u = state.updates[mac];
    if (!u) continue;

    const tr = $("update-rows").querySelector("[data-mac='" + CSS.escape(mac) + "']");
    if (!tr) continue;

    const phase = tr.querySelector(".phase");
    phase.textContent =
      u.phase + (u.phase === "uploading" ? " " + u.percent + "%" : "") +
      (u.detail ? " -- " + u.detail : "");
    phase.className = "phase " + u.phase;

    const bar = tr.querySelector(".progress");
    bar.className = "progress " + u.phase;
    bar.firstElementChild.style.width = u.percent + "%";

    /* The version column follows what the fleet reports, so a finished
     * update shows the new version as soon as a refresh has read it. */
    const d = state.devices.find(x => x.mac === mac);
    if (d) tr.querySelector(".version").textContent = d.version || "?";
  }

  $("update-start").disabled = updateInProgress(updateModal.macs);
}

async function startUpdate() {
  const source = document.querySelector("input[name=update-src]:checked").value;
  const body = { macs: updateModal.macs, image: source };

  /* Inactive from the first click until every worker has finished -- or,
   * when nothing started, until the toasts have said why. */
  $("update-start").disabled = true;

  try {
    if (source === "upload") {
      if (!updateModal.uploaded) {
        const file = $("update-file").files[0];
        if (!file) { toast("Choose a .bin file first", "err"); return; }

        const response = await fetch("/api/update/upload",
          { method: "POST",
            headers: { "X-Filename": file.name,
                       "Content-Type": "application/octet-stream" },
            body: file });
        const doc = await response.json();
        if (!response.ok) { toast("Upload failed: " + (doc.error || response.status), "err"); return; }
        updateModal.uploaded = doc.name;
      }

      body.name = updateModal.uploaded;
    }

    const doc = await api("/api/update", body);
    if (doc.skipped && doc.skipped.length) {
      for (const s of doc.skipped) {
        const d = state.devices.find(x => x.mac === s.mac);
        toast((d ? (d.hostname || d.ip) : s.mac) + ": skipped -- " + s.reason, "err");
      }
    }
    await pollState();
  } catch (error) {
    toast("Update failed: " + error.message, "err");
  }

  $("update-start").disabled = updateInProgress(updateModal.macs);
}

/* ------------------------------------------------------------------ console */

const LEVEL_ORDER = { debug: 0, info: 1, warn: 2, error: 3 };

function renderConsole() {
  const minLevel = LEVEL_ORDER[$("console-level").value] || 0;
  const source = $("console-source").value;
  const lines = $("console-lines");

  const atBottom = lines.scrollTop + lines.clientHeight >= lines.scrollHeight - 20;

  lines.innerHTML = "";

  for (const entry of consoleState.entries) {
    if (LEVEL_ORDER[entry.level] < minLevel) continue;
    if (source && entry.source !== source) continue;
    const div = document.createElement("div");
    div.className = entry.level;
    div.innerHTML = "<span class='ts'>" + esc(entry.ts) + "</span>" +
                    "<span class='src'>" + esc(entry.source) + "</span>" +
                    esc(entry.message);
    lines.appendChild(div);
  }

  if (atBottom) lines.scrollTop = lines.scrollHeight;
}

async function pollConsole() {
  if (consoleState.paused) return;

  try {
    /* Everything is fetched once; the toolbar's level filter is applied at
     * render time, so changing it does not re-fetch or lose entries. */
    const doc = await api("/api/log?since=" + consoleState.since);

    for (const entry of doc.entries) {
      consoleState.since = Math.max(consoleState.since, entry.seq);
      consoleState.entries.push(entry);
      consoleState.sources.add(entry.source);

      if ($("console").classList.contains("hidden") &&
          (entry.level === "warn" || entry.level === "error")) {
        consoleState.unseenProblems++;
      }
    }

    /* The drawer shows the tail; the server keeps the rest. */
    if (consoleState.entries.length > 1000) {
      consoleState.entries.splice(0, consoleState.entries.length - 1000);
    }

    renderConsole();
    renderConsoleBadge();
    renderConsoleSources();
  } catch (error) { /* the state poll reports the manager being gone */ }
}

function renderConsoleBadge() {
  const badge = $("console-badge");
  badge.classList.toggle("hidden", consoleState.unseenProblems === 0);
  badge.textContent = consoleState.unseenProblems || "";
}

function renderConsoleSources() {
  const select = $("console-source");
  const current = select.value;
  const known = new Set([...select.options].map(o => o.value));

  for (const source of consoleState.sources) {
    if (known.has(source)) continue;
    const option = document.createElement("option");
    option.value = source;
    option.textContent = source;
    select.appendChild(option);
  }

  select.value = current;
}

/* The bulk bar rides above the console drawer; the body class is what
 * tells the stylesheet where the drawer is. */
function renderConsoleVisibility() {
  document.body.classList.toggle("console-open",
    !$("console").classList.contains("hidden"));
}

/* -------------------------------------------------------------------- setup */

function wireEvents() {
  $("scan-btn").addEventListener("click", async () => {
    try {
      await api("/api/scan", { subnet: $("subnet").value });
      pollState();
    } catch (error) {
      toast("Scan not started: " + error.message, "err");
    }
  });

  $("refresh-btn").addEventListener("click", async () => {
    await api("/api/refresh", {});
    setTimeout(pollState, 1500);
  });

  $("settings-btn").addEventListener("click", () => {
    const panel = $("settings-panel");
    panel.classList.toggle("hidden");
    $("set-subnet").value = state.settings.subnet;
    $("set-interval").value = String(state.settings.interval_s);
    $("set-refresh-enabled").checked = !!state.settings.refresh_enabled;
    $("set-log-level").value = state.settings.log_level;
  });

  /* A custom interval, typed rather than offered: the select covers the
   * common ones, and anything else goes through the same setting. */
  $("set-interval").addEventListener("change", () => {});
  $("settings-save").addEventListener("click", async () => {
    try {
      const doc = await api("/api/settings", {
        subnet: $("set-subnet").value,
        interval_s: parseInt($("set-interval").value, 10),
        refresh_enabled: $("set-refresh-enabled").checked,
        log_level: $("set-log-level").value,
      });
      state.settings = doc.settings;
      $("subnet").value = doc.settings.subnet;
      $("settings-panel").classList.add("hidden");
      schedulePoll();
      toast("Settings saved", "ok");
    } catch (error) {
      toast("Settings not saved: " + error.message, "err");
    }
  });

  $("console-btn").addEventListener("click", () => {
    $("console").classList.toggle("hidden");
    renderConsoleVisibility();
    consoleState.unseenProblems = 0;
    renderConsoleBadge();
    pollConsole();
  });

  $("console-hide").addEventListener("click", () => {
    $("console").classList.add("hidden");
    renderConsoleVisibility();
  });
  $("console-level").addEventListener("change", renderConsole);
  $("console-source").addEventListener("change", renderConsole);
  $("console-pause").addEventListener("click", () => {
    consoleState.paused = !consoleState.paused;
    $("console-pause").textContent = consoleState.paused ? "Resume" : "Pause";
  });
  $("console-clear").addEventListener("click", async () => {
    await api("/api/log/clear", {});
    consoleState.entries = [];
    consoleState.since = 0;
    renderConsole();
  });
  $("console-copy").addEventListener("click", () => {
    const text = consoleState.entries.map(e =>
      e.ts + " " + e.level.toUpperCase().padEnd(5) + " [" + e.source + "] " + e.message
    ).join("\n");
    navigator.clipboard.writeText(text).then(() => toast("Console copied", "ok"));
  });

  $("select-all").addEventListener("change", () => {
    state.selected.clear();
    if ($("select-all").checked) {
      for (const d of state.devices) state.selected.add(d.mac);
    }
    renderDevices();
  });

  for (const th of document.querySelectorAll("#devices th.sortable")) {
    th.addEventListener("click", () => {
      if (state.sort.key === th.dataset.sort) state.sort.dir = -state.sort.dir;
      else { state.sort.key = th.dataset.sort; state.sort.dir = 1; }
      renderDevices();
    });
  }

  $("bulk-clear-btn").addEventListener("click", () => {
    state.selected.clear();
    renderDevices();
  });
  $("bulk-config-btn").addEventListener("click", () => {
    const macs = [...state.selected].filter(mac =>
      state.devices.some(d => d.mac === mac && d.online));
    if (macs.length === 0) { toast("No online devices selected", "err"); return; }
    openConfigModal(macs, true);
  });
  $("bulk-update-btn").addEventListener("click", () => {
    const macs = [...state.selected].filter(mac =>
      state.devices.some(d => d.mac === mac && d.online));
    if (macs.length === 0) { toast("No online devices selected", "err"); return; }
    openUpdateModal(macs);
  });
  $("bulk-restart-btn").addEventListener("click", async () => {
    const macs = [...state.selected].filter(mac =>
      state.devices.some(d => d.mac === mac && d.online));
    if (!confirm("Restart " + macs.length + " device(s)?")) return;
    for (const mac of macs) {
      try { await api("/api/device/" + mac + "/restart", {}); }
      catch (error) { /* reported per device in the console */ }
    }
    toast("Restart requested on " + macs.length + " device(s)", "ok");
  });

  $("config-save").addEventListener("click", saveConfig);
  $("config-cancel").addEventListener("click", () => $("config-modal").classList.add("hidden"));

  $("update-start").addEventListener("click", startUpdate);
  $("update-close").addEventListener("click", () => $("update-modal").classList.add("hidden"));
}

async function boot() {
  wireEvents();
  setInterval(refreshTick, 1000);
  await pollState();
  $("subnet").value = state.settings.subnet;
  pollConsole();
  schedulePoll();

  /* A deep link: #config=<mac> opens that device's settings dialog, so a
   * bookmark lands straight in the form. */
  if (location.hash.startsWith("#config=")) {
    const mac = location.hash.slice(8);

    if (state.devices.some(d => d.mac === mac && d.online))
      openConfigModal([mac], false);
  }
}

boot();
