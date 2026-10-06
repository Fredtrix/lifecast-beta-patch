// racepresets.js
// Lifecast - the race's preset faces, where RaceMenu's "Face Presets" mode
// used to send the player (D125).
//
// This is NOT the Presets section: that one is the player's own .jslot files
// (presets.js, the quick button on the belt). This one is the ten or so faces
// the RACE record ships for the player's race and sex. Same word, different
// thing - the DLL calls them race presets for that reason.
//
// Browsing is free and applying is not: a preset is one full redraw of the
// actor, so moving the list sends nothing and only Apply does.
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
// of these, so the list opens at 0 and means "the one Apply would use", never
// "the one you are wearing".
"use strict";

(() => {
  let count = 0;
  let names = [];

  const section = LifecastControls.section("Face Presets");

  const row = LifecastControls.row({
    label: "Preset",
    min: 0,
    max: 0,
    step: 1,
    button: 1,
    format: (v) => String(v) + " / " + String(count - 1),
    // Browsing is local: the DLL hears nothing until Apply.
    send: () => showName(),
  });
  const nameEl = LifecastControls.el("div", "part-name");
  row.el.appendChild(nameEl);
  section.body.appendChild(row.el);

  const bar = LifecastControls.el("div", "preset-bar");
  const apply = LifecastControls.el("button", "panel-button", "Apply");
  apply.type = "button";
  apply.tabIndex = -1;
  apply.title = "Applies this preset face to your character";
  bar.appendChild(apply);
  const status = LifecastControls.el("div", "preset-status", "--");
  bar.appendChild(status);
  section.body.appendChild(bar);

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
  }

  apply.addEventListener("click", () => {
    if (count === 0) {
      return;
    }
    const i = index();
    status.textContent = "applying " + labelFor(i) + "...";
    LifecastBridge.send("race-preset-apply", { i: i });
  });

  LifecastBridge.on("race-presets", (data) => {
    count = data && typeof data.count === "number" ? data.count : 0;
    names = data && Array.isArray(data.names) ? data.names : [];
    // A race with no preset faces is a real case, not an error. The ROW goes
    // (as look.js does with an empty list) and the section stays: the belt
    // decides whether its button exists by whether the section is in the DOM
    // (belt.js), so hiding the section would leave a button that leads
    // nowhere.
    row.el.hidden = count === 0;
    apply.disabled = count === 0;
    if (count === 0) {
      row.set(null);
      nameEl.textContent = "";
      status.textContent = "this race has no preset faces";
      return;
    }
    row.setRange(0, count - 1);
    if (!row.dragging()) {
      row.set(Math.min(index(), count - 1));
    }
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

  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("race-presets-get");
    }
  });
})();
