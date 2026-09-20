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
  scan: { running: false, done: 0, total: 0 },
  updates: {},
  selected: new Set(),
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

/* ------------------------------------------------------------- state poll */

async function pollState() {
  try {
    const doc = await api("/api/state");
    state.settings = doc.settings;
    state.devices = doc.devices;
    state.scan = doc.scan;
    state.updates = doc.updates || {};
    renderDevices();
    renderScan();
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

function renderDevices() {
  const tbody = $("device-rows");
  tbody.innerHTML = "";

  $("empty-hint").classList.toggle("hidden", state.devices.length > 0);

  for (const d of state.devices) {
    const tr = document.createElement("tr");
    tr.className = d.online ? "" : "offline";

    const name = d.hostname || "(unnamed)";
    const update = state.updates[d.mac];

    tr.innerHTML =
      "<td><input type='checkbox' class='row-select' data-mac='" + esc(d.mac) + "'" +
        (state.selected.has(d.mac) ? " checked" : "") + "></td>" +
      "<td><span class='status-dot " + (d.online ? "on" : "off") + "'></span></td>" +
      "<td class='hostcell'><strong>" + esc(name) + "</strong>" +
        (d.comment ? "<span class='comment'>" + esc(d.comment) + "</span>" : "") + "</td>" +
      "<td>" + esc(d.ip) + "</td>" +
      "<td class='dim small'>" + esc(d.mac) + "</td>" +
      "<td>" + esc(d.target_name || "-") + "</td>" +
      "<td>" + esc(d.version || "-") + (update ? " <span class='dim'>(" + esc(update.phase) + ")</span>" : "") + "</td>" +
      "<td>" + (d.wired ? "wired" : (d.rssi != null ? esc(d.rssi) + " dBm" : "-")) + "</td>" +
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
  const bar = $("bulk-bar");
  const count = [...state.selected].filter(mac =>
    state.devices.some(d => d.mac === mac)).length;

  bar.classList.toggle("hidden", count === 0);
  $("bulk-count").textContent = count + " selected";
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

const updateModal = { macs: [], uploaded: null };

async function openUpdateModal(macs) {
  updateModal.macs = macs;
  updateModal.uploaded = null;

  const container = $("update-devices");
  container.innerHTML = "";

  for (const mac of macs) {
    const d = state.devices.find(x => x.mac === mac);
    if (!d) continue;
    const div = document.createElement("div");
    div.className = "update-device";
    div.dataset.mac = mac;
    div.innerHTML = "<span class='name'>" + esc(d.hostname || d.ip) + "</span>" +
                    "<span class='phase'>" + esc(d.target_name || "?") + ", v" + esc(d.version || "?") + "</span>";
    container.appendChild(div);
  }

  $("update-progress").innerHTML = "";
  $("update-modal").classList.remove("hidden");

  try {
    const doc = await api("/api/update/images");
    const lines = [];
    for (const [target, name] of Object.entries(doc.targets)) {
      const image = doc.images[target];
      lines.push(name + ": " + (image.path ? image.path : "no image"));
    }
    $("update-images").innerHTML = lines.map(esc).join("<br>");
  } catch (error) {
    $("update-images").textContent = "Could not list images: " + error.message;
  }
}

function renderUpdateProgress() {
  if ($("update-modal").classList.contains("hidden")) return;

  for (const mac of updateModal.macs) {
    const u = state.updates[mac];
    if (!u) continue;

    let div = $("update-progress").querySelector("[data-mac='" + CSS.escape(mac) + "']");

    if (!div) {
      div = document.createElement("div");
      div.className = "update-device";
      div.dataset.mac = mac;
      const d = state.devices.find(x => x.mac === mac);
      div.innerHTML = "<span class='name'>" + esc(d ? (d.hostname || d.ip) : mac) + "</span>" +
                      "<span class='phase'></span>" +
                      "<div class='progress'><div></div></div>";
      $("update-progress").appendChild(div);
    }

    div.querySelector(".phase").textContent =
      u.phase + (u.phase === "uploading" ? " " + u.percent + "%" : "") +
      (u.detail ? " -- " + u.detail : "");
    div.querySelector(".phase").className = "phase " + u.phase;
    const bar = div.querySelector(".progress");
    bar.className = "progress " + u.phase;
    bar.firstElementChild.style.width = u.percent + "%";
  }
}

async function startUpdate() {
  const source = document.querySelector("input[name=update-src]:checked").value;
  const body = { macs: updateModal.macs, image: source };

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

  try {
    const doc = await api("/api/update", body);
    if (doc.skipped && doc.skipped.length) {
      for (const s of doc.skipped) {
        const d = state.devices.find(x => x.mac === s.mac);
        toast((d ? (d.hostname || d.ip) : s.mac) + ": skipped -- " + s.reason, "err");
      }
    }
    pollState();
  } catch (error) {
    toast("Update failed: " + error.message, "err");
  }
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
    consoleState.unseenProblems = 0;
    renderConsoleBadge();
    pollConsole();
  });

  $("console-hide").addEventListener("click", () => $("console").classList.add("hidden"));
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
