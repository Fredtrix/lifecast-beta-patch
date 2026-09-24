// delegate.js
// Atelier - what the Atelier hands back to RaceMenu.
//
// These controls are RaceMenu's and stay there: each entry says what it is
// and takes the player to RaceMenu, where it lives. F4 or the RaceMenu
// button bring the Atelier back, and every section asks the DLL for fresh
// values when the panel returns - so whatever changed there, race and sex
// included, shows here.
"use strict";

(() => {
  const ENTRIES = [
    ["race-sex", "Race & Sex", "Changing race or sex rebuilds every list here."],
    ["presets", "Face Presets", "The race's preset faces."],
    ["face-sliders", "Extended Face Sliders", "RaceMenu's own face sliders, beyond the game's."],
    ["expressions", "Expressions", "The face expression sliders."],
    ["paints", "Overlays & Paints", "Body, hand, feet and face paint, and the full war paint catalog."],
    ["sculpt", "Sculpt", "Free-form editing of the head mesh."],
  ];

  const section = AtelierControls.section("In RaceMenu");
  for (const [key, label, note] of ENTRIES) {
    const row = AtelierControls.el("div", "delegate");
    const text = AtelierControls.el("div", "delegate-text");
    text.appendChild(AtelierControls.el("span", "slider-name", label));
    text.appendChild(AtelierControls.el("span", "delegate-note", note));
    const go = AtelierControls.el("button", "panel-button", "Open");
    go.type = "button";
    go.tabIndex = -1;
    go.title = "Opens RaceMenu (F4 comes back)";
    go.addEventListener("click", () => AtelierBridge.send("show-racemenu", { for: key }));
    row.appendChild(text);
    row.appendChild(go);
    section.appendChild(row);
  }
  AtelierControls.mount(section);
})();
