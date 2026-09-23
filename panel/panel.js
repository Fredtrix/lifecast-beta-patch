// panel.js
// Atelier - the panel frame: says hello to the DLL and shows whether the
// bridge is up.
"use strict";

(() => {
  const PAGE_VERSION = "0.4.1";

  // How long the page waits for the DLL's answer to "hello" before saying
  // so. Only the status line depends on it; nothing waits on it.
  const HELLO_TIMEOUT_MS = 3000;

  const statusEl = document.getElementById("bridge-status");
  const versionsEl = document.getElementById("versions");
  const toRaceMenuEl = document.getElementById("to-racemenu");

  let answered = false;

  function setStatus(text, kind) {
    statusEl.textContent = text;
    statusEl.className = "status status-" + kind;
  }

  AtelierBridge.on("hello", (data) => {
    answered = true;
    versionsEl.textContent = "page " + PAGE_VERSION + " / dll " + data.dll + " / protocol " + data.protocol;
    if (data.compatible) {
      setStatus("connected", "ok");
    } else {
      setStatus("version mismatch", "bad");
    }
  });

  AtelierBridge.on("error", (data) => {
    console.warn("[panel] the DLL reported an error", data);
  });

  // Back to RaceMenu from inside the page. The DLL hides this page, which
  // also releases Meridian's focus, and shows RaceMenu's film again.
  function toRaceMenu() {
    if (!AtelierBridge.send("show-racemenu")) {
      setStatus("no bridge", "bad");
    }
  }

  // The button is the page's only way back. Esc is deliberately NOT one: it
  // only ever went one way (nothing brings the Atelier back on Esc), and a
  // key that means "leave" here would fight any text field the page grows.
  // F4 is the key, and it works in both directions.
  toRaceMenuEl.addEventListener("click", toRaceMenu);

  AtelierBridge.whenAttached((ok) => {
    if (!ok) {
      setStatus("no bridge", "bad");
      return;
    }
    AtelierBridge.send("hello", { page: PAGE_VERSION, protocol: AtelierBridge.PROTOCOL });
    setTimeout(() => {
      if (!answered) {
        setStatus("no answer", "warn");
      }
    }, HELLO_TIMEOUT_MS);
  });
})();
