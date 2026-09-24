// scales.js
// Atelier - the body scale sliders.
//
// Which sliders exist is the DLL's answer, not this page's: it depends on
// the player's sex and on which nodes their skeleton has, and RaceMenu can
// change both while the panel is open. The "scales" message carries the
// list; when its keys change the rows are rebuilt, otherwise only the values
// move.
"use strict";

(() => {
  const section = AtelierControls.section("Body Scales");
  const body = AtelierControls.el("div");
  section.appendChild(body);
  AtelierControls.mount(section);

  let rows = new Map(); // key -> row
  let keys = "";

  function build(nodes) {
    rows = new Map();
    body.replaceChildren();
    for (const n of nodes) {
      const row = AtelierControls.row({
        label: n.label,
        min: n.min,
        max: n.max,
        step: 0.01,
        button: 0.05,
        format: (v) => v.toFixed(2),
        send: (v, final) => AtelierBridge.send("scales-set", { k: n.k, v: v, final: final }),
      });
      rows.set(n.k, row);
      body.appendChild(row.el);
    }
  }

  AtelierBridge.on("scales", (data) => {
    const nodes = data && data.ready && Array.isArray(data.nodes) ? data.nodes : [];
    const next = nodes.map((n) => n.k).join();
    if (next !== keys) {
      keys = next;
      build(nodes);
    }
    section.hidden = nodes.length === 0;
    for (const n of nodes) {
      const row = rows.get(n.k);
      if (row && !row.dragging()) {
        row.set(n.value);
      }
    }
  });

  AtelierBridge.whenAttached((ok) => {
    if (ok) {
      AtelierBridge.send("scales-get");
    }
  });
})();
