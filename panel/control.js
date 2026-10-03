// control.js
// Lifecast - Character Control: the things a player DOES to the character
// while editing, as opposed to the sliders that describe what it looks like.
//
// It stands where the Index section used to. The Index was a grid of tiles
// that jumped to a category, and the author cut it in favour of this.
//
// The key is "control" and NOT "character": "character" is already taken by
// the identity section, which is always open because it is identity and not
// a category to browse (D064). A name that reads alike is not a free key
// (D079).
//
// Undress (Fase 6.6, E5): a toggle, not a state readout. The DLL only ever
// unequips what is already worn and re-equips that same instance - the item
// never leaves the inventory (see Edit/Undress.h). Same shape as the light
// toggle this section was written to receive: the button says what pressing
// it does, and the label after a click is what the DLL confirms, not a
// guess of what happened.
//
// Light (Fase 6.6, E4b): same shape again, and for the same reason as
// Undress - the DLL never reads the stage light's state back (Edit/Light.h),
// so a label that claimed state would be an inference dressed as a
// measurement. Placement here is provisional (layout is Fase 7).
"use strict";

(() => {
  const section = LifecastControls.section("Character Control");
  LifecastControls.mount(section, "control");

  const button = LifecastControls.el("button", "panel-button control-button", "Undress");
  button.type = "button";
  const note = LifecastControls.el("span", "control-note", "");
  section.body.appendChild(button);
  section.body.appendChild(note);

  let on = false;   // this feature's own belief; the DLL's "undress" reply is the truth
  let busy = false;

  function render() {
    button.textContent = on ? "Dress" : "Undress";
    button.disabled = busy;
  }

  button.addEventListener("click", () => {
    if (busy) {
      return;
    }
    busy = true;
    note.textContent = "";
    render();
    if (!LifecastBridge.send("undress-set", { on: !on })) {
      busy = false;
      note.textContent = "No bridge to the game - nothing was done.";
      render();
    }
  });

  LifecastBridge.on("undress", (data) => {
    on = !!(data && data.on);
    busy = false;
    render();
  });

  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("undress-get");
    }
  });

  render();

  const lightButton = LifecastControls.el("button", "panel-button control-button", "Light Off");
  lightButton.type = "button";
  const lightNote = LifecastControls.el("span", "control-note", "");
  section.body.appendChild(lightButton);
  section.body.appendChild(lightNote);

  let lightOn = true;   // this feature's own belief; the DLL's "light" reply is the truth
  let lightBusy = false;

  function renderLight() {
    lightButton.textContent = lightOn ? "Light Off" : "Light On";
    lightButton.disabled = lightBusy;
  }

  lightButton.addEventListener("click", () => {
    if (lightBusy) {
      return;
    }
    lightBusy = true;
    lightNote.textContent = "";
    renderLight();
    if (!LifecastBridge.send("light-set", { on: !lightOn })) {
      lightBusy = false;
      lightNote.textContent = "No bridge to the game - nothing was done.";
      renderLight();
    }
  });

  LifecastBridge.on("light", (data) => {
    lightOn = !!(data && data.on);
    lightBusy = false;
    renderLight();
  });

  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("light-get");
    }
  });

  renderLight();
})();
