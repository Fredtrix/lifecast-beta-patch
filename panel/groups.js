// groups.js
// Atelier - a level above the section, and a grid that jumps straight to one
// (E2 items 1, 2 - D074: read second-hand off the author's Prisma captures,
// origin in the phase document, not copied).
//
// controls.js knows how to draw a group heading and how to reveal a section
// by key; it does not know what "Body" or "Face" means. That mapping lives
// here, as the one place a key's prefix turns into a category.
"use strict";

(() => {
  const GROUPS = [
    { title: "Body", test: (key) => key === "look:Body" || key === "look:Hair" || key === "look:Features" || key === "obody" || key === "scales" || key === "tints" },
    { title: "Face", test: (key) => key.indexOf("face:") === 0 },
  ];

  // "character" opts out (always open, D064 - it is identity, not a category
  // to browse) and "index" opts out of indexing itself.
  const UNINDEXED = new Set(["character", "index"]);

  function groupOf(key) {
    const hit = GROUPS.find((g) => g.test(key));
    return hit ? hit.title : null;
  }

  AtelierControls.setGroups(groupOf);

  const host = document.getElementById("sections");
  const section = AtelierControls.section("Index");
  const grid = AtelierControls.el("div", "index-grid");
  section.body.appendChild(grid);
  AtelierControls.mount(section, "index");
  AtelierControls.setOrder(["character", "index"]);

  // One tile per group, and one per section that belongs to no group - the
  // categories a player can jump to. The first section mounted under a
  // category (DOM order, so it follows whatever order.js decides later) is
  // where the tile reveals.
  function refresh() {
    const targets = new Map(); // title -> key of its first section
    host.querySelectorAll(":scope > .section").forEach((s) => {
      const key = s.dataset.key;
      if (UNINDEXED.has(key)) {
        return;
      }
      const title = groupOf(key) || s.querySelector(".section-title").textContent;
      if (!targets.has(title)) {
        targets.set(title, key);
      }
    });

    grid.replaceChildren();
    targets.forEach((key, title) => {
      const b = AtelierControls.el("button", "index-tile", title);
      b.type = "button";
      b.addEventListener("click", () => AtelierControls.reveal(key));
      grid.appendChild(b);
    });
  }

  refresh();
  new MutationObserver(refresh).observe(host, { childList: true });
})();
