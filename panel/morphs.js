// morphs.js
// Lifecast - the slider tabs a mod injects into RaceMenu, body morphs among them.
//
// Nothing here knows a mod's name. The DLL reads RaceMenu's own film and
// hands over every category some mod registered, with the sliders hanging off
// it - label, range, interval and current value (Edit/BodyMorphs.h). One
// section per category, in the order the film lists them.
//
// A value goes back as the slider's callback name, which is the only thing
// the mod that owns it recognises. The DLL turns that into the same event
// RaceMenu's own slider sends, so the mod applies it itself.
//
// A tab with no sliders is not a mistake and is not hidden: a mod may add its
// category unconditionally and its sliders only for one sex, so the tab is
// really there and really empty. RaceMenu shows it empty too. Hiding it would
// break the rule this phase exists for - if RaceMenu has a tab, the Lifecast
// shows it - in the one case where the tab is easiest to lose.
"use strict";

(() => {
  // [{ key, el, rows }], in the order the film lists the tabs.
  let built = [];
  let signature = "";

  function decimals(step) {
    if (step >= 1) {
      return 0;
    }
    return step >= 0.1 ? 1 : 2;
  }

  function create(cat, sliders) {
    const section = LifecastControls.section(cat.label);
    const rows = new Map();
    if (sliders.length === 0) {
      section.body.appendChild(
        LifecastControls.el(
          "div",
          "delegate-note",
          "RaceMenu has this tab and it holds nothing right now. What goes in it is the mod's own decision - some add theirs for one sex only."
        )
      );
    }
    for (const s of sliders) {
      const dp = decimals(s.step);
      // Five intervals to an arrow press, the ratio the body scales use: one
      // interval is what the mod declared for a drag, and a row two hundred
      // presses wide is not usable. But a row of ten intervals or fewer is
      // not really a slider - a mod registers 0 to 2 by 1 to get a button,
      // and one of the two here is a Reset that clears every body morph. On
      // those the arrow moves one interval, so no single press can cross the
      // range and fire it.
      const steps = (s.max - s.min) / s.step;
      const row = LifecastControls.row({
        label: s.label,
        min: s.min,
        max: s.max,
        step: s.step,
        button: steps > 10 ? s.step * 5 : s.step,
        format: (v) => v.toFixed(dp),
        send: (v, final) => LifecastBridge.send("body-morph-set", { c: s.c, v: v, final: final }),
      });
      rows.set(s.c, row);
      section.body.appendChild(row.el);
    }
    LifecastControls.mount(section, "morph:" + cat.key);
    return { key: cat.key, el: section, rows: rows };
  }

  LifecastBridge.on("body-morphs", (data) => {
    // ready says the film was read. A false one is a bad read and not an
    // empty machine: what is on screen is better than nothing, so leave it.
    if (!data || !data.ready || !Array.isArray(data.categories)) {
      return;
    }
    const cats = data.categories.filter((c) => c && typeof c.key === "string" && c.key !== "");
    for (const cat of cats) {
      if (!Array.isArray(cat.sliders)) {
        cat.sliders = [];
      }
    }

    // Every tab, every label and every callback in one signature, and when it
    // moves ALL the sections are rebuilt - not just the one that changed. A
    // mod registers its sliders again each time the menu opens, under
    // conditions that can have changed (sex is one), so a tab can empty or
    // fill. Rebuilding one section alone would re-mount it, and a section
    // mounted later sorts later: the tabs would stop being in the order
    // RaceMenu lists them, which is the only order there is a reason to show
    // them in. Measured on the bench, 2026-09-30.
    const next = JSON.stringify(cats.map((c) => [c.key, c.label, c.sliders.map((s) => s.c)]));
    if (next !== signature) {
      signature = next;
      for (const entry of built) {
        entry.el.remove();
      }
      built = cats.map((cat) => create(cat, cat.sliders));
    }

    for (let i = 0; i < cats.length; i += 1) {
      for (const s of cats[i].sliders) {
        const row = built[i].rows.get(s.c);
        if (row && !row.dragging()) {
          row.set(s.value);
        }
      }
    }
  });

  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("body-morphs-get");
    }
  });
})();
