// history.js
// Lifecast - undo and redo over the values the player changed.
//
// An entry is one FINISHED gesture on one control: the value it held before
// the hand touched it, the value it ended on, and how to put either one
// back. Nothing else enters - not scrolling, not a section opened or
// closed, not what was typed into the search field. Those are where the
// player is looking, and a history that undoes them undoes the wrong thing.
//
// The history lives here and not in the DLL because a gesture only exists
// here: the DLL sees a stream of *-set messages and cannot tell the drag
// that ended from the one still under way. Undo is the same *-set the
// control itself would send, with the older value - so it inherits, without
// a line of its own, everything the normal path already does (RaceMenuSync,
// the redraw, the echo back).
//
// The history does NOT cross the panel closing. It could: the browser is
// created once and only shown and hidden (Panel.h), so this page and
// everything in it survives. That is exactly why the clearing is written
// out rather than left to happen - between one opening and the next the
// player can have changed race, sex or actor, and an entry made before that
// would push a value from another character onto this one. "visibility"
// is the DLL's own message, sent from Panel::SetVisible.
"use strict";

const LifecastHistory = (() => {
  // Entries kept. Well past a single sitting at the panel; the cap is only
  // here so a long session cannot grow the list without bound.
  const LIMIT = 100;
  const LOG_LIMIT = 200;

  // entries[0 .. at-1] are done; entries[at ..] were undone and can be
  // redone. A new entry throws the second half away - the ordinary rule:
  // once you edit after undoing, there is no future to return to.
  const entries = [];
  let at = 0;

  // An apply() sends through the control's own path, and that path is what
  // records. Without this a single undo would record itself as an edit.
  let applying = false;

  const log = [];
  const listeners = [];

  function note(a_what, a_extra) {
    const line = Object.assign(
      { t: Math.round(performance.now()), what: a_what, at: at, n: entries.length },
      a_extra || {}
    );
    log.push(line);
    if (log.length > LOG_LIMIT) {
      log.shift();
    }
    console.log("[history] " + JSON.stringify(line));
    return line;
  }

  function changed() {
    listeners.forEach((fn) => {
      try {
        fn();
      } catch (e) {
        console.error("[history] listener threw", e);
      }
    });
  }

  // a_entry: { label, before, after, apply(value) }
  // Returns whether it became an entry. A gesture that ended where it
  // started is not one, and neither is a gesture on a control that never
  // received a value to start from.
  function record(a_entry) {
    if (applying) {
      return false;
    }
    if (!a_entry || typeof a_entry.apply !== "function") {
      return false;
    }
    if (a_entry.before === undefined || a_entry.before === null) {
      return false;
    }
    if (a_entry.before === a_entry.after) {
      return false;
    }
    entries.length = at;
    entries.push(a_entry);
    if (entries.length > LIMIT) {
      entries.shift();
    }
    at = entries.length;
    note("record", { label: a_entry.label, from: a_entry.before, to: a_entry.after });
    changed();
    return true;
  }

  function run(a_entry, a_value, a_what) {
    applying = true;
    try {
      a_entry.apply(a_value);
    } catch (e) {
      console.error("[history] apply threw", e);
    } finally {
      applying = false;
    }
    note(a_what, { label: a_entry.label, to: a_value });
  }

  function undo() {
    if (at === 0) {
      note("undo-empty");
      return false;
    }
    const entry = entries[at - 1];
    at -= 1;
    run(entry, entry.before, "undo");
    changed();
    return true;
  }

  function redo() {
    if (at >= entries.length) {
      note("redo-empty");
      return false;
    }
    const entry = entries[at];
    at += 1;
    run(entry, entry.after, "redo");
    changed();
    return true;
  }

  function clear(a_why) {
    if (entries.length === 0) {
      return;
    }
    entries.length = 0;
    at = 0;
    note("clear", { why: a_why });
    changed();
  }

  function wire() {
    const undoButton = document.getElementById("undo");
    const redoButton = document.getElementById("redo");
    if (!undoButton || !redoButton) {
      return;
    }
    undoButton.addEventListener("click", () => undo());
    redoButton.addEventListener("click", () => redo());
    const paint = () => {
      undoButton.disabled = at === 0;
      redoButton.disabled = at >= entries.length;
      undoButton.title = at === 0 ? "Nothing to undo" : "Undo " + entries[at - 1].label;
      redoButton.title = at >= entries.length ? "Nothing to redo" : "Redo " + entries[at].label;
    };
    listeners.push(paint);
    paint();
  }

  wire();

  // The panel was hidden: the switcher went back to RaceMenu, or the menu
  // closed. See the head of this file for why this is not optional.
  LifecastBridge.on("visibility", (data) => {
    if (data && data.visible === false) {
      clear("the panel was hidden");
    }
  });

  return {
    record: record,
    undo: undo,
    redo: redo,
    clear: clear,
    // For a measurement from outside the page: how many entries can still
    // be undone, how many exist, and what happened in order.
    depth: () => at,
    size: () => entries.length,
    log: () => log.slice(),
    onChange: (fn) => listeners.push(fn),
  };
})();
