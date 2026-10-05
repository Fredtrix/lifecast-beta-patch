// belt.js
// Lifecast - the belt and the small panel (Fase 7, E4: the LB structure).
//
// The screen is the character's. Along the bottom runs a belt: the title,
// the categories, search and the panel's buttons. Choosing a category opens a
// small panel anchored over the belt's left end, showing ONE region (one
// section) of it, with the sister regions as chips at its head. All shows
// every section; search and Changed show every section that matches. The
// categories are groups.js's groups - this file knows none by name.
//
// Where things live:
//   - the row counter sits at the head of the small panel, shown whenever
//     more than one region is on screen (All, search, Changed); in a single
//     region the belt's per-category numbers say it. The head is OUTSIDE
//     #sections on purpose: search.js watches the whole subtree of #sections
//     and rewrites the counter on every mutation, so a counter inside it feeds
//     its own observer forever (measured in the E4 probe: the page hung);
//   - the version line is in Settings > Debug (D132); the name and Finish are
//     on the belt, search and Changed over the upper left corner (D129),
//     undo and redo in the Character Control bar (D130).
//
// Opening: the page outlives the menu (presets.js receives "menu-closed"),
// so the belt re-opens as the player left it in this run of the game, and
// each category re-opens on the region last shown in it. The first open shows
// no category, on purpose. Keeping it across games needs a settings key the
// DLL does not accept today.
"use strict";

