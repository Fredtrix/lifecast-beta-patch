// control.js
// Atelier - Character Control: the things a player DOES to the character
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
"use strict";

(() => {
  const section = AtelierControls.section("Character Control");
  AtelierControls.mount(section, "control");

  const button = AtelierControls.el("button", "panel-button control-button", "Undress");
  button.type = "button";
  const note = AtelierControls.el("span", "control-note", "");
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
    if (!AtelierBridge.send("undress-set", { on: !on })) {
      busy = false;
      note.textContent = "No bridge to the game - nothing was done.";
      render();
    }
  });

  AtelierBridge.on("undress", (data) => {
    on = !!(data && data.on);
    busy = false;
    render();
  });

  AtelierBridge.whenAttached((ok) => {
    if (ok) {
      AtelierBridge.send("undress-get");
    }
  });

  render();
})();
