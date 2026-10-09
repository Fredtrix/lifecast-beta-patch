// racepresets.js
// Lifecast - the race's preset faces, where RaceMenu's "Face Presets" mode
// used to send the player (D125).
//
// This is NOT the Presets section: that one is the player's own .jslot files
// (presets.js, the quick button on the belt). This one is the ten or so faces
// the RACE record ships for the player's race and sex. Same word, different
// thing - the DLL calls them race presets for that reason.
//
// Every step applies, as in RaceMenu: moving the slider, pressing an arrow or
// picking a row each send the preset the list now rests on. A preset is one
// full redraw of the actor, so a drag across the whole list can send as many
// redraws as the list has steps. That is the behaviour asked for, and nothing
// here holds a step back (no debounce, no throttle, no "only the last one of
// the drag"): a stall during a drag is a number to measure, not something to
// hide. The one thing skipped is a step that lands on the preset the actor
// already wears, which is not a step; an edit to the face (see faceTouched)
// ends that, so the same preset can be chosen again to start over from it.
//
// The list shows "Preset 1", "Preset 2", ... in the order the game returns,
// and NOT what the DLL sends as the name. Every preset's full name is
// "Prisoner", and GetFormEditorID() comes back empty at runtime, so the DLL
// has nothing better than the FormID in hex - and a hex FormID on a player's
// list is the kind of thing the Lifecast exists not to do. The DLL's name is
// kept as the row's tooltip, where it costs a player nothing and still
// answers "which record is this?" for whoever is measuring.
//
// "Preset N" is PROVISIONAL, not a design: the real label (the editor ID read
// from the plugin, or a thumbnail) is a Fase 8 item in the BACKLOG.
//
// There is no "current" preset: a face the player has been editing is not one
// of these, so the list opens at 0 and means "the first one a step would
// apply", never "the one you are wearing".
"use strict";

