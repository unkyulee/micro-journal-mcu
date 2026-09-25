#pragma once

#include <Arduino.h>

// Single page app served by the file server
// Self contained on purpose: in hotspot mode the browser has no internet,
// so no external fonts, scripts or stylesheets.
//
// Two views: the file list, and a full screen editor that shows nothing but
// the text, a back arrow and the save state.
static const char FILESERVER_PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Micro Journal</title>
<style>
:root {
  --bg: #f4f2ed;
  --paper: #fffdf9;
  --text: #24231f;
  --muted: #6f6b63;
  --faint: #a19c92;
  --line: #e3dfd6;
  --hover: #efece5;
  --accent: #3f6c58;
  --accent-soft: #e2ece6;
  --danger: #b1412f;
  --danger-soft: #f6e4df;
  --warn: #9a6a12;
  --warn-soft: #f5ecd8;
  --font-ui: system-ui, -apple-system, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
  --font-serif: "Iowan Old Style", "Palatino Linotype", Palatino, Charter, Georgia, serif;
  --font-mono: ui-monospace, "SF Mono", "Cascadia Mono", Menlo, Consolas, monospace;
  color-scheme: light;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #141412; --paper: #1a1a18; --text: #e9e6df; --muted: #a09b91; --faint: #6d6960;
    --line: #2d2c28; --hover: #252421; --accent: #86bba1; --accent-soft: #23302a;
    --danger: #e3806b; --danger-soft: #3a2420; --warn: #ddb25c; --warn-soft: #362d1b;
    color-scheme: dark;
  }
}

* { box-sizing: border-box; }
html, body { height: 100%; margin: 0; }
body { background: var(--bg); color: var(--text); font: 15px/1.45 var(--font-ui); -webkit-font-smoothing: antialiased; }
button { font: inherit; color: inherit; }
svg { width: 16px; height: 16px; flex: none; stroke: currentColor; fill: none; stroke-width: 1.8; stroke-linecap: round; stroke-linejoin: round; }
[hidden] { display: none !important; }

/* ---------- file list ---------- */
.list { max-width: 40rem; margin: 0 auto; padding: max(28px, env(safe-area-inset-top)) 16px 48px; }
.head { display: flex; align-items: flex-end; justify-content: space-between; gap: 12px; margin-bottom: 20px; }
.head h1 { margin: 0; font: 400 26px/1.2 var(--font-serif); letter-spacing: .01em; }
.head .sub { color: var(--faint); font-size: 13px; margin-top: 2px; }
.notice { margin: 0 0 16px; padding: 10px 12px; border-radius: 8px; background: var(--warn-soft); color: var(--warn); font-size: 14px; }
.notice.error { background: var(--danger-soft); color: var(--danger); }
.group { margin-bottom: 18px; }
.group-title { font-size: 11px; font-weight: 600; letter-spacing: .07em; text-transform: uppercase; color: var(--faint); padding: 0 12px 6px; }
.rows { background: var(--paper); border: 1px solid var(--line); border-radius: 12px; overflow: hidden; }
.file { display: flex; align-items: center; gap: 10px; padding: 11px 8px 11px 14px; cursor: pointer; }
.file + .file { border-top: 1px solid var(--line); }
.file:hover, .file:focus-visible { background: var(--hover); outline: 0; }
.file > svg { color: var(--muted); }
.file .name { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.file .tag { font-size: 11px; padding: 2px 7px; border-radius: 999px; background: var(--accent-soft); color: var(--accent); white-space: nowrap; }
.file .size { color: var(--faint); font-size: 13px; font-variant-numeric: tabular-nums; min-width: 4.5em; text-align: right; }
.icon-btn { display: inline-flex; align-items: center; justify-content: center; width: 32px; height: 32px; border-radius: 7px; border: 0; background: none; color: var(--faint); cursor: pointer; }
.icon-btn:hover { background: var(--line); color: var(--text); }
.icon-btn.danger:hover { background: var(--danger-soft); color: var(--danger); }
.empty { text-align: center; color: var(--muted); padding: 36px 16px; }
.storage { margin-top: 26px; color: var(--faint); font-size: 13px; }
.storage-bar { height: 4px; border-radius: 2px; background: var(--line); overflow: hidden; margin-bottom: 6px; }
.storage-bar span { display: block; height: 100%; width: 0; background: var(--accent); transition: width .3s; }

/* ---------- full screen editor ---------- */
.write { position: fixed; inset: 0; background: var(--paper); }
#editor { position: absolute; inset: 0; width: 100%; height: 100%; resize: none; border: 0; outline: 0; margin: 0;
  background: var(--paper); color: var(--text); caret-color: var(--accent); tab-size: 4;
  padding: max(72px, 12vh) max(24px, calc((100% - 44rem) / 2)) 45vh;
  font: 20px/1.75 var(--font-serif); }
