// keyboard.js
// Atelier - two corrections to the keyboard as Meridian UI delivers it to
// text fields. Both measured in the game, ABNT2 layout:
//
//   1. A dead key does not compose. The accent arrives as its own character
//      and the next vowel as another: "~" then "a" gives "~a", not the
//      composed letter. Here the pair is joined back into one character.
//   2. Some key presses arrive twice, about 15 ms apart, both as fresh key
//      presses (not auto-repeat). No person presses one key twice that fast,
//      so a second identical insertion inside that window is dropped. A held
//      key survives because Meridian repeats it slower than the window (see
//      keyRepeating below), not because of a guard.
//
// Everything happens in the page and applies to every text field in it.
// Nothing here depends on the DLL. When Meridian delivers the keyboard
// correctly, neither correction has anything to do.
"use strict";

(() => {
  // Two insertions of the same text closer than this are one key press.
  // Measured: 15-16 ms between the doubled events.
  const DOUBLE_WINDOW_MS = 30;

  // Spacing accent, as the dead key inserts it -> combining mark.
  const ACCENTS = {
    "~": "\u0303",
    "\u00b4": "\u0301",
    "'": "\u0301",
    "`": "\u0300",
    "^": "\u0302",
    "\u00a8": "\u0308",
    "\"": "\u0308",
  };

  // Keys that do not end a pending accent.
  const NEUTRAL_KEYS = new Set(["Dead", "Shift", "Control", "Alt", "AltGraph", "CapsLock", "Meta"]);

  let deadPressed = false; // a dead key went down; its accent is the next insertion
  let accent = null;       // { char, mark }: the accent now sitting before the caret

  let lastData = null;
  let lastTime = 0;

  // INERT under Meridian, and not the defence of a held key. Measured in
  // the game: Meridian delivers auto-repeat as fresh presses, repeat false,
  // ~59 ms apart at the fastest (~74 ms steady). What keeps a held key from
  // being eaten is that gap against DOUBLE_WINDOW_MS - and nothing promises
  // it. This flag only matters for a route that does mark auto-repeat.
  let keyRepeating = false;

  function isTextField(el) {
    if (!el) {
      return false;
    }
    if (el.tagName === "TEXTAREA") {
      return true;
    }
    return el.tagName === "INPUT" && /^(text|search|)$/i.test(el.type || "");
  }

  document.addEventListener("keydown", (event) => {
    keyRepeating = event.repeat;
    if (event.key === "Dead") {
      deadPressed = true;
      return;
    }
    if (event.key.length > 1 && !NEUTRAL_KEYS.has(event.key)) {
      // Backspace, arrows, Enter and the like: the accent is abandoned.
      deadPressed = false;
      accent = null;
    }
  }, true);

  document.addEventListener("beforeinput", (event) => {
    const el = event.target;
    if (!isTextField(el) || event.inputType !== "insertText" || event.data === null) {
      return;
    }

    const data = event.data;
    const now = performance.now();

    // 2. The doubled press.
    if (!keyRepeating && data === lastData && now - lastTime < DOUBLE_WINDOW_MS) {
      event.preventDefault();
      return;
    }
    lastData = data;
    lastTime = now;

    // 1a. The accent itself: let it show, remember where it is.
    if (deadPressed) {
      deadPressed = false;
      accent = Object.prototype.hasOwnProperty.call(ACCENTS, data) ? { char: data, mark: ACCENTS[data] } : null;
      return;
    }

    // 1b. The character after the accent.
    if (!accent) {
      return;
    }
    const pending = accent;
    accent = null;

    const pos = el.selectionStart;
    if (pos === null || pos !== el.selectionEnd || pos < 1 || el.value.charAt(pos - 1) !== pending.char) {
      return;
    }

    if (data === " ") {
      // Accent then space is the accent alone - what Windows does.
      event.preventDefault();
      return;
    }

    const composed = (data + pending.mark).normalize("NFC");
    if (composed.length !== 1) {
      // No such letter: both stay, as Windows leaves them.
      return;
    }

    event.preventDefault();
    el.setRangeText(composed, pos - 1, pos, "end");
    el.dispatchEvent(new InputEvent("input", { bubbles: true, inputType: "insertText", data: composed }));
  }, true);
})();
