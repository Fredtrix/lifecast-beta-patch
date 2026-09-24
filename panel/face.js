// face.js
// Atelier - the face morph sliders.
//
// The DLL owns the values (the player's face morphs) and redraws the face;
// this page only shows sliders and sends what the player sets. Indices are
// positions in the face morph array, in the order of the NPC_ record's NAM9
// field; the groups below follow the names the game gives each entry.
"use strict";

(() => {
  const GROUPS = [
    { title: "Nose", sliders: [[0, "Length"], [1, "Height"]] },
    { title: "Jaw", sliders: [[2, "Height"], [3, "Width"], [4, "Forward"]] },
    { title: "Cheekbones", sliders: [[5, "Height"], [6, "Width"]] },
    { title: "Eyes", sliders: [[7, "Height"], [8, "Width"], [17, "Depth"]] },
    { title: "Brows", sliders: [[9, "Height"], [10, "Width"], [11, "Forward"]] },
    { title: "Mouth", sliders: [[12, "Height"], [13, "Forward"]] },
    { title: "Chin", sliders: [[14, "Width"], [15, "Length"], [16, "Forward"]] },
  ];

  const rows = new Map(); // morph index -> row

  function format(v) {
    return (v >= 0 ? "+" : "") + v.toFixed(2);
  }

  for (const group of GROUPS) {
    const section = AtelierControls.section(group.title);
    for (const [index, label] of group.sliders) {
      const row = AtelierControls.row({
        label: label,
        min: -1,
        max: 1,
        step: 0.01,
        button: 0.05,
        format: format,
        send: (v, final) => AtelierBridge.send("face-set", { i: index, v: v, final: final }),
      });
      rows.set(index, row);
      section.appendChild(row.el);
    }
    AtelierControls.mount(section);
  }

  AtelierBridge.on("face", (data) => {
    const morphs = data && Array.isArray(data.morphs) ? data.morphs : [];
    for (const [index, row] of rows) {
      if (!row.dragging()) {
        row.set(morphs[index] === undefined ? null : morphs[index]);
      }
    }
  });

  AtelierBridge.whenAttached((ok) => {
    if (ok) {
      AtelierBridge.send("face-get");
    }
  });
})();
