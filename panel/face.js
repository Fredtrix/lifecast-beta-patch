// face.js
// Atelier - the face sliders section.
//
// One group for now: the jaw. The DLL owns the values (the player's face
// morphs) and redraws the face; this page only shows sliders and sends what
// the player sets. Indices are positions in the face morph array, in the
// order of the NPC_ record's NAM9 field.
//
// While a slider is dragged the page sends at most one value per
// SEND_GAP_MS, always the latest; when the drag ends it sends the final
// value marked as such, which is what makes the DLL redraw at once.
"use strict";

(() => {
  const GROUP = {
    title: "Jaw",
    sliders: [
      { i: 2, label: "Up / Down" },
      { i: 3, label: "Narrow / Wide" },
      { i: 4, label: "Forward / Back" },
    ],
  };

  const MIN = -1;
  const MAX = 1;
  const STEP = 0.01;
  const SEND_GAP_MS = 50;

  const host = document.getElementById("sections");
  const rows = new Map(); // morph index -> { input, value, dragging, timer, lastSent }

  function format(v) {
    return (v >= 0 ? "+" : "") + v.toFixed(2);
  }

  function send(row, index, final) {
    if (row.timer !== null) {
      clearTimeout(row.timer);
      row.timer = null;
    }
    row.lastSent = performance.now();
    AtelierBridge.send("face-set", { i: index, v: Number(row.input.value), final: final });
  }

  function onInput(row, index) {
    row.dragging = true;
    row.value.textContent = format(Number(row.input.value));
    const wait = SEND_GAP_MS - (performance.now() - row.lastSent);
    if (wait <= 0) {
      send(row, index, false);
    } else if (row.timer === null) {
      row.timer = setTimeout(() => send(row, index, false), wait);
    }
  }

  function onChange(row, index) {
    row.dragging = false;
    send(row, index, true);
  }

  function build() {
    const section = document.createElement("section");
    section.className = "section";

    const title = document.createElement("h2");
    title.className = "section-title";
    title.textContent = GROUP.title;
    section.appendChild(title);

    for (const s of GROUP.sliders) {
      const wrap = document.createElement("label");
      wrap.className = "slider";

      const name = document.createElement("span");
      name.className = "slider-name";
      name.textContent = s.label;

      const value = document.createElement("span");
      value.className = "slider-value";
      value.textContent = "--";

      const input = document.createElement("input");
      input.type = "range";
      input.min = String(MIN);
      input.max = String(MAX);
      input.step = String(STEP);
      input.value = "0";
      input.disabled = true;

      wrap.appendChild(name);
      wrap.appendChild(value);
      wrap.appendChild(input);
      section.appendChild(wrap);

      const row = { input: input, value: value, dragging: false, timer: null, lastSent: 0 };
      rows.set(s.i, row);
      input.addEventListener("input", () => onInput(row, s.i));
      input.addEventListener("change", () => onChange(row, s.i));
    }

    const placeholder = host.querySelector(".placeholder");
    if (placeholder) {
      placeholder.remove();
    }
    host.appendChild(section);
  }

  AtelierBridge.on("face", (data) => {
    const morphs = data && Array.isArray(data.morphs) ? data.morphs : [];
    for (const [index, row] of rows) {
      if (row.dragging) {
        continue; // the player's hand wins over a value from before it moved
      }
      const v = morphs[index];
      if (typeof v !== "number" || !isFinite(v)) {
        row.input.disabled = true;
        row.value.textContent = "--";
        continue;
      }
      row.input.disabled = false;
      row.input.value = String(v);
      row.value.textContent = format(v);
    }
  });

  build();

  AtelierBridge.whenAttached((ok) => {
    if (ok) {
      AtelierBridge.send("face-get");
    }
  });
})();
