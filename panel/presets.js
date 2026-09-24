// presets.js
// Atelier - the presets section: the presets on disk, searched and sorted
// here, and a name to save the current character under.
//
// The DLL sends the whole list once (and again when the panel comes back on
// screen, or after a save); search and sort run over that copy, so typing
// sends nothing across the bridge. Whether each file reads as a preset
// arrives later, as a second message, and marks the rows that do not - a
// bad file never costs the list.
//
// ONE text field, on purpose: it searches the list, and its text is also the
// name the Save button uses. The page keeps a single text field so that the
// keyboard corrections in keyboard.js have one case to cover, not two.
"use strict";

(() => {
  // A second press within this time confirms overwriting a preset that
  // already has the name.
  const CONFIRM_MS = 3000;

  let items = [];          // { name, time, size, bad: null | why }
  let sortBy = "name";     // "name" | "time"
  let selected = null;     // name of the selected row
  let confirmUntil = 0;
  let notice = "";         // the last save's outcome, kept across re-renders

  const section = AtelierControls.section("Presets");

  const field = AtelierControls.el("input", "preset-field");
  field.type = "search";
  field.placeholder = "Search, or a name to save as";
  field.spellcheck = false;
  field.autocomplete = "off";

  const bar = AtelierControls.el("div", "preset-bar");
  const byName = AtelierControls.el("button", "panel-button", "A-Z");
  const byTime = AtelierControls.el("button", "panel-button", "Newest");
  const save = AtelierControls.el("button", "panel-button preset-save", "Save");
  for (const b of [byName, byTime, save]) {
    b.type = "button";
    b.tabIndex = -1;
    bar.appendChild(b);
  }

  const status = AtelierControls.el("div", "preset-status", "--");

  // Shown only when RaceMenu did not save at the panel's request (an older
  // RaceMenu, or the call failing): saving is RaceMenu's own, one click away.
  // The list, search and sort stay here either way.
  const fallback = AtelierControls.el("div", "delegate preset-fallback");
  const fallbackText = AtelierControls.el("div", "delegate-text");
  fallbackText.appendChild(AtelierControls.el("span", "slider-name", "Save in RaceMenu"));
  fallbackText.appendChild(AtelierControls.el("span", "delegate-note",
    "RaceMenu did not save from here. Its own Presets page saves the same files."));
  const fallbackGo = AtelierControls.el("button", "panel-button", "Open");
  fallbackGo.type = "button";
  fallbackGo.tabIndex = -1;
  fallbackGo.title = "Opens RaceMenu (F4 comes back)";
  fallbackGo.addEventListener("click", () => AtelierBridge.send("show-racemenu", { for: "presets" }));
  fallback.appendChild(fallbackText);
  fallback.appendChild(fallbackGo);
  fallback.hidden = true;
  const list = AtelierControls.el("ul", "preset-list");

  section.appendChild(field);
  section.appendChild(bar);
  section.appendChild(status);
  section.appendChild(fallback);
  section.appendChild(list);
  AtelierControls.mount(section);

  function same(a, b) {
    return a.toLowerCase() === b.toLowerCase();
  }

  function dateText(seconds) {
    if (!seconds) {
      return "";
    }
    const d = new Date(seconds * 1000);
    const pad = (n) => String(n).padStart(2, "0");
    return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate());
  }

  function render() {
    const query = field.value.trim().toLowerCase();
    const shown = items.filter((it) => query === "" || it.name.toLowerCase().includes(query));
    shown.sort(sortBy === "time"
      ? (a, b) => b.time - a.time || a.name.localeCompare(b.name)
      : (a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: "base" }));

    byName.classList.toggle("is-on", sortBy === "name");
    byTime.classList.toggle("is-on", sortBy === "time");

    list.textContent = "";
    for (const it of shown) {
      const li = AtelierControls.el("li", "preset-row");
      li.appendChild(AtelierControls.el("span", "preset-name", it.name));
      li.appendChild(AtelierControls.el("span", "preset-date", it.bad ? "unreadable" : dateText(it.time)));
      if (it.bad) {
        li.classList.add("preset-bad");
        li.title = it.bad;
      }
      if (selected !== null && same(selected, it.name)) {
        li.classList.add("is-selected");
      }
      li.addEventListener("click", () => {
        selected = it.name;
        render();
      });
      list.appendChild(li);
    }

    const bad = items.filter((it) => it.bad).length;
    status.textContent = items.length === 0
      ? "No presets in RaceMenu's preset folder."
      : shown.length + " of " + items.length + " presets" + (bad ? ", " + bad + " unreadable" : "");
    if (notice) {
      status.textContent += " - " + notice;
    }

    save.disabled = field.value.trim() === "";
    save.textContent = Date.now() < confirmUntil ? "Overwrite?" : "Save";
  }

  field.addEventListener("input", () => {
    confirmUntil = 0;
    notice = "";
    render();
  });
  byName.addEventListener("click", () => {
    sortBy = "name";
    render();
  });
  byTime.addEventListener("click", () => {
    sortBy = "time";
    render();
  });

  save.addEventListener("click", () => {
    const name = field.value.trim();
    if (name === "") {
      return;
    }
    const exists = items.some((it) => same(it.name, name));
    if (exists && Date.now() >= confirmUntil) {
      confirmUntil = Date.now() + CONFIRM_MS;
      render();
      setTimeout(render, CONFIRM_MS + 50);
      return;
    }
    confirmUntil = 0;
    notice = "saving " + name + "...";
    render();
    AtelierBridge.send("preset-save", { name: name, overwrite: exists });
  });

  AtelierBridge.on("presets", (data) => {
    const incoming = data && Array.isArray(data.items) ? data.items : [];
    items = incoming.map((it) => ({ name: String(it.name), time: Number(it.time) || 0, size: it.size, bad: null }));
    render();
  });

  AtelierBridge.on("presets-checked", (data) => {
    const bad = data && Array.isArray(data.bad) ? data.bad : [];
    for (const b of bad) {
      const it = items.find((x) => x.name === b.name);
      if (it) {
        it.bad = String(b.why || "unreadable");
      }
    }
    render();
  });

  AtelierBridge.on("preset-saved", (data) => {
    if (data && data.ok) {
      selected = data.name;
      notice = "saved " + data.name;
    } else {
      notice = "not saved: " + (data && data.why ? data.why : "unknown reason");
      if (data && data.fallback) {
        fallback.hidden = false;
      }
    }
    render();
  });

  AtelierBridge.whenAttached((ok) => {
    if (ok) {
      AtelierBridge.send("presets-get");
    }
  });

  render();
})();
