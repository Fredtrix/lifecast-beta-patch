// search.js
// Atelier - the toolbar: search by slider name, the "changed" filter, and
// the counter (E2 items 3, 4, 5).
//
// Both filters and the counter are read straight off the DOM that the
// section files already build - no registry of rows kept here. A row is
// "changed" when its own data-changed says so (controls.js, V079); "which
// slider is a slider" is answered by ".slider" inside a section's body.
// Sections with no .slider rows at all - Presets, Body, In RaceMenu,
// Character - are outside both filters (Prisma item 3 is about sliders).
//
// This runs before any section is built (script order: controls.js,
// shortcuts.js, search.js, then the sections), so it never scans the DOM
// once at startup - every call re-queries it live, and a MutationObserver
// re-runs the filter whenever a row appears, disappears, or its
// data-changed flips.
"use strict";

(() => {
  const searchInput = document.getElementById("search");
  const filterButton = document.getElementById("filter-changed");
  const counterEl = document.getElementById("row-counter");
  const host = document.getElementById("sections");

  if (!searchInput || !filterButton || !counterEl || !host) {
    return;
  }

  function openSection(section) {
    if (section.dataset.open === "1") {
      return;
    }
    section.dataset.open = "1";
    const head = section.querySelector(".section-head");
    if (head) {
      head.setAttribute("aria-expanded", "true");
    }
  }

  function apply() {
    const q = searchInput.value.trim().toLowerCase();
    const changedOnly = filterButton.getAttribute("aria-pressed") === "true";
    const active = q !== "" || changedOnly;

    let totalRows = 0;
    let matchedRows = 0;
    let changedMatched = 0;
    let totalSections = 0;

    host.querySelectorAll(":scope > .section").forEach((section) => {
      const rows = section.querySelectorAll(".section-body > .slider");
      if (rows.length === 0) {
        // Not a slider section - neither filter touches it.
        section.hidden = false;
        return;
      }
      totalSections += 1;

      let sectionHasVisible = false;
      rows.forEach((row) => {
        totalRows += 1;
        const nameEl = row.querySelector(".slider-name");
        const label = nameEl ? nameEl.textContent.toLowerCase() : "";
        const matchesQuery = q === "" || label.includes(q);
        const isChanged = row.dataset.changed === "1";
        const matchesChanged = !changedOnly || isChanged;
        const visible = matchesQuery && matchesChanged;
        row.hidden = !visible;
        if (matchesQuery) {
          matchedRows += 1;
          if (isChanged) {
            changedMatched += 1;
          }
        }
        if (visible) {
          sectionHasVisible = true;
        }
      });

      if (active) {
        section.hidden = !sectionHasVisible;
        if (sectionHasVisible) {
          openSection(section);
        }
      } else {
        section.hidden = false;
      }
    });

    // A group heading (E2 item 1, controls.js's arrange()) with every one of
    // its sections hidden is a label over nothing - hide it along with them.
    // Matched by data-group, not DOM adjacency: an ungrouped section (e.g.
    // Presets) can sit right after a group's last section and must not be
    // read as extending it.
    const groupVisible = new Map();
    host.querySelectorAll(":scope > .section").forEach((s) => {
      const g = s.dataset.group;
      if (!g) {
        return;
      }
      if (!s.hidden || !groupVisible.has(g)) {
        groupVisible.set(g, groupVisible.get(g) || !s.hidden);
      }
    });
    host.querySelectorAll(":scope > .group-heading").forEach((h) => {
      h.hidden = !groupVisible.get(h.textContent);
    });

    if (changedOnly) {
      counterEl.textContent = changedMatched + " changed of " + matchedRows;
    } else if (q !== "") {
      counterEl.textContent = matchedRows + " of " + totalRows + " sliders";
    } else {
      counterEl.textContent = totalRows + " sliders in " + totalSections + " sections";
    }
  }

  searchInput.addEventListener("input", apply);

  filterButton.addEventListener("click", () => {
    const pressed = filterButton.getAttribute("aria-pressed") === "true";
    filterButton.setAttribute("aria-pressed", pressed ? "false" : "true");
    apply();
  });

  new MutationObserver(apply).observe(host, {
    childList: true,
    subtree: true,
    attributes: true,
    attributeFilter: ["data-changed"],
  });

  apply();
})();
