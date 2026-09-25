// Body.h
// Atelier
//
// The body section: the player picks one of OBody's body presets and sees it
// applied at once.
//
// The Atelier delegates here literally. OBody knows the presets, which one
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

#pragma once

namespace Atelier::Edit::Body
{
	// Registers the bridge handlers, the session callbacks and the OBody
	// readiness callback. Call once, at kDataLoaded, after Session::Install.
	// Safe to call twice.
	void Install();
}
