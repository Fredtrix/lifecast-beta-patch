// control.js
// Atelier - Character Control: the things a player DOES to the character
// while editing, as opposed to the sliders that describe what it looks like.
//
// This file is the frame and nothing else. It is empty of function on
// purpose: it is the moulding that zoom, stage lighting and undress land in
// later. A section with no body still draws its own header and still
// collapses, so an empty one is visible without pretending to do anything.
//
// It stands where the Index section used to. The Index was a grid of tiles
// that jumped to a category, and the author cut it in favour of this.
//
// The key is "control" and NOT "character": "character" is already taken by
// the identity section, which is always open because it is identity and not
// a category to browse (D064). A name that reads alike is not a free key
// (D079).
"use strict";

(() => {
  const section = AtelierControls.section("Character Control");
  AtelierControls.mount(section, "control");
})();
