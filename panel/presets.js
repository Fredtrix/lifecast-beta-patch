// presets.js
// Atelier - the presets section: the presets on disk, searched and sorted
// here, a preset to load, and a name to save the current character under.
//
// The DLL sends the whole list once (and again when the panel comes back on
// screen, or after a save); search and sort run over that copy, so typing
// sends nothing across the bridge. Whether each file reads as a preset
// arrives later, as a second message, and marks the rows that do not - a
// bad file never costs the list.
//
// Loading and saving are RaceMenu's, asked for from here: Load applies the
// selected preset whole, Save writes the character under the field's name.
// A load the DLL gave up on can still answer later ("late"), and that later
// answer is the one that describes the character.
// Saving over an existing preset can be refused by the DLL when the overwrite
// would destroy parts of that file the live character was not given here;
// the page then lists them and offers a new name or RaceMenu's own save.
// An overwrite the DLL allows is previewed first: when the file would gain or
// lose entries (a skin override left by an earlier load, say), the page shows
// both, with the same weight, and the file is written only on "Save anyway".
//
// ONE text field, on purpose: it searches the list, and its text is also the
// name the Save button uses. The page keeps a single text field so that the
// keyboard corrections in keyboard.js have one case to cover, not two.
"use strict";

(() => {
  // A second press within this time confirms overwriting a preset that
  // already has the name.
  const CONFIRM_MS = 3000;

  // The longest preset name the DLL accepts.
  const NAME_MAX = 64;

  let items = [];          // { name, time, size, bad: null | why }
  let sortBy = "name";     // "name" | "time"
  let selected = null;     // name of the selected row
  let confirmUntil = 0;
  let notice = "";         // the last outcome, kept across re-renders
  let loading = false;     // a load was asked and has not answered yet

  const section = AtelierControls.section("Presets");

  const field = AtelierControls.el("input", "preset-field");
  field.type = "search";
  field.placeholder = "Search, or a name to save as";
  field.spellcheck = false;
  field.autocomplete = "off";

  const bar = AtelierControls.el("div", "preset-bar");
  const byName = AtelierControls.el("button", "panel-button", "A-Z");
  const byTime = AtelierControls.el("button", "panel-button", "Newest");
  const load = AtelierControls.el("button", "panel-button preset-load", "Load");
  load.title = "Applies the selected preset";
  const save = AtelierControls.el("button", "panel-button preset-save", "Save");
  for (const b of [byName, byTime, load, save]) {
    b.type = "button";
    b.tabIndex = -1;
    bar.appendChild(b);
  }

  const status = AtelierControls.el("div", "preset-status", "--");

  // Shown only when RaceMenu did not save or load at the panel's request (an
  // older RaceMenu, or the call failing): its own Presets page does both, one
  // click away. The list, search and sort stay here either way.
  const fallback = AtelierControls.el("div", "delegate preset-fallback");
  const fallbackText = AtelierControls.el("div", "delegate-text");
  const fallbackTitle = AtelierControls.el("span", "slider-name", "");
  const fallbackNote = AtelierControls.el("span", "delegate-note", "");
  fallbackText.appendChild(fallbackTitle);
  fallbackText.appendChild(fallbackNote);
  const fallbackGo = AtelierControls.el("button", "panel-button", "Open");
  fallbackGo.type = "button";
  fallbackGo.tabIndex = -1;
  fallbackGo.title = "Opens RaceMenu (F4 comes back)";
  fallbackGo.addEventListener("click", () => AtelierBridge.send("show-racemenu", { for: "presets" }));
  fallback.appendChild(fallbackText);
  fallback.appendChild(fallbackGo);
  fallback.hidden = true;

  // Shown when the DLL refused to save over an existing preset: what the
  // overwrite would have destroyed, and the two ways on.
  const guard = AtelierControls.el("div", "delegate preset-guard");
  const guardText = AtelierControls.el("div", "delegate-text");
  const guardTitle = AtelierControls.el("span", "slider-name", "");
  const guardNote = AtelierControls.el("span", "delegate-note", "");
  guardText.appendChild(guardTitle);
  guardText.appendChild(guardNote);
  const guardRename = AtelierControls.el("button", "panel-button", "New name");
  guardRename.title = "Keeps the existing preset and saves under a free name";
  const guardRaceMenu = AtelierControls.el("button", "panel-button", "RaceMenu");
  guardRaceMenu.title = "Opens RaceMenu, whose own save replaces the file (F4 comes back)";
  for (const b of [guardRename, guardRaceMenu]) {
    b.type = "button";
    b.tabIndex = -1;
  }
  guardRaceMenu.addEventListener("click", () => AtelierBridge.send("show-racemenu", { for: "presets" }));
  guard.appendChild(guardText);
  guard.appendChild(guardRename);
  guard.appendChild(guardRaceMenu);
  guard.hidden = true;

  // Shown when an overwrite would change what the existing file holds: what it
  // would gain and what it would lose, compared entry by entry by the DLL.
  const preview = AtelierControls.el("div", "delegate preset-preview");
  const previewText = AtelierControls.el("div", "delegate-text");
  const previewTitle = AtelierControls.el("span", "slider-name", "");
  const previewGain = AtelierControls.el("span", "delegate-note", "");
  const previewLoss = AtelierControls.el("span", "delegate-note", "");
  // What the two lists cover, said in the box: a list the player reads as
  // complete is worse than no list.
  const previewScope = AtelierControls.el("span", "delegate-note", "");
  previewText.appendChild(previewTitle);
  previewText.appendChild(previewGain);
  previewText.appendChild(previewLoss);
  previewText.appendChild(previewScope);
  const previewSave = AtelierControls.el("button", "panel-button", "Save anyway");
  previewSave.title = "Writes the preset exactly as compared here";
  const previewCancel = AtelierControls.el("button", "panel-button", "Cancel");
  previewCancel.title = "Keeps the existing preset as it is";
  for (const b of [previewSave, previewCancel]) {
    b.type = "button";
    b.tabIndex = -1;
  }
  preview.appendChild(previewText);
  preview.appendChild(previewSave);
  preview.appendChild(previewCancel);
  preview.hidden = true;
  let previewName = null;  // the name the shown preview is for
  let reveal = null;       // a box just shown, brought into view by the next render

  const list = AtelierControls.el("ul", "preset-list");

  section.appendChild(field);
  section.appendChild(bar);
  section.appendChild(status);
  section.appendChild(fallback);
  section.appendChild(guard);
  section.appendChild(preview);
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

  // What a refused overwrite would have destroyed, in the player's words.
  const LOSS_WORDS = {
    sculpt: () => "sculpt",
    custom: (n) => n + " extended sliders",
    bodyMorphs: (n) => n + " body morphs",
    transforms: (n) => n + " node transforms",
    overrides: (n) => n + " overlays",
    skinOverrides: (n) => n + " skin overrides",
  };

  function lossText(loss) {
    return loss
      .map((l) => (LOSS_WORDS[l.section] || ((n) => n + " " + l.section))(l.count))
      .join(", ");
  }

  // What an overwrite would gain or lose, entry by entry, in the player's words.
  const ENTRY_WORDS = {
    sculpt: ["sculpted mesh", "sculpted meshes"],
    custom: ["extended slider", "extended sliders"],
    bodyMorphs: ["body morph value", "body morph values"],
    transforms: ["node transform value", "node transform values"],
    overrides: ["overlay value", "overlay values"],
    skinOverrides: ["skin override", "skin overrides"],
    headParts: ["head part", "head parts"],
    tintInfo: ["tint layer", "tint layers"],
    faceTextures: ["face texture", "face textures"],
  };

  function entryText(diff, side) {
    return diff
      .filter((d) => d[side] > 0)
      .map((d) => {
        const w = ENTRY_WORDS[d.section] || [d.section + " entry", d.section + " entries"];
        return d[side] + " " + (d[side] === 1 ? w[0] : w[1]);
      })
      .join(", ");
  }

  function entryLabels(diff, side) {
    const key = side === "gained" ? "gainedLabels" : "lostLabels";
    return diff
      .filter((d) => Array.isArray(d[key]) && d[key].length)
      .map((d) => d.section + ": " + d[key].join("; "))
      .join("\n");
  }

  function hidePreview(cancel) {
    if (cancel && previewName !== null) {
      AtelierBridge.send("preset-save-cancel", { name: previewName });
    }
    previewName = null;
    preview.hidden = true;
  }

  // The first "<name> 2", "<name> 3"... that no preset uses and that stays
  // within the length the DLL accepts.
  function freeName(name) {
    for (let i = 2; i < 1000; i++) {
      const suffix = " " + i;
      const candidate = name.slice(0, NAME_MAX - suffix.length).trimEnd() + suffix;
      if (!items.some((it) => same(it.name, candidate))) {
        return candidate;
      }
    }
    return name;
  }

  function showFallback(what) {
    fallbackTitle.textContent = what === "load" ? "Load in RaceMenu" : "Save in RaceMenu";
    fallbackNote.textContent = what === "load"
      ? "RaceMenu did not load from here. Its own Presets page loads the same files."
      : "RaceMenu did not save from here. Its own Presets page saves the same files.";
    fallback.hidden = false;
    reveal = fallback;
  }

  // A box the player has to read or answer is on screen when it appears,
  // wherever the panel is scrolled: the boxes sit in this section, which
  // scrolls with the panel body. The body scrolls just enough to show the
  // whole box, or its top when it is taller than the body.
  function bringIntoView(box) {
    const body = box.closest(".panel-body");
    if (!body || box.hidden) {
      return;
    }
    const b = body.getBoundingClientRect();
    const r = box.getBoundingClientRect();
    if (r.top < b.top) {
      body.scrollTop += r.top - b.top;
    } else if (r.bottom > b.bottom) {
      body.scrollTop += Math.min(r.bottom - b.bottom, r.top - b.top);
    }
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

    const pick = selected === null ? undefined : items.find((it) => same(it.name, selected));
    load.disabled = loading || !pick || pick.bad !== null;

    if (reveal !== null) {
      bringIntoView(reveal);
      reveal = null;
    }
  }

  field.addEventListener("input", () => {
    confirmUntil = 0;
    notice = "";
    guard.hidden = true;
    hidePreview(true);
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

  load.addEventListener("click", () => {
    if (loading || selected === null) {
      return;
    }
    loading = true;
    guard.hidden = true;
    fallback.hidden = true;
    notice = "loading " + selected + "...";
    render();
    AtelierBridge.send("preset-load", { name: selected });
  });

  guardRename.addEventListener("click", () => {
    field.value = freeName(field.value.trim());
    guard.hidden = true;
    confirmUntil = 0;
    notice = "";
    render();
    field.focus();
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
    guard.hidden = true;
    hidePreview(false);
    fallback.hidden = true;
    notice = "saving " + name + "...";
    render();
    AtelierBridge.send("preset-save", { name: name, overwrite: exists });
  });

  previewSave.addEventListener("click", () => {
    if (previewName === null) {
      return;
    }
    const name = previewName;
    hidePreview(false);
    notice = "saving " + name + "...";
    render();
    AtelierBridge.send("preset-save", { name: name, overwrite: true, confirm: true });
  });

  previewCancel.addEventListener("click", () => {
    hidePreview(true);
    notice = "not saved";
    render();
  });

  AtelierBridge.on("preset-save-preview", (data) => {
    const diff = data && Array.isArray(data.diff) ? data.diff : [];
    const gain = entryText(diff, "gained");
    const loss = entryText(diff, "lost");
    previewName = String(data && data.name);
    previewTitle.textContent = "Save over " + previewName + "?";
    previewGain.textContent = "It would GAIN: " + (gain || "nothing") + ".";
    previewGain.title = entryLabels(diff, "gained");
    previewLoss.textContent = "It would LOSE: " + (loss || "nothing") + ".";
    previewLoss.title = entryLabels(diff, "lost");
    previewScope.textContent = "Compared: " + Object.keys(ENTRY_WORDS).map((k) => ENTRY_WORDS[k][1]).join(", ") +
      ". A value that only changed is an edit, and is not listed.";
    preview.hidden = false;
    reveal = preview;
    notice = "not saved yet - see what it would change";
    render();
  });

  // The DLL ended the preview on its side: a load, the menu closing, the
  // preset changing on disk, another save. The box goes whatever name it
  // shows - the DLL holds one preview at most, and a box it does not hold
  // must not be clickable. The notice changes only when a box was showing:
  // a drop the page caused itself (Cancel, a new save) already said why.
  AtelierBridge.on("preset-save-preview-dropped", (data) => {
    const showing = previewName !== null;
    hidePreview(false);
    if (showing) {
      notice = "not saved: " + (data && data.why ? data.why : "the preview ended") +
        " - save again to see what it would change";
    }
    render();
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

  // The menu closed: what this section showed during that menu session ends
  // with it - the DLL has already let go of what those boxes stood for. F4
  // does not send this; the boxes stay for the player who comes back.
  AtelierBridge.on("menu-closed", () => {
    guard.hidden = true;
    fallback.hidden = true;
    hidePreview(false);
    confirmUntil = 0;
    notice = "";
    reveal = null;
    render();
  });

  // Every answer names its preset. Only an answer about the name the box
  // shows ends the box: an answer about another name says nothing about the
  // preview the DLL still holds, and hiding its box would leave that preview
  // pending with nothing on screen to agree or cancel.
  AtelierBridge.on("preset-saved", (data) => {
    if (previewName !== null && data && same(String(data.name), previewName)) {
      hidePreview(false);
    }
    if (data && data.ok) {
      selected = data.name;
      notice = "saved " + data.name;
    } else if (data && Array.isArray(data.loss)) {
      // Refused by the overwrite guard: the existing file is kept as it was.
      notice = "not saved";
      guardTitle.textContent = "Keep " + data.name + "?";
      guardNote.textContent = data.loss.length
        ? "Saving over it would lose what was not loaded here: " + lossText(data.loss) + "."
        : "Saving over it is not safe: " + (data.why || "unknown reason") + ".";
      guard.hidden = false;
      reveal = guard;
    } else {
      notice = "not saved: " + (data && data.why ? data.why : "unknown reason");
      if (data && data.fallback) {
        showFallback("save");
      }
    }
    render();
  });

  AtelierBridge.on("preset-loaded", (data) => {
    loading = false;
    if (data && data.ok) {
      // A late answer overrides the "not loaded" shown at the timeout.
      selected = data.name;
      notice = "loaded " + data.name + (data.late ? " (late, after " + data.ms + " ms)" : "");
      fallback.hidden = true;
    } else {
      notice = "not loaded: " + (data && data.why ? data.why : "unknown reason");
      if (data && data.fallback) {
        showFallback("load");
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
