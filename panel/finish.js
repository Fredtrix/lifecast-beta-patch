// finish.js
// Lifecast - the character's name, and the button that ends the menu.
//
// One click finishes, however the menu was opened: there is no confirmation
// step on this page. The DLL does the same thing either way (it gives the name
// and closes the menu); what differs is only the words this page shows while
// it happens, from the DLL's guess at whether this is a new game. A wrong
// guess changes the words, never what happens.
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
  // What stays in #finish is the Debug section, under Settings (D132); the
  // name and the button are on the belt (D129), always on screen.
  const host = document.getElementById("finish");
  if (host) {
    const s = LifecastControls.section("Debug", { open: true });
    while (host.firstChild) {
      s.body.appendChild(host.firstChild);
    }
    host.remove();
    LifecastControls.mount(s, "debug");
    LifecastControls.setOrder(["debug"]);
  }

  const field = document.getElementById("finish-name");
  const button = document.getElementById("finish-button");
  const note = document.getElementById("finish-note");
  // A second Finish (the E3 header had one). Optional: the belt's button is
  // always on screen now, and this page has none.
  const top = document.getElementById("finish-top");
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
  let sent = false;       // a finish was sent and not refused

  function say(text, kind) {
    note.textContent = text;
    note.className = "finish-note" + (kind ? " is-" + kind : "");
    note.hidden = text === "";
  }

  function render() {
    button.disabled = sent || readable === false;
    if (top) {
      top.disabled = button.disabled;
    }
    field.disabled = sent || readable === false;
  }

  field.addEventListener("input", () => {
    edited = true;
    if (!sent) {
      say("");
    }
    render();
  });

  function finish() {
    if (sent) {
      return;
    }
    if (readable !== true) {
      say(UNREADABLE, "bad");
      render();
      return;
    }
    const name = field.value.trim();
    if (name === "") {
      say("The character needs a name.", "bad");
      render();
      field.focus();
      return;
    }
    sent = true;
    say(newGame ? "Finishing..." : "Closing...");
    render();
    if (!LifecastBridge.send("finish", { name: name })) {
      sent = false;
      say("No bridge to the game - nothing was done.", "bad");
      render();
    }
  }

  button.addEventListener("click", finish);
  if (top) {
    top.addEventListener("click", finish);
  }

  LifecastBridge.on("name", (data) => {
    const incoming = data && data.session !== undefined ? data.session : null;
    newGame = !!(data && data.newGame);
    // Missing counts as not read: the page and the DLL ship together.
    readable = !!(data && data.readable === true);
    if (incoming !== session) {
      // A new menu session: whatever was typed or sent belongs to the last one.
      session = incoming;
      edited = false;
      sent = false;
      say("");
    }
    if (!edited && !sent) {
      field.value = readable && typeof data.name === "string" ? data.name : "";
    }
    if (!readable) {
      say(UNREADABLE, "bad");
    } else if (note.textContent === UNREADABLE) {
      say("");
    }
    render();
  });

  LifecastBridge.on("finish-result", (data) => {
    if (data && data.ok) {
      say(newGame ? "Finished." : "Closed.", "ok");
    } else {
      sent = false;
      say("Not finished: " + (data && data.why ? data.why : "unknown reason") + ".", "bad");
    }
    render();
  });

  LifecastBridge.whenAttached((ok) => {
    if (ok) {
      LifecastBridge.send("name-get");
    }
  });

  render();
})();
