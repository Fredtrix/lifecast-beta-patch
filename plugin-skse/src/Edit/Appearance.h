// Appearance.h
// Lifecast
//
// Everything on the head and body that is not a face morph and is chosen
// from a list the game already has, plus weight:
//
//   nose type, eye shape, mouth shape   TESNPC::FaceData::parts [0], [2], [3]
//   hair, facial hair, eyes, scar,      head parts, one per type
//   brows
//   hair color                          one of the race's hair color forms
//   complexion                          one of the race's face detail sets
//   weight                              TESNPC::weight, 0 to 100
//
// Every list is read from the game at run time, for the player's race and
// sex, and never fixed here: what a player has depends on their load order.
// A head part list is every playable, non-extra head part of that type, for
// that sex, whose valid races include the player's race - the same count
// RaceMenu shows. A type list (nose, eye, mouth) has as many entries as the
// race says it has presets for that feature.
//
// Redraw: hair color shows with Actor::UpdateHairColor(), which is cheap and
// runs on every write. Everything else only shows after DoReset3D(true), the
// shared, throttled redraw (Edit/Session.h).
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "look-get"  {}
//   dll -> page  "look"      { "ready": bool, "controls": { <key>: <control> } }
//                             list:  { "kind": "list", "count": N, "index": i }
//                                    i is -1 when the current value is not in
//                                    the list; hair color adds "swatches",
//                                    one "#RRGGBB" per entry
//                             range: { "kind": "range", "min": a, "max": b,
//                                      "value": v }
//   page -> dll  "look-set"  { "k": key, "v": index or value, "final": bool }
//
// The DLL pushes "look" when the panel comes on screen - RaceMenu may have
// changed race, sex or any of these meanwhile - and after every final write,
// read back from the game.

#pragma once

namespace Lifecast::Edit::Appearance
{
	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();

	// NOT CALLED at present: race and sex changes are handed to RaceMenu's own
	// callbacks (Edit/RaceSex), which apply the game's rules for the new race
	// or sex. Kept as a fallback should that path ever be unavailable.
	//
	// After the player's race or sex was changed: for each of the seven head
	// part types, the hair colour and the complexion, what the new race and sex
	// accept stays, and what they do not becomes the race's default (the first
	// of its list when it declares none). Not a reset: a value the race would
	// have taken is the player's choice and is kept. The caller redraws.
	void FitToRace(const char* a_why);

	// After RaceMenu itself changed the player's race or sex: the lists the page
	// was sent are the old ones, and the new ones are rebuilt on the next read
	// (from the data handler until the menu reopens). Writes nothing to the game.
	void ListsChanged();
}
