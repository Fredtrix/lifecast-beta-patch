// settings.js
// Atelier - the panel's preferences, kept by the DLL across games.
//
// There is no MCM: what the player sets for the panel is set in the panel,
// where it is used, and the DLL keeps it in a file of its own. This module is
// the page's copy of that file's values. A section reads a value with get(),
// hears the saved values arrive with onChange(), and writes one with set().
//
// The page never decides a default for the DLL: until the DLL answers, get()
// returns undefined and each section keeps its own starting value. A value
// the DLL does not accept is answered with the value it kept, so the page
// always ends showing what is on disk.
"use strict";

const AtelierSettings = (() => {
  let values = {};
  const listeners = [];

  AtelierBridge.on("settings", (data) => {
    values = data && typeof data === "object" ? data : {};
    for (const fn of listeners) {
      try {
        fn();
      } catch (e) {
        console.error("[settings] a listener threw", e);
      }
    }
  });

  AtelierBridge.whenAttached((ok) => {
    if (ok) {
      AtelierBridge.send("settings-get");
    }
  });

  return {
    get(key) {
      return values[key];
    },
    set(key, value) {
      values[key] = value;
      AtelierBridge.send("settings-set", { key: key, value: value });
    },
    onChange(fn) {
      listeners.push(fn);
    },
  };
})();