(() => {
  let count = 0;
  let names = [];
  // The last index sent, so a step onto the same preset sends nothing. Cleared
  // when the race's list changes: index 3 of another race is another face.
  let applied = null;
  let listKey = "";
  // The portrait of the face taken before the first preset of the gesture in
  // progress (F27), handed to the history entry when the gesture ends.
  let gesture = null;

  const section = LifecastControls.section("Face Presets");

  const row = LifecastControls.row({
    label: "Preset",
    min: 0,
    max: 0,
    step: 1,
    button: 1,
    format: (v) => String(v) + " / " + String(count - 1),
    // Arrows, list rows and the end of a drag all arrive here; the drag
    // itself is also heard below, step by step.
    send: () => step(),
    // A race preset rewrites the whole face, so the generic entry (the older
    // INDEX, replayed) would redraw the older preset over every edit made
    // since. The entry is a portrait instead: see entryFor().
    portrait: false,
    entry: (before, after) => entryFor(before, after),
  });
  const list = LifecastList.create({ row: row });
  const nameEl = LifecastControls.el("div", "part-name");
  row.el.appendChild(nameEl);
  section.body.appendChild(row.el);

  const status = LifecastControls.el("div", "preset-status", "--");
  section.body.appendChild(status);

  LifecastControls.mount(section, "face:RacePresets");

  function index() {
    return Number(row.input.value);
  }

  // The label is positional, not read from the record: "Preset N" where N is
  // the 1-based place in the order the DLL sent. The slider's own value stays
  // 0-based, because that is the index that goes to the DLL and appears in the
  // log - so a reader comparing the panel to Lifecast.log sees 1-based here
  // and 0-based there, ON PURPOSE. What the DLL called the preset goes to the
  // tooltip, so the FormID is still reachable without being on the player's
  // face.
  function labelFor(i) {
    return "Preset " + String(i + 1);
  }

  function showName() {
    const i = index();
    if (names[i] === undefined && i >= count) {
      nameEl.textContent = "";
      nameEl.title = "";
      return;
    }
    nameEl.textContent = labelFor(i);
    nameEl.title = names[i] === undefined ? "" : names[i];
    list.setCurrent(i);
  }

  // One step: show where the list rests and send that preset, once per
  // distinct preset. Sends are never delayed or merged.
  function step() {
    showName();
    if (count === 0) {
      return;
    }
    const i = index();
    if (i === applied) {
      return;
    }
    if (gesture === null) {
      gesture = LifecastControls.portrait();
    }
    applied = i;
    status.textContent = "applying " + labelFor(i) + "...";
    LifecastBridge.send("race-preset-apply", { i: i });
  }

  // The history entry for one finished gesture on the Preset row. Undo puts
  // the portrait back (and the row on the index it held) and sends no preset;
  // redo sends the preset again, which is a pure function of the index, on
  // the very face the undo left (the stack hands redo back only that face).
  // The portrait lives in the entry, not in history.js: the history keeps its
  // one-control contract, and an entry may hold whatever apply() understands.
  function entryFor(before, after) {
    const portrait = gesture;
    gesture = null;
    if (portrait === null) {
      return null;
    }
    return {
      label: "Preset",
      before: { index: before, portrait: portrait },
      after: after,
      describe: (v) =>
        typeof v === "number" ? v : { index: v.index, portrait: v.portrait.rows.length + " rows" },
      apply: (v) => {
        if (typeof v === "number") {
          row.set(v);
          showName();
          applied = v;
          status.textContent = "applying " + labelFor(v) + "...";
          LifecastBridge.send("race-preset-apply", { i: v });
          return;
        }
        row.set(v.index);
        showName();
        applied = null;
        const n = LifecastControls.restorePortrait(v.portrait);
        status.textContent = "undone: " + String(n) + " values put back";
      },
    };
  }

  // The range reports every step of a drag; the row's own send is paced for
  // sliders that stream a value, and a preset is not a value to stream.
  row.input.addEventListener("input", step);

  // An edit anywhere else puts the face off every preset, so the preset the
  // list rests on is no longer the one worn: going back to it must send again.
  // Heard at the document, in the capture phase, so no control has to opt in:
  // sliders, lists, swatches and Undo/Redo all count. Search and the belt do
  // not edit the face and are left out.
  function faceTouched(e) {
    const t = e.target;
    if (!t || !t.closest) {
      return;
    }
    const own = row.el.closest(".section");
    if (own && own.contains(t)) {
      return;
    }
    if (t.closest("#sections") || t.closest("#undo, #redo, #undoall")) {
      applied = null;
    }
  }
  ["input", "change", "click"].forEach((n) => document.addEventListener(n, faceTouched, true));
  LifecastBridge.on("race-presets", (data) => {
    count = data && typeof data.count === "number" ? data.count : 0;
    names = data && Array.isArray(data.names) ? data.names : [];
    // A race with no preset faces is a real case, not an error. The ROW goes
    // (as look.js does with an empty list) and the section stays: the belt
    // decides whether its button exists by whether the section is in the DOM
    // (belt.js), so hiding the section would leave a button that leads
    // nowhere.
    row.el.dataset.empty = count === 0 ? "1" : ""; // the search respects it (F4)
    row.el.hidden = count === 0;
    const key = names.join("|");
    if (key !== listKey) {
      listKey = key;
      applied = null;
    }
    if (count === 0) {
      row.set(null);
      list.setItems([], 0);
      nameEl.textContent = "";
      status.textContent = "this race has no preset faces";
      return;
    }
    row.setRange(0, count - 1);
    if (!row.dragging()) {
      row.set(Math.min(index(), count - 1));
    }
    if (data.sig) {
      LifecastList.verify("race-presets", names.map((n) => [n]), data.sig);
    }
    list.setItems(names.map((n, i) => ({ name: labelFor(i), title: n })), Math.min(index(), count - 1));
    showName();
  });

  LifecastBridge.on("race-preset-applied", (data) => {
    const ok = !!(data && data.ok);
    const why = data && typeof data.why === "string" ? data.why : "";
    // On success the DLL's "why" is the preset's name - the hex FormID again -
    // so the status is built from the echoed index instead. On failure "why"
    // is a sentence and is shown as it comes.
    const i = data && typeof data.i === "number" ? data.i : index();
    status.textContent = ok ? "applied " + labelFor(i) : "not applied" + (why ? " - " + why : "");
  });

  // The player chose a preset on RaceMenu's own slider while the panel was
  // away: the face already shows it, so the row rests on it too. Nothing is
  // sent back - the preset is already worn.
  LifecastBridge.on("race-preset-native", (data) => {
    const i = data && typeof data.i === "number" ? data.i : -1;
    if (i < 0 || i >= count) {
      return;
    }
    row.set(i);
    applied = i;
    showName();
    status.textContent = "RaceMenu is on " + labelFor(i);
  });

  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("race-presets-get");
    }
  });
})();
