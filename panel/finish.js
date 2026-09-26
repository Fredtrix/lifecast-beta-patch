// finish.js
// Atelier - the character's name, and the button that ends the menu.
//
// Finishing cannot be taken back: at the start of a new game it is the end of
// character creation. So the button asks twice - the first click shows what
// will happen and turns the button into "Confirm", and only a second click
// within CONFIRM_MS sends anything. The DLL does the same thing whichever way
// the menu was opened (it gives the name and closes the menu); what differs
// is only what this page SAYS first, from the DLL's guess at whether this is
// a new game. A wrong guess changes the words, never what happens.
//
// The name is always required. What the field shows comes from the
// character, read by the DLL each time the panel comes on screen; a name the
// player typed and has not finished with is kept across F4 swaps in the same
// menu session, and dropped when the menu opens again.
//
// A name the DLL could NOT read is not an empty name. Until a "name" message
// says the name was read, the finish is not offered: the page says why and
// points to RaceMenu's own Done button, instead of refusing as if the player
// had cleared the field - which would leave no way out of the page.
//
// After a finish is accepted the field and the button stay disabled: the
// menu is closing, and a second click must not reach the DLL. The DLL
// refuses a second finish in the same menu session on its own as well.
"use strict";

(() => {
  // Same window as the preset overwrite confirmation (presets.js).
  const CONFIRM_MS = 3000;

  const field = document.getElementById("finish-name");
  const button = document.getElementById("finish-button");
  const note = document.getElementById("finish-note");
  if (!field || !button || !note) {
    console.error("[finish] the name and finish elements are missing from the page");
    return;
  }

  const UNREADABLE = "The character's name could not be read, so this panel cannot finish. " +
    "Use RaceMenu's Done button.";

  let newGame = false;
  let readable = null;    // null: no "name" yet; false: the DLL could not read it
  let session = null;     // the menu session the field's text belongs to
  let edited = false;     // the player typed in this session
  let confirmUntil = 0;
  let sent = false;       // a finish was sent and not refused

  function say(text, kind) {
    note.textContent = text;
    note.className = "finish-note" + (kind ? " is-" + kind : "");
    note.hidden = text === "";
  }

  function render() {
    const confirming = Date.now() < confirmUntil;
    button.textContent = confirming ? "Confirm" : "Finish";
    button.disabled = sent || readable === false;
    field.disabled = sent || readable === false;
  }

  function confirmText(name) {
    return newGame
      ? "Finish creating \"" + name + "\"?"
      : "Close the editor as \"" + name + "\"?";
  }

  field.addEventListener("input", () => {
    edited = true;
    confirmUntil = 0;
    if (!sent) {
      say("");
    }
    render();
  });

  button.addEventListener("click", () => {
    if (sent) {
      return;
    }
    if (readable !== true) {
      confirmUntil = 0;
      say(UNREADABLE, "bad");
      render();
      return;
    }
    const name = field.value.trim();
    if (name === "") {
      confirmUntil = 0;
      say("The character needs a name.", "bad");
      render();
      field.focus();
      return;
    }
    if (Date.now() >= confirmUntil) {
      confirmUntil = Date.now() + CONFIRM_MS;
      say(confirmText(name));
      render();
      setTimeout(() => {
        if (!sent && Date.now() >= confirmUntil) {
          say("");
          render();
        }
      }, CONFIRM_MS + 50);
      return;
    }
    confirmUntil = 0;
    sent = true;
    say(newGame ? "Finishing..." : "Closing...");
    render();
    if (!AtelierBridge.send("finish", { name: name })) {
      sent = false;
      say("No bridge to the game - nothing was done.", "bad");
      render();
    }
  });

  AtelierBridge.on("name", (data) => {
    const incoming = data && data.session !== undefined ? data.session : null;
    newGame = !!(data && data.newGame);
    // Missing counts as not read: the page and the DLL ship together.
    readable = !!(data && data.readable === true);
    if (incoming !== session) {
      // A new menu session: whatever was typed or sent belongs to the last one.
      session = incoming;
      edited = false;
      sent = false;
      confirmUntil = 0;
      say("");
    }
    if (!edited && !sent) {
      field.value = readable && typeof data.name === "string" ? data.name : "";
    }
    if (!readable) {
      confirmUntil = 0;
      say(UNREADABLE, "bad");
    } else if (note.textContent === UNREADABLE) {
      say("");
    }
    render();
  });

  AtelierBridge.on("finish-result", (data) => {
    if (data && data.ok) {
      say(newGame ? "Finished." : "Closed.", "ok");
    } else {
      sent = false;
      say("Not finished: " + (data && data.why ? data.why : "unknown reason") + ".", "bad");
    }
    render();
  });

  AtelierBridge.whenAttached((ok) => {
    if (ok) {
      AtelierBridge.send("name-get");
    }
  });

  render();
})();
