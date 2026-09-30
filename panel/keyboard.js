// keyboard.js
// Atelier - two corrections to the keyboard as Meridian UI delivers it to
// text fields. Both measured in the game, ABNT2 layout:
//
//   1. A dead key does not compose. The accent arrives as its own character
//      and the next vowel as another: "~" then "a" gives "~a", not the
//      composed letter. Here the pair is joined back into one character.
//   2. Some key presses arrive twice as two whole presses. The duplicate is
//      dropped by KEY STATE: a second keydown for a key that has not come up
//      again, close behind the first, is not a person pressing twice.
//
// Everything happens in the page and applies to every text field in it.
// Nothing here depends on the DLL. When Meridian delivers the keyboard
// correctly, neither correction has anything to do.
"use strict";

(() => {
  // --- The doubled press ---------------------------------------------------
  //
  // Measured on the round of 2026-09-26 (Atelier.log, page clock), five
  // doubled presses, and every one of them has the same shape:
  //
  //     keydown, keydown, keyup, keyup
  //
  // The second keydown always arrives BEFORE any keyup. That is what is used
  // here, and it is why the old guard - "same text again inside 30 ms" - let
  // three of the five through: it compared the TEXT INSERTED, and a doubled
  // "T" arrives as "T" then "t" because the Shift is already up. It also
  // missed a pair 108 ms apart. Key state catches all five with no dependence
  // on which character came out.
  //
  // The gap is measured against the FIRST keydown of the press, not the
  // previous one, and the two populations do not overlap:
  //
  //     doubled press   15, 15, 54, 57, 108 ms   (5 of 5, round of 2026-09-26)
  //     auto-repeat     520 ms to the second press, then 59-75 ms (round A4)
  //
  // 200 ms sits between them with room on both sides. Comparing against the
  // previous keydown instead would flag auto-repeat from the third press on.
  const DOUBLE_GAP_MS = 200;

  // Keys down right now: code -> time of the first keydown of this press.
  const heldSince = new Map();

  // Set by keydown, read by the insertion that keydown produces. Measured
  // ordering: keydown, then beforeinput, then input.
  let dropNextInsertion = false;

  // --- The dead key --------------------------------------------------------

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
    const code = event.code;
    const now = performance.now();

    if (code) {
      const since = heldSince.get(code);
      if (since === undefined) {
        heldSince.set(code, now);
        dropNextInsertion = false;
      } else {
        // The key never came up. Auto-repeat that says so is always genuine;
        // under Meridian it does not say so, which is what the gap is for.
        dropNextInsertion = !event.repeat && (now - since) < DOUBLE_GAP_MS;
      }
    } else {
      dropNextInsertion = false;
    }

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

  document.addEventListener("keyup", (event) => {
    if (event.code) {
      heldSince.delete(event.code);
    }
  }, true);

  document.addEventListener("beforeinput", (event) => {
    const el = event.target;
    if (!isTextField(el) || event.inputType !== "insertText" || event.data === null) {
      return;
    }

    // 2. The doubled press. Decided by the keydown this insertion belongs to.
    if (dropNextInsertion) {
      dropNextInsertion = false;
      event.preventDefault();
      return;
    }
    dropNextInsertion = false;

    const data = event.data;

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
