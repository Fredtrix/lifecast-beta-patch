// delegate.js
// Lifecast - the "In RaceMenu" section: what this panel hands back to RaceMenu.
//
// Two kinds of row live here, and they arrive from opposite directions.
//
// The FIXED rows below are deliberate handovers, each with its reason in D027.
// One of them is a RaceMenu *mode* rather than a category (Sculpt), so it has
// no entry in the category list at all and could never come from the list.
// Face Presets was the other, and it is gone: the race's preset faces are a
// section of their own now (racepresets.js, Edit/RacePresets.cpp), the second
// door D125 brought inside. The War Paint Catalog left before it (D141), and
// Race & Sex after it: two controls now, the race in Race and the sex in Body
// (racesex.js, Edit/RaceSex.cpp).
//
// The rest arrive FROM THE GAME. RaceMenu's category list belongs to the
// player, not to us: mods inject categories into it, so which ones exist is
// not knowable from here. The DLL reads the live list and hands it over
// (D082), and every entry this build does not cover gets a door - which is
// what makes "if RaceMenu has a tab, it shows up in the Lifecast" true on a
// machine this project has never seen.
"use strict";

(() => {
  const ENTRIES = [
    ["face-sliders", "Extended Face Sliders", "RaceMenu's own face sliders, beyond the game's."],
    ["extra", "Extra Sliders", "The sliders RaceMenu files under its own Extra tab."],
    ["expressions", "Expressions", "The face expression sliders."],
    ["sculpt", "Sculpt", "Free-form editing of the head mesh."],
  ];

  // Categories that already have a home here, matched by the category's own
  // label - and "a home" is a section OR a fixed row above. $EXTRA and
  // $EXPRESSIONS have a row each. The four overlay categories have sections
  // of their own (overlays.js). $MAKEUP is war paint, which is a tint mask
  // and not an overlay: its home is the War Paint layer in Skin & Tints,
  // which carries the full catalog too (D084; the door to it left with D125).
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

  const section = LifecastControls.section("In RaceMenu");

  // live marks a row built from the category list, so a second list replaces
  // those rows instead of stacking another copy of them under the fixed ones.
  function addRow(key, label, note, live) {
    const row = LifecastControls.el("div", "delegate");
    row.dataset.door = key;
    if (live) {
      row.dataset.live = "1";
    }
    const text = LifecastControls.el("div", "delegate-text");
    text.appendChild(LifecastControls.el("span", "slider-name", label));
    text.appendChild(LifecastControls.el("span", "delegate-note", note));
    const go = LifecastControls.el("button", "panel-button", "Open");
    go.type = "button";
    go.tabIndex = -1;
    go.title = "Opens RaceMenu (F4 comes back)";
    go.addEventListener("click", () => LifecastBridge.send("show-racemenu", { for: key }));
    row.appendChild(text);
    row.appendChild(go);
    section.body.appendChild(row);
  }

  for (const [key, label, note] of ENTRIES) {
    addRow(key, label, note, false);
  }
  LifecastControls.mount(section, "delegate");


  // Where each category the page already covers is at home: a section key (a
  // prefix ends in ":"), or a door row. The check below measures the DOM
  // against this list and against the live category list, so a category with
  // no home shows up as an orphan instead of passing over in silence.
  // $ALL is the one entry that is not a category (it carries every bit).
  const HOME = {
    "RACE": { section: "race" },
    "BODY": { section: "look:Body" },
    "HEAD": { section: "face:" }, "FACE": { section: "face:" }, "EYES": { section: "face:" },
    "BROW": { section: "face:" }, "MOUTH": { section: "face:" },
    "HAIR": { section: "look:Hair" },
    "$EXTRA": { door: "extra" },
    "$EXPRESSIONS": { door: "expressions" },
    // The 15 colours of the film's COLORS tab live in seven sections: the skin
    // layers in tints, hair in look, and the face tints in the face sections
    // that tints.js files them under. All seven must be there.
    "$COLORS": { sections: ["tints", "look:Hair", "face:Mouth", "face:Eyes", "face:Cheekbones", "face:Nose", "face:Chin"] },
    "$MAKEUP": { section: "tints" },
    "$BODY PAINT": { section: "overlay:body" },
    "$HAND PAINT": { section: "overlay:hand" },
    "$FOOT PAINT": { section: "overlay:foot" },
    "$FACE PAINT": { section: "face:paint" },
    "$BODY SCALES": { section: "scales" },
  };

  function hasSection(prefix) {
    for (const s of document.querySelectorAll("#sections > [data-key]")) {
      const k = s.dataset.key;
      if (prefix.endsWith(":") ? k.indexOf(prefix) === 0 : k === prefix) {
        return true;
      }
    }
    return false;
  }

  function hasDoor(key) {
    return !!section.body.querySelector('[data-door="' + key + '"]');
  }

  // Every entry of the LIVE list, and what stands for it. An entry with no
  // home is an orphan. "Absent because the player has no overlay" is not one:
  // the category is not in the list then, so it is never looked at.
  let lastEntries = null;
  function check() {
    if (!lastEntries) {
      return null;
    }
    const orphans = [];
    const missing = [];
    for (const e of lastEntries) {
      const label = e.text;
      if (label === "$ALL") {
        continue;
      }
      const home = HOME[label];
      let ok;
      if (home) {
        if (home.door) {
          ok = hasDoor(home.door);
        } else {
          ok = true;
          for (const sec of home.sections || [home.section]) {
            if (!hasSection(sec)) {
              ok = false;
              missing.push(label + " (no section " + sec + ")");
            }
          }
        }
      } else if (typeof e.key === "string" && e.key !== "") {
        ok = hasSection("morph:" + e.key);
      } else {
        ok = hasDoor("category:" + label);
      }
      if (!ok) {
        orphans.push(label);
      }
    }
    return { total: lastEntries.length, orphans: orphans, missing: missing };
  }

  // The page settles a moment after the list arrives: the DLL's sections mount
  // on its own time. Checking on arrival would call an unmounted section an
  // orphan, so the check waits for two quiet seconds and reports the last one.
  let checkTimer = 0;
  function scheduleCheck() {
    clearTimeout(checkTimer);
    checkTimer = setTimeout(() => {
      const r = check();
      if (r) {
        LifecastBridge.send("categories-checked", r);
      }
    }, 2000);
  }
  window.LifecastDelegate = { check };

  LifecastBridge.on("racemenu-categories", (data) => {
    // ready says the list was read. A false one is a bad read, not an empty
    // list, and the doors already on screen are better than none: leave them.
    if (!data || !data.ready || !Array.isArray(data.entries)) {
      return;
    }
    for (const old of section.body.querySelectorAll("[data-live]")) {
      old.remove();
    }
    lastEntries = data.entries;
    scheduleCheck();
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
      addRow("category:" + entry.text, entry.text, "RaceMenu has this tab; the Lifecast does not build it.", true);
    }
  });

  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("racemenu-categories-get");
    }
  });
})();
