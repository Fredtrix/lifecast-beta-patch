// delegate.js
// Atelier - the "In RaceMenu" section: what this panel hands back to RaceMenu.
//
// Two kinds of row live here, and they arrive from opposite directions.
//
// The FIXED rows below are deliberate handovers, each with its reason in D027.
// Two of them are RaceMenu *modes* rather than categories (Face Presets,
// Sculpt), so they have no entry in the category list at all and could never
// come from the list.
//
// The rest arrive FROM THE GAME. RaceMenu's category list belongs to the
// player, not to us: mods inject categories into it, so which ones exist is
// not knowable from here. The DLL reads the live list and hands it over
// (D082), and every entry this build does not cover gets a door - which is
// what makes "if RaceMenu has a tab, it shows up in the Atelier" true on a
// machine this project has never seen.
"use strict";

(() => {
  const ENTRIES = [
    ["race-sex", "Race & Sex", "Changing race or sex rebuilds every list here."],
    ["presets", "Face Presets", "The race's preset faces."],
    ["face-sliders", "Extended Face Sliders", "RaceMenu's own face sliders, beyond the game's."],
    ["expressions", "Expressions", "The face expression sliders."],
    ["paints", "War Paint Catalog", "Every war paint installed. The Atelier offers the race's own."],
    ["sculpt", "Sculpt", "Free-form editing of the head mesh."],
  ];

  // Categories that already have a home here, matched by the category's own
  // label - and "a home" is a section OR a fixed row above. $EXTRA and
  // $EXPRESSIONS have a row each. The four overlay categories have sections
  // of their own (overlays.js). $MAKEUP is war paint, which is a tint mask
  // and not an overlay: its home is the War Paint layer in Skin & Tints, and
  // the row above is the door to the full catalog, which that layer does not
  // carry (D084).
  //
  // Matched by label and never by flag. AddCategory registers a category by
  // text key and AddSliderEx defaults its section to 0, so 0 is a real value
  // that every text-keyed category shares - matching on it would stack a
  // second body-scales tab on top of the one that exists, silently (D082).
  //
  // A label is not stable: two installed mods ship the same script with
  // different category labels, and the game's translations change the ones
  // written with a $ prefix. When a label moves, the category stops being
  // recognised and becomes a door - never a duplicate tab, never a lost one.
  // The failure falls on the safe side on purpose.
  const COVERED = [
    "$ALL",  // not a category: it carries every category bit at once (2044)
    "RACE",
    "BODY", "HEAD", "FACE", "EYES", "BROW", "MOUTH", "HAIR",
    "$EXTRA", "$EXPRESSIONS",
    "$COLORS",
    "$MAKEUP", "$BODY PAINT", "$HAND PAINT", "$FOOT PAINT", "$FACE PAINT",
    "$BODY SCALES",
  ];

  const section = AtelierControls.section("In RaceMenu");

  // live marks a row built from the category list, so a second list replaces
  // those rows instead of stacking another copy of them under the fixed ones.
  function addRow(key, label, note, live) {
    const row = AtelierControls.el("div", "delegate");
    if (live) {
      row.dataset.live = "1";
    }
    const text = AtelierControls.el("div", "delegate-text");
    text.appendChild(AtelierControls.el("span", "slider-name", label));
    text.appendChild(AtelierControls.el("span", "delegate-note", note));
    const go = AtelierControls.el("button", "panel-button", "Open");
    go.type = "button";
    go.tabIndex = -1;
    go.title = "Opens RaceMenu (F4 comes back)";
    go.addEventListener("click", () => AtelierBridge.send("show-racemenu", { for: key }));
    row.appendChild(text);
    row.appendChild(go);
    section.body.appendChild(row);
  }

  for (const [key, label, note] of ENTRIES) {
    addRow(key, label, note, false);
  }
  AtelierControls.mount(section, "delegate");

  AtelierBridge.on("racemenu-categories", (data) => {
    // ready says the list was read. A false one is a bad read, not an empty
    // list, and the doors already on screen are better than none: leave them.
    if (!data || !data.ready || !Array.isArray(data.entries)) {
      return;
    }
    for (const old of section.body.querySelectorAll("[data-live]")) {
      old.remove();
    }
    for (const entry of data.entries) {
      if (typeof entry.text !== "string" || entry.text === "" || COVERED.indexOf(entry.text) >= 0) {
        continue;
      }
      // A category a mod registered carries its own key, and morphs.js builds
      // a section for every one of those - empty when the mod registered no
      // sliders this time. So a keyed category already has a home and must
      // not get a door as well. The film's own categories have no key and are
      // covered by label above; the one keyed category the list above names is
      // $BODY SCALES, which has a section of its own.
      if (typeof entry.key === "string" && entry.key !== "") {
        continue;
      }
      // The label goes back as the key: Select() presses the entry whose label
      // is exactly this one, so no list of keys has to be kept on both sides.
      addRow("category:" + entry.text, entry.text, "RaceMenu has this tab; the Atelier does not build it.", true);
    }
  });

  AtelierBridge.whenAttached((ok) => {
    if (ok) {
      AtelierBridge.send("racemenu-categories-get");
    }
  });
})();
