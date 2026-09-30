// groups.js
// Atelier - a level above the section (E2 item 1 - D074: read second-hand off
// the author's Prisma captures, origin in the phase document, not copied).
//
// controls.js knows how to draw a group heading and how to reveal a section
// by key; it does not know what "Body" or "Face" means. That mapping lives
// here, as the one place a key's prefix turns into a category.
//
// This file used to do a second job as well: an "Index" section holding a
// grid of tiles, one per category, that jumped to the first section under it.
// The author cut that section, so the grid, the set of keys it skipped and
// the observer that rebuilt it all left with it - they had no other caller.
// What stays is the grouping, which is the job the file is named after.
"use strict";

(() => {
  const GROUPS = [
    { title: "Body", test: (key) => key === "look:Body" || key === "look:Hair" || key === "look:Features" || key === "obody" || key === "scales" || key === "tints" || key.indexOf("overlay:") === 0 || key.indexOf("morph:") === 0 },
    { title: "Face", test: (key) => key.indexOf("face:") === 0 },
  ];

  // key can be undefined for a section still mounting (E2 item 3): a section
  // without a key must never reach a group's test, because one test here
  // (Face) calls .indexOf on it without a guard of its own.
  function groupOf(key) {
    if (typeof key !== "string") {
      return null;
    }
    const hit = GROUPS.find((g) => g.test(key));
    return hit ? hit.title : null;
  }

  AtelierControls.setGroups(groupOf);

  // The explicit order this page had by accident, from the <script> tags,
  // before OBody's section started mounting on the DLL's own time (E2 item
  // 1): without a declared order an async mount always lands last, wherever
  // "last" happens to be when the DLL answers. Only OBody needed naming to
  // fix that; character, control and the Body group members are named too so
  // that naming OBody does not silently move any of them.
  // The paint overlay sections mount on the DLL's own time, like OBody, so
  // the three body ones are named here or they land wherever "last" happens
  // to be. Face paint is deliberately NOT named: a rank puts a section ahead
  // of every unranked one, which would tear the Face group in two - a
  // heading above face paint and a second heading above the face sections
  // that have no rank. Unranked, it falls last inside its own group, which
  // is both stable and where it belongs.
  //
  // That last sentence was WRONG when it was written and is true now: an
  // unranked section used to sort past every group and open a second heading
  // of its own down at the bottom - which is exactly the tearing the sentence
  // says it avoids. controls.js rank() now lends an unranked section the
  // place of its group. Measured on the bench, 2026-09-30.
  //
  // The sections a mod's tab produces (morph:<key>) are unranked for a second
  // reason on top of that one: which of them exist is the player's mod order,
  // so there is no order to declare that would survive a different machine.
  // Unranked they fall last inside Body, in the order RaceMenu lists them.
  AtelierControls.setOrder([
    "character", "control",
    "look:Body", "look:Hair", "look:Features", "scales", "obody", "tints",
    "overlay:body", "overlay:hand", "overlay:foot",
  ]);
})();