#editor.mono { font: 15px/1.6 var(--font-mono); }
.corner { position: fixed; top: max(12px, env(safe-area-inset-top)); display: flex; align-items: center; z-index: 2; }
.corner.left { left: max(12px, env(safe-area-inset-left)); }
.corner.right { right: max(18px, env(safe-area-inset-right)); }
.back { width: 36px; height: 36px; border-radius: 50%; border: 0; background: none; color: var(--faint); cursor: pointer; display: inline-flex; align-items: center; justify-content: center; opacity: .55; transition: opacity .2s, background .2s; }
.back:hover, .back:focus-visible { opacity: 1; background: var(--hover); color: var(--text); outline: 0; }
.back svg { width: 20px; height: 20px; }
.state { font-size: 12px; color: var(--faint); font-variant-numeric: tabular-nums; }
.state[data-s="error"], .state[data-s="invalid"] { color: var(--danger); }

@media (max-width: 600px) {
  #editor { font-size: 18px; padding: 64px 18px 45vh; }
}

.toast { position: fixed; left: 50%; bottom: 24px; transform: translateX(-50%) translateY(16px); opacity: 0; background: var(--text); color: var(--bg); padding: 9px 14px; border-radius: 8px; font-size: 14px; transition: .25s; pointer-events: none; max-width: calc(100vw - 32px); z-index: 5; }
.toast.show { opacity: 1; transform: translateX(-50%); }
</style>
</head>
<body>
<svg style="display:none" xmlns="http://www.w3.org/2000/svg">
  <symbol id="i-file" viewBox="0 0 24 24"><path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><path d="M14 3v5h5M9 13h6M9 17h4"/></symbol>
  <symbol id="i-gear" viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M2 12h3M19 12h3M4.9 19.1 7 17M17 7l2.1-2.1"/></symbol>
  <symbol id="i-box" viewBox="0 0 24 24"><path d="M21 8 12 3 3 8v8l9 5 9-5z"/><path d="m3 8 9 5 9-5M12 13v8"/></symbol>
  <symbol id="i-down" viewBox="0 0 24 24"><path d="M12 4v11m-5-5 5 5 5-5M5 20h14"/></symbol>
  <symbol id="i-trash" viewBox="0 0 24 24"><path d="M4 7h16M10 11v6M14 11v6M6 7l1 12a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2l1-12M9 7V4h6v3"/></symbol>
  <symbol id="i-back" viewBox="0 0 24 24"><path d="M19 12H5m6-6-6 6 6 6"/></symbol>
</svg>

<!-- file list -->
<section class="list" id="listView">
  <div class="head">
    <div>
      <h1>Micro Journal</h1>
      <div class="sub" id="version">Connecting&hellip;</div>
    </div>
  </div>
  <div class="notice" id="notice" hidden></div>
  <div id="files"></div>
  <div class="storage" id="storage" hidden>
    <div class="storage-bar"><span id="storageFill"></span></div>
    <div id="storageText"></div>
  </div>
</section>

<!-- full screen editor -->
<section class="write" id="writeView" hidden>
  <div class="corner left"><button class="back" id="btnBack" title="Back to files" aria-label="Back to files"><svg><use href="#i-back"/></svg></button></div>
  <div class="corner right"><span class="state" id="state" aria-live="polite"></span></div>
  <textarea id="editor" spellcheck="true" autocapitalize="sentences" aria-label="File content"></textarea>
</section>

<div class="toast" id="toast"></div>

