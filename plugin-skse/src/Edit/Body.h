// Body.h
// Lifecast
//
// The body section: the player picks one of OBody's body presets and sees it
// applied at once.
//
// The Lifecast delegates here literally. OBody knows the presets, which one
// the player carries and how to apply one; this module asks and relays. It
// holds no list of its own between messages and never decides what a body
// is - every message to the page is read from OBody at that moment.
//
// Without OBody answering (see Core/ObodyLink.h) the page is told "not
// ready" and does not build the section at all: not empty, not disabled,
// absent. The same happens if OBody withdraws while the panel is open.
//
// Which list is offered follows the sex of the player's actor base, the one
// RaceMenu edits. OBody's own reading of the player (the IsFemale flag of
// the assignment) is logged next to it on every send, so the two can be
// compared on a run rather than assumed to agree.
//
// Applying is OBody's AssignPresetToActor with its default flag, which
// applies the morphs immediately - measured to show on screen with the
// RaceSex Menu open, with no redraw from this side.
//
// A preset load replaces the whole character, bodyMorphs included; the
// section re-reads OBody after every load (Session::Replaced) and logs what
// OBody says the player carries then, so what the page shows is OBody's
// answer and not this module's memory.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "body-get"  {}
//   dll -> page  "body"      { "ready": false }
//                          or { "ready": true, "female": bool,
//                               "presets": [ name, ... ],
//                               "current": name or "" }
//   page -> dll  "body-set"  { "name": text }
//
// WHAT THE PANEL WROTE. Choosing a body is the first place the panel itself
// writes body morphs, and it is the one place that knows it did: the save
// preview (Preset/Save.h) must not announce as a GAIN what the player just
// chose here. So every body-set reads the player's body morphs through SKEE
// before and after OBody applies, and keeps what the body-set wrote:
//
//   - the keys the body-set touched are the keys with any entry added,
//     removed or changed between the two reads (in memory, same floats - no
//     file in between, so an exact comparison is right here);
//   - what is kept is every entry the character carries AFTER the body-set
//     under those keys, with its value. The first body sets the first
//     "OBody" morphs; a second body replaces them, and what the second
//     body-set left is what is kept - REPLACED, not added to the first, so
//     the entries of a body no longer on the character can never hide a gain
//     of the same name from somewhere else;
//   - a body-set that changed nothing (the same body again) keeps what was
//     kept;
//   - a preset load (Session::Replaced) and the menu closing forget it: after
//     either, nothing on the character is known to be the panel's.
//
// This is provenance for the preview, and nothing else: it never decides
// whether a file may be overwritten (that is Preset/Guard.h, untouched). If
// SKEE cannot be read, nothing is kept, and every body morph gain is
// announced - the preview errs on the side of saying too much.

#pragma once

#include <nlohmann/json.hpp>

namespace Lifecast::Edit::Body
{
	// Registers the bridge handlers, the session callbacks and the OBody
	// readiness callback. Call once, at kDataLoaded, after Session::Install.
	// Safe to call twice.
	void Install();

	// What this menu session's last effective body-set left on the character,
	// in the shape of a .jslot "bodyMorphs" list:
	// [ { "name": text, "keys": [ { "key": text, "value": number } ] } ].
	// Empty when nothing is known to be the panel's. Game thread only.
	const nlohmann::json& Written();
}