const LifecastBelt = (() => {
  const root = document.documentElement;
  const nav = document.getElementById("belt");
  const host = document.getElementById("sections");
  const chips = document.getElementById("regions");
  const search = document.getElementById("search");
  const changed = document.getElementById("filter-changed");

  if (!nav || !host || !chips) {
    return null;
  }

  // cat: the open category, or null. region: the section key shown.
  // last: the region last shown per category, for this run of the game.
  const ui = { cat: null, region: null, all: false, results: false, last: new Map() };

  function sections() {
    return Array.prototype.slice.call(host.querySelectorAll(":scope > .section"));
  }

  // data-group is written by controls.js's arrange(), from groups.js.
  function inCat(name) {
    return sections().filter((s) => s.dataset.group === name);
  }

  const buttons = new Map();
  for (const name of LifecastGroups.names.concat(["All"], LifecastGroups.after)) {
    const b = document.createElement("button");
    b.type = "button";
    b.tabIndex = -1;
    b.className = "belt-cat";
    b.dataset.cat = name;
    const label = document.createElement("span");
    label.textContent = name;
    const n = document.createElement("span");
    n.className = "belt-count";
    b.appendChild(label);
    b.appendChild(n);
    b.addEventListener("click", () => choose(name));
    nav.appendChild(b);
    buttons.set(name, b);
  }

  // Settings (D132): a category like the others, but its button is the
  // footer's, beside RaceMenu's, and carries no count.
  for (const name of LifecastGroups.footer) {
    const b = document.querySelector('.panel-footer [data-cat="' + name + '"]');
    if (b) {
      b.addEventListener("click", () => choose(name));
      buttons.set(name, b);
    }
  }

  // The always-visible buttons (autor, 2026-10-05 - D126), after the
  // categories: Presets and OBody open their region from anywhere, Sculpt
  // hands over to RaceMenu (the one thing the Lifecast does not do - D125).
  // OBody shows only when its section exists: without the mod there is no
  // section (D007), and a button to nothing would be a lie.
  const QUICK = [
    { name: "Presets", cat: "Presets", key: "presets" },
    { name: "OBody", cat: "Body", key: "obody" },
    { name: "Sculpt", title: "Opens RaceMenu's Sculpt (F4 comes back)", send: "sculpt" },
  ];
  const quick = [];
  const sep = document.createElement("span");
  sep.className = "belt-sep";
  nav.appendChild(sep);
  for (const q of QUICK) {
    const b = document.createElement("button");
    b.type = "button";
    b.tabIndex = -1;
    b.className = "panel-button belt-quick";
    b.textContent = q.name;
    if (q.title) {
      b.title = q.title;
    }
    b.addEventListener("click", () => {
      if (q.send) {
        LifecastBridge.send("show-racemenu", { for: q.send });
      } else {
        openRegion(q.cat, q.key);
      }
    });
    nav.appendChild(b);
    quick.push({ q: q, el: b });
  }

  // A second click on the open category closes the panel.
  function choose(name) {
    if (name === "All") {
      ui.all = !ui.all;
      ui.cat = null;
    } else {
      ui.all = false;
      ui.cat = ui.cat === name ? null : name;
      if (ui.cat) {
        const keys = inCat(ui.cat).map((s) => s.dataset.key);
        const kept = ui.last.get(ui.cat);
        ui.region = keys.indexOf(kept) >= 0 ? kept : (keys[0] || null);
      }
    }
    apply();
    host.scrollTop = 0;
  }

  // One region of one category, from a quick button; pressed again, closes.
  function openRegion(cat, key) {
    const shown = !ui.all && !ui.results && ui.cat === cat && ui.region === key;
    ui.all = false;
    ui.cat = shown ? null : cat;
    ui.region = shown ? ui.region : key;
    apply();
    host.scrollTop = 0;
  }

  function chooseRegion(key) {
    ui.region = key;
    apply();
    host.scrollTop = 0;
  }

  // search.js does the filtering and the counting; this only decides that
  // the panel is open and shows every region while a filter is on.
  function filtering() {
    return (search && search.value.trim() !== "") ||
      (changed && changed.getAttribute("aria-pressed") === "true");
  }

  function apply() {
    ui.results = filtering();
    const wide = ui.all || ui.results;
    const region = !wide && !!ui.cat;
    if (region && ui.region) {
      ui.last.set(ui.cat, ui.region);
    }
    for (const s of sections()) {
      const on = region && s.dataset.key === ui.region;
      s.classList.toggle("is-region", on);
      s.classList.toggle("is-off", region ? !on : !wide);
    }
    for (const [name, b] of buttons) {
      const n = name === "All" ? host.querySelectorAll(".slider").length
        : inCat(name).reduce((t, s) => t + s.querySelectorAll(".slider").length, 0);
      const count = b.querySelector(".belt-count");
      if (count) {
        count.textContent = n > 0 ? String(n) : "";
      }
      const on = name === "All" ? ui.all && !ui.results : name === ui.cat && !wide;
      b.classList.toggle("is-on", on);
      b.setAttribute("aria-pressed", on ? "true" : "false");
    }
    for (const { q, el } of quick) {
      if (q.key) {
        el.hidden = !host.querySelector(':scope > .section[data-key="' + q.key + '"]');
        el.classList.toggle("is-on", region && ui.cat === q.cat && ui.region === q.key);
      }
    }
    root.toggleAttribute("data-belt-open", wide || region);
    root.toggleAttribute("data-belt-wide", wide);
    root.toggleAttribute("data-belt-region", region);
    chips.textContent = "";
    if (region) {
      for (const s of inCat(ui.cat)) {
        const r = document.createElement("button");
        r.type = "button";
        r.tabIndex = -1;
        r.className = "region-chip" + (s.dataset.key === ui.region ? " is-on" : "");
        r.dataset.key = s.dataset.key;
        r.textContent = (s.querySelector(".section-title") || {}).textContent || s.dataset.key;
        r.addEventListener("click", () => chooseRegion(s.dataset.key));
        chips.appendChild(r);
      }
    }
  }

  // Sections mount as direct children of #sections, some on the DLL's own
  // time, and arrange() re-appends them: childList is enough, and one apply
  // per frame is plenty.
  let queued = false;
  new MutationObserver(() => {
    if (queued) {
      return;
    }
    queued = true;
    requestAnimationFrame(() => {
      queued = false;
      apply();
    });
  }).observe(host, { childList: true });

  if (search) {
    search.addEventListener("input", apply);
  }
  if (changed) {
    changed.addEventListener("click", apply);
  }

  apply();

  return {
    choose: choose,
    chooseRegion: chooseRegion,
    openRegion: openRegion,
    state: () => ({ cat: ui.cat, region: ui.region, all: ui.all, results: ui.results }),
  };
})();
