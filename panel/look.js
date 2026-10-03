// look.js
// Lifecast - the list controls (head parts, feature types, hair colour,
// complexion) and weight.
//
// Every list comes from the DLL, for the player's race and sex, in the
// "look" message: how many entries it has and which one is current. The
// page never assumes a length - it depends on the load order. A list with
// no entries hides its row. A current value that is not in the list shows
// as "--" until the player picks one.
//
// Hair colour is a list too - the race's preset colours - and also free: the
// chip opens a picker, and a picked colour goes to RaceMenu's own custom
// hair colour, which is what keeps it across saves. The list then shows
// "--", because the colour is no longer one of its entries.
"use strict";

(() => {
  const SECTIONS = [
    {
      title: "Body",
      controls: [["weight", "Weight"]],
    },
    {
      title: "Hair",
      controls: [["hair", "Hair"], ["hair-color", "Hair Color"], ["facial-hair", "Facial Hair"]],
    },
    {
      title: "Features",
      controls: [
        ["nose-type", "Nose Type"],
        ["eye-shape", "Eye Shape"],
        ["eyes", "Eye Color"],
        ["brows", "Brow Type"],
        ["mouth-shape", "Mouth Shape"],
        ["scar", "Scar"],
        ["complexion", "Complexion"],
      ],
    },
  ];

  const PART_KEYS = new Set(["hair", "facial-hair", "eyes", "brows", "scar"]);

  const rows = new Map(); // key -> { row, kind, count, chip, picker, color, grid, swatches }

  function send(key, v, final) {
    LifecastBridge.send("look-set", { k: key, v: v, final: final });
  }

  function buildRow(key, label) {
    const entry = { kind: key === "weight" ? "range" : "list", count: 0, chip: null, grid: null };
    entry.row = LifecastControls.row({
      label: label,
      min: 0,
      max: key === "weight" ? 100 : 0,
      step: 1,
      button: 1,
      format: (v) => (entry.kind === "list" ? String(v) + " / " + String(entry.count - 1) : String(Math.round(v))),
      send: (v, final) => send(key, v, final),
    });

    if (PART_KEYS.has(key)) {
      // The part the player wears, as RaceMenu names it: the DLL sends the
      // name and the plugin; while dragging, the index moves ahead of them.
      entry.nameEl = LifecastControls.el("div", "part-name");
      entry.row.el.appendChild(entry.nameEl);
      entry.row.input.addEventListener("input", () => {
        entry.nameEl.textContent = "...";
        entry.nameEl.title = "";
      });
    }

    if (key === "hair-color") {
      entry.chip = LifecastControls.el("button", "swatch-chip picker-chip");
      entry.chip.type = "button";
      entry.chip.tabIndex = -1;
      entry.chip.title = "Pick any colour";
      entry.row.el.firstChild.appendChild(entry.chip);
      entry.picker = LifecastPicker.create({
        label: "Hair Color",
        get: () => entry.color || "#000000",
        send: (hex, final) => {
          entry.color = hex;
          entry.chip.style.background = hex;
          LifecastBridge.send("look-set", { k: "hair-rgb", v: hex, final: final });
        },
      });
      entry.chip.addEventListener("click", () => entry.picker.toggle());
      entry.grid = LifecastControls.el("div", "swatch-grid");
      entry.row.el.appendChild(entry.grid);
      entry.row.el.appendChild(entry.picker.el);
      entry.row.input.addEventListener("input", () => {
        const i = Number(entry.row.input.value);
        entry.color = entry.swatches && entry.swatches[i];
        paintChip(entry, i);
      });
    }
    rows.set(key, entry);
    return entry.row.el;
  }

  function paintChip(entry, index) {
    entry.chip.style.background = entry.color || "transparent";
    entry.picker.refresh();
    for (const b of entry.grid.children) {
      b.classList.toggle("selected", Number(b.dataset.index) === index);
    }
  }

  function fillSwatches(entry, swatches) {
    const same = entry.swatches && entry.swatches.join() === swatches.join();
    entry.swatches = swatches;
    if (same) {
      return;
    }
    entry.grid.replaceChildren();
    swatches.forEach((color, i) => {
      const b = LifecastControls.el("button", "swatch");
      b.type = "button";
      b.tabIndex = -1;
      b.dataset.index = String(i);
      b.style.background = color;
      b.title = String(i);
      // A swatch does not go through the row's own send, so it records its
      // own entry: read the index BEFORE set() moves it, then pick.
      const pick = (index) => {
        entry.row.set(index);
        entry.color = entry.swatches && entry.swatches[index];
        paintChip(entry, index);
        send("hair-color", index, true);
      };
      b.addEventListener("click", () => {
        const before = Number(entry.row.input.value);
        pick(i);
        LifecastHistory.record({ label: "Hair Color", before: before, after: i, apply: pick });
      });
      entry.grid.appendChild(b);
    });
  }

  for (const s of SECTIONS) {
    const section = LifecastControls.section(s.title);
    for (const [key, label] of s.controls) {
      section.body.appendChild(buildRow(key, label));
    }
    LifecastControls.mount(section, "look:" + s.title);
  }

  LifecastBridge.on("look", (data) => {
    const controls = data && data.ready && data.controls ? data.controls : {};
    for (const [key, entry] of rows) {
      const c = controls[key];
      if (!c) {
        entry.row.set(null);
        continue;
      }
      if (c.kind === "range") {
        entry.kind = "range";
        entry.row.setRange(c.min, c.max);
        if (!entry.row.dragging()) {
          entry.row.set(c.value);
        }
        continue;
      }

      entry.kind = "list";
      entry.count = c.count;
      entry.row.el.hidden = c.count === 0;
      if (c.count === 0) {
        entry.row.set(null); // no stale values from the last list behind the hidden row
        continue;
      }
      entry.row.setRange(0, c.count - 1);
      if (entry.grid) {
        fillSwatches(entry, Array.isArray(c.swatches) ? c.swatches : []);
      }
      if (entry.row.dragging()) {
        continue;
      }
      if (c.index < 0) {
        // Enabled, but showing that the current value is not a list entry.
        entry.row.set(0);
        entry.row.valueEl.textContent = "--";
      } else {
        entry.row.set(c.index);
      }
      if (entry.nameEl) {
        const name = typeof c.name === "string" ? c.name : "";
        const source = typeof c.source === "string" ? c.source : "";
        entry.nameEl.textContent = c.index < 0 ? "" : name + (source ? "  -  " + source : "");
        entry.nameEl.title = entry.nameEl.textContent;
      }
      if (entry.chip && !entry.picker.dragging()) {
        // The DLL sends the colour the player wears, preset or not.
        entry.color = typeof c.color === "string" ? c.color : entry.swatches && entry.swatches[c.index];
        paintChip(entry, c.index);
      }
    }
  });

  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("look-get");
    }
  });
})();
