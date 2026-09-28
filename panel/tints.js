// tints.js
// Atelier - the tint masks: colour and intensity per layer, and which
// texture shows for war paint, dirt and markings.
//
// The layers come from the DLL, for the player's race and sex. Colour is set
// as red, green and blue, 0 to 255, or by a swatch - the race's own preset
// colours for that texture, when it has any - or freely, by the picker
// that opens from the layer's colour chip. Intensity is 0 to 100 percent.
// A picked layer adds a row that chooses its texture, "None" included.
"use strict";

(() => {
  const section = AtelierControls.section("Skin & Tints");
  const body = AtelierControls.el("div");
  section.body.appendChild(body);
  AtelierControls.mount(section, "tints");

  let layers = new Map(); // key -> { pick, r, g, b, alpha, chip, picker, grid, swatches, none }
  let keys = "";

  function hex2(n) {
    return Math.round(n).toString(16).padStart(2, "0").toUpperCase();
  }

  function colorOf(entry) {
    return "#" + hex2(entry.r.input.value) + hex2(entry.g.input.value) + hex2(entry.b.input.value);
  }

  function send(key, fields, final) {
    AtelierBridge.send("tint-set", Object.assign({ k: key, final: final }, fields));
  }

  function paint(entry) {
    entry.chip.style.background = colorOf(entry);
    entry.picker.refresh();
  }

  function setColor(entry, color) {
    const v = parseInt(color.slice(1), 16);
    if (!entry.r.dragging()) entry.r.set((v >> 16) & 255);
    if (!entry.g.dragging()) entry.g.set((v >> 8) & 255);
    if (!entry.b.dragging()) entry.b.set(v & 255);
    paint(entry);
  }

  function colorRow(key, entry, label) {
    return AtelierControls.row({
      label: label,
      min: 0,
      max: 255,
      step: 1,
      button: 1,
      format: (v) => String(Math.round(v)),
      send: (_v, final) => {
        paint(entry);
        send(key, { color: colorOf(entry) }, final);
      },
    });
  }

  function build(list) {
    layers = new Map();
    body.replaceChildren();
    for (const l of list) {
      const entry = { pick: null, swatches: null, none: false };
      const head = AtelierControls.el("div", "tint-head");
      const name = AtelierControls.el("span", "slider-name", l.label);
      entry.chip = AtelierControls.el("button", "swatch-chip picker-chip");
      entry.chip.type = "button";
      entry.chip.tabIndex = -1;
      entry.chip.title = "Pick any colour";
      name.appendChild(entry.chip);
      head.appendChild(name);
      body.appendChild(head);
      entry.picker = AtelierPicker.create({
        label: l.label,
        get: () => colorOf(entry),
        send: (hex, final) => {
          setColor(entry, hex);
          send(l.k, { color: hex }, final);
        },
      });
      entry.chip.addEventListener("click", () => {
        if (!entry.none) {
          entry.picker.toggle();
        }
      });
      body.appendChild(entry.picker.el);

      if (l.kind === "pick") {
        entry.pick = AtelierControls.row({
          label: "Texture",
          min: -1,
          max: 0,
          step: 1,
          button: 1,
          format: (v) => (v < 0 ? "None" : String(v + 1) + " / " + String(entry.count)),
          send: (v, final) => send(l.k, { index: v }, final),
        });
        body.appendChild(entry.pick.el);
      }
      entry.r = colorRow(l.k, entry, "Red");
      entry.g = colorRow(l.k, entry, "Green");
      entry.b = colorRow(l.k, entry, "Blue");
      entry.alpha = AtelierControls.row({
        label: "Intensity",
        min: 0,
        max: 100,
        step: 1,
        button: 5,
        format: (v) => String(Math.round(v)) + "%",
        send: (v, final) => send(l.k, { alpha: v / 100 }, final),
      });
      for (const row of [entry.r, entry.g, entry.b, entry.alpha]) {
        body.appendChild(row.el);
      }
      entry.grid = AtelierControls.el("div", "swatch-grid");
      body.appendChild(entry.grid);
      layers.set(l.k, entry);
    }
  }

  function fillSwatches(key, entry, label, swatches) {
    const same = entry.swatches && entry.swatches.join() === swatches.join();
    entry.swatches = swatches;
    entry.grid.hidden = swatches.length === 0;
    if (same) {
      return;
    }
    entry.grid.replaceChildren();
    for (const color of swatches) {
      const b = AtelierControls.el("button", "swatch");
      b.type = "button";
      b.tabIndex = -1;
      b.style.background = color;
      b.title = color;
      // A swatch does not go through a colour row's send, so it records its
      // own entry: read the colour BEFORE setColor() moves the R/G/B rows.
      const pick = (hex) => {
        setColor(entry, hex);
        send(key, { color: hex }, true);
      };
      b.addEventListener("click", () => {
        const before = colorOf(entry);
        pick(color);
        // colorOf() is always upper case; a swatch from the DLL need not be,
        // and the same colour in two spellings would be an entry that undoes
        // to itself.
        AtelierHistory.record({ label: label, before: before, after: color.toUpperCase(), apply: pick });
      });
      entry.grid.appendChild(b);
    }
  }

  AtelierBridge.on("tints", (data) => {
    const list = data && data.ready && Array.isArray(data.layers) ? data.layers : [];
    const next = list.map((l) => l.k + ":" + l.kind).join();
    if (next !== keys) {
      keys = next;
      build(list);
    }
    section.hidden = list.length === 0;
    for (const l of list) {
      const entry = layers.get(l.k);
      if (!entry) {
        continue;
      }
      // A picked layer with nothing showing has no colour of its own to
      // edit until a texture is chosen.
      const none = l.kind === "pick" && l.index < 0;
      entry.none = none;
      if (none) {
        entry.picker.el.hidden = true;
      }
      if (entry.pick) {
        entry.count = l.count;
        entry.pick.setRange(-1, l.count - 1);
        if (!entry.pick.dragging()) {
          entry.pick.set(l.index);
        }
      }
      if (none) {
        for (const row of [entry.r, entry.g, entry.b, entry.alpha]) {
          row.set(null);
        }
        entry.chip.style.background = "transparent";
      } else if (!entry.picker.dragging()) {
        setColor(entry, l.color);
        if (!entry.alpha.dragging()) {
          entry.alpha.set(Math.round(l.alpha * 100));
        }
      }
      fillSwatches(l.k, entry, l.label, none ? [] : Array.isArray(l.swatches) ? l.swatches : []);
    }
  });

  AtelierBridge.whenAttached((ok) => {
    if (ok) {
      AtelierBridge.send("tints-get");
    }
  });
})();
