// shortcuts.js
// Atelier - the always-present top bar (E2 item 8): one click from anywhere
// in the scrolled list to Presets or Body, and to RaceMenu's own Sculpt.
//
// These are jumps, not a second navigation structure: Presets and Body
// reveal (opening the section if it was collapsed, D064) and scroll to the
// real section built by presets.js / body.js. Sculpt has no section of its
// own here - it delegates straight to RaceMenu, the same way delegate.js's
// "In RaceMenu" row does, by sending the same "show-racemenu" message.
"use strict";

(() => {
  const presetsButton = document.getElementById("shortcut-presets");
  const obodyButton = document.getElementById("shortcut-obody");
  const sculptButton = document.getElementById("shortcut-sculpt");

  if (presetsButton) {
    presetsButton.addEventListener("click", () => {
      AtelierControls.reveal("presets");
    });
  }

  if (obodyButton) {
    obodyButton.addEventListener("click", () => {
      AtelierControls.reveal("obody");
    });
  }

  if (sculptButton) {
    sculptButton.addEventListener("click", () => {
      AtelierBridge.send("show-racemenu", { for: "sculpt" });
    });
  }

  // AtelierBridge.on() holds one handler per message type (bridge.js), and
  // body.js already owns "body" - a second listener here would replace
  // its handler, not add to it. The section's own presence in the DOM is
  // the fact this button tracks, so it watches that directly instead.
  if (obodyButton) {
    const host = document.getElementById("sections");
    const sync = () => {
      obodyButton.disabled = !host.querySelector('.section[data-key="obody"]');
    };
    sync();
    new MutationObserver(sync).observe(host, { childList: true });
  }
})();