<script>
(() => {
  "use strict";

  // save shortly after typing pauses, but never wait longer than MAX_WAIT
  // while typing continuously - keeps flash writes to a few per minute
  const IDLE_MS = 2000;
  const MAX_WAIT_MS = 10000;
  const RETRY_MIN_MS = 3000;
  const RETRY_MAX_MS = 60000;
  const TEXT_EXT = ["txt", "md", "markdown", "json", "csv", "log", "ini", "cfg", "conf", "yml", "yaml", "xml", "html", "htm"];
  const SYSTEM_FILES = ["/config.json", "/wifi.json"];

  const $ = (id) => document.getElementById(id);
  const editor = $("editor");

  let listing = { files: [] };
  let doc = null;          // { name, saved, dirtySince, json }
  let saveTimer = 0;
  let inflight = null;
  let retryDelay = 0;
  let saveState = "";

  // ---------- helpers ----------
  const ext = (name) => { const m = /\.([^.\/]+)$/.exec(name); return m ? m[1].toLowerCase() : ""; };
  const baseName = (name) => name.replace(/^\//, "");
  const isText = (name) => { const e = ext(name); return e === "" || TEXT_EXT.includes(e); };
  const isJson = (name) => ext(name) === "json";
  const fileInfo = (name) => listing.files.find((f) => f.name === name);
  const svg = (id) => '<svg><use href="#' + id + '"/></svg>';

  function fmtSize(n) {
    if (n < 1024) return n + " B";
    if (n < 1048576) return (n / 1024).toFixed(n < 10240 ? 1 : 0) + " KB";
    return (n / 1048576).toFixed(1) + " MB";
  }
  const fmtTime = (d) => d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });

  let toastTimer = 0;
  function toast(msg) {
    const t = $("toast");
    t.textContent = msg;
    t.classList.add("show");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => t.classList.remove("show"), 2600);
  }

  async function api(method, url, body, headers) {
    let res;
    try {
      res = await fetch(url, { method, body, headers, cache: "no-store" });
    } catch (e) {
      throw new Error("Device not reachable");
    }
    let data = null;
    try { data = await res.json(); } catch (e) {}
    if (!res.ok) throw new Error((data && data.error) || ("HTTP " + res.status));
    return data;
  }
  const fileUrl = (name, extra) => "/api/file?name=" + encodeURIComponent(name) + (extra || "");

  // the body is sent as is, the device streams it straight to storage
  // the device only sees headers while a raw body streams in, so the name travels in one
  const uploadHeaders = (name) => ({ "Content-Type": "application/octet-stream", "X-File-Name": encodeURIComponent(name) });
  const uploadBlob = (name, blob) => api("POST", fileUrl(name), blob, uploadHeaders(name));

  // ---------- file list ----------
  async function refresh() {
    try {
      listing = await api("GET", "/api/list");
      renderList();
      showNotice();
    } catch (e) {
      showNotice(e.message + ". Is web mode still open on the device?", true);
    }
  }

  function showNotice(message, isError) {
    const n = $("notice");
    if (!message && listing.restart) message = "Settings changed. The device restarts to apply them when you exit web mode.";
    n.hidden = !message;
    n.textContent = message || "";
    n.classList.toggle("error", !!isError);
  }

  const groupOf = (name) => !isText(name) ? 2 : (isJson(name) ? 1 : 0);

  function renderList() {
    $("version").textContent = "Firmware " + (listing.version || "");

    const total = listing.total || 0, used = listing.used || 0;
    $("storage").hidden = !(total > 0);
    if (total > 0) {
      $("storageFill").style.width = Math.min(100, (used / total) * 100).toFixed(1) + "%";
      $("storageText").textContent = fmtSize(used) + " of " + fmtSize(total) + " used";
    }

    const box = $("files");
    box.textContent = "";
    if (!listing.files.length) {
      box.innerHTML = '<div class="empty">No files yet.</div>';
      return;
    }

    const groups = [["Documents", []], ["Settings", []], ["Other files", []]];
    listing.files.slice()
      .sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: "base" }))
      .forEach((f) => groups[groupOf(f.name)][1].push(f));

    groups.forEach(([title, files]) => {
      if (!files.length) return;
      const g = document.createElement("div");
      g.className = "group";
      g.innerHTML = '<div class="group-title"></div><div class="rows"></div>';
      g.firstChild.textContent = title;
      const rows = g.lastChild;

      files.forEach((f) => {
        const row = document.createElement("div");
        row.className = "file";
        row.tabIndex = 0;
        row.setAttribute("role", "button");
        row.title = isText(f.name) ? "Open " + baseName(f.name) : "Download " + baseName(f.name);
        const icon = isJson(f.name) ? "i-gear" : (isText(f.name) ? "i-file" : "i-box");
        row.innerHTML = svg(icon) + '<span class="name"></span>' +
          (f.name === listing.active ? '<span class="tag" title="The device reloads this file when you exit web mode">On device</span>' : "") +
          '<span class="size"></span>' +
          '<button class="icon-btn" data-a="download" title="Download">' + svg("i-down") + "</button>" +
          '<button class="icon-btn danger" data-a="delete" title="Delete">' + svg("i-trash") + "</button>";
        row.querySelector(".name").textContent = baseName(f.name);
        row.querySelector(".size").textContent = fmtSize(f.size);

        const activate = () => isText(f.name) ? openFile(f.name) : download(f.name);
        row.addEventListener("click", (ev) => {
          const a = ev.target.closest("[data-a]");
          if (!a) return activate();
          ev.stopPropagation();
          if (a.dataset.a === "download") download(f.name);
          else remove(f.name);
        });
        row.addEventListener("keydown", (ev) => { if (ev.key === "Enter" && ev.target === row) activate(); });
        rows.appendChild(row);
      });
      box.appendChild(g);
    });
  }

  // ---------- views ----------
  function showList() {
    $("writeView").hidden = true;
    $("listView").hidden = false;
    document.title = "Micro Journal";
  }

  function showEditor() {
    $("listView").hidden = true;
    $("writeView").hidden = false;
    document.title = baseName(doc.name) + " \u00b7 Micro Journal";
  }

  async function openFile(name, fromHistory) {
    if (!(await leaveCurrent())) return;

    const info = fileInfo(name);
    if (info && info.size > 4 * 1048576 && !confirm(baseName(name) + " is " + fmtSize(info.size) + ". Opening it may be slow. Continue?")) return;

    let text;
    try {
      const res = await fetch(fileUrl(name), { cache: "no-store" });
      if (!res.ok) throw new Error(res.status === 404 ? "File not found" : "HTTP " + res.status);
      text = await res.text();
    } catch (e) {
      toast("Couldn't open " + baseName(name) + ": " + e.message);
      refresh();
      return;
    }

    doc = { name, saved: text, dirtySince: 0, json: isJson(name) };
    editor.value = text;
    editor.classList.toggle("mono", doc.json);
    editor.spellcheck = !doc.json;
    setState("saved", "Saved");
    showEditor();
    if (!fromHistory) history.pushState({ file: name }, "", "#" + encodeURIComponent(name));

    // journals grow at the end - continue where the writing stopped
    const caret = doc.json ? 0 : text.length;
    editor.focus();
    editor.setSelectionRange(caret, caret);
    editor.scrollTop = doc.json ? 0 : editor.scrollHeight;
  }

  // flush and go back to the list
  async function closeFile(fromHistory) {
    if (!(await leaveCurrent())) {
      // stay on the file, restore the address bar entry the browser dropped
      if (fromHistory && doc) history.pushState({ file: doc.name }, "", "#" + encodeURIComponent(doc.name));
      return;
    }
    clearTimeout(saveTimer);
    doc = null;
    editor.value = "";
    showList();
    if (!fromHistory) history.back();
    refresh();
  }

  // make sure nothing is lost before switching away from the current file
  async function leaveCurrent() {
    if (!doc) return true;
    if (!isDirty() && !inflight) return true;
    if (await save()) return true;
    return confirm("Changes to " + baseName(doc.name) + " couldn't be saved. Discard them?");
  }

  // ---------- autosave ----------
  const isDirty = () => !!doc && editor.value !== doc.saved;

  function jsonProblem(text) {
    try { JSON.parse(text); return ""; } catch (e) { return e.message; }
  }

  function schedule() {
    clearTimeout(saveTimer);
    if (!isDirty()) return;
    const now = Date.now();
    if (!doc.dirtySince) doc.dirtySince = now;
    const wait = Math.max(0, Math.min(IDLE_MS, doc.dirtySince + MAX_WAIT_MS - now));
    saveTimer = setTimeout(save, wait);
  }

  async function save() {
    clearTimeout(saveTimer);
    while (inflight) await inflight.catch(() => {});
    if (!isDirty()) return true;

    const d = doc, text = editor.value;
    if (d.json) {
      const problem = jsonProblem(text);
      if (problem) { setState("invalid", "Invalid JSON \u00b7 not saved", problem); return false; }
    }

    setState("saving", "Saving\u2026");
    inflight = uploadBlob(d.name, new Blob([text], { type: "text/plain" }));
    try {
      await inflight;
      d.saved = text;
      retryDelay = 0;
      if (doc === d) {
        if (isDirty()) { d.dirtySince = Date.now(); setState("dirty", "Edited"); schedule(); }
        else { d.dirtySince = 0; setState("saved", "Saved " + fmtTime(new Date())); }
      }
      return true;
    } catch (e) {
      if (doc === d) {
        retryDelay = Math.min(retryDelay ? retryDelay * 2 : RETRY_MIN_MS, RETRY_MAX_MS);
        setState("error", "Not saved \u00b7 retrying in " + Math.round(retryDelay / 1000) + "s", e.message);
        saveTimer = setTimeout(save, retryDelay);
      }
      return false;
    } finally {
      inflight = null;
    }
  }

  function setState(s, text, detail) {
    saveState = s;
    const el = $("state");
    el.dataset.s = s;
    el.textContent = text || "";
    el.title = detail || "";
  }

  editor.addEventListener("input", () => {
    if (!doc) return;
    if (!isDirty()) { clearTimeout(saveTimer); doc.dirtySince = 0; setState("saved", "Saved"); return; }
    if (doc.json) {
      const problem = jsonProblem(editor.value);
      if (problem) { clearTimeout(saveTimer); setState("invalid", "Invalid JSON \u00b7 not saved", problem); return; }
    }
    if (saveState !== "error") setState("dirty", "Edited");
    schedule();
  });

  // indent with spaces inside JSON
  editor.addEventListener("keydown", (ev) => {
    if (ev.key === "Tab" && doc && doc.json && !ev.ctrlKey && !ev.metaKey && !ev.altKey) {
      ev.preventDefault();
      document.execCommand("insertText", false, "  ");
    }
  });

  // ---------- list actions ----------
  function download(name) {
    const a = document.createElement("a");
    a.href = fileUrl(name, "&download=1");
    a.download = baseName(name);
    document.body.appendChild(a);
    a.click();
    a.remove();
  }

  async function remove(name) {
    let msg = "Delete " + baseName(name) + "? This can't be undone.";
    if (SYSTEM_FILES.includes(name)) msg = baseName(name) + " holds device settings. The device goes back to defaults if it's deleted.\n\n" + msg;
    else if (name === listing.active) msg = baseName(name) + " is the file open on the device.\n\n" + msg;
    if (!confirm(msg)) return;

    try {
      await api("DELETE", fileUrl(name));
      toast("Deleted " + baseName(name));
    } catch (e) {
      toast("Couldn't delete: " + e.message);
    }
    refresh();
  }

  // ---------- wiring ----------
  $("btnBack").onclick = () => closeFile(false);

  document.addEventListener("keydown", (ev) => {
    if (!doc) return;
    const mod = ev.ctrlKey || ev.metaKey;
    if (mod && ev.key.toLowerCase() === "s") { ev.preventDefault(); save(); }
    else if (ev.key === "Escape") { ev.preventDefault(); closeFile(false); }
  });

  // browser back / forward moves between the list and the editor
  window.addEventListener("popstate", (ev) => {
    const file = ev.state && ev.state.file;
    if (file && (!doc || doc.name !== file)) openFile(file, true);
    else if (!file && doc) closeFile(true);
  });

  // flush when the tab goes to the background
  document.addEventListener("visibilitychange", () => { if (document.visibilityState === "hidden" && isDirty()) save(); });

  // last chance when the page closes - keepalive requests are limited to 64KB
  window.addEventListener("pagehide", () => {
    if (!isDirty() || inflight) return;
    const text = editor.value;
    if (doc.json && jsonProblem(text)) return;
    const blob = new Blob([text], { type: "text/plain" });
    if (blob.size > 60000) return;
    try {
      fetch(fileUrl(doc.name), { method: "POST", body: blob, keepalive: true, headers: uploadHeaders(doc.name) });
    } catch (e) {}
  });
  window.addEventListener("beforeunload", (ev) => {
    if (isDirty() || inflight) { ev.preventDefault(); ev.returnValue = ""; }
  });

  // ---------- start: always on the file list ----------
  history.replaceState(null, "", location.pathname);
  refresh();
})();
</script>
</body>
</html>
)rawliteral";
