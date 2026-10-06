// RacePresets.h
// Lifecast
//
// The race's preset faces - what RaceMenu's "Face Presets" mode shows, moved
// into the Lifecast (D125). This is NOT the player's own .jslot presets: those
// are the Presets section (Preset/, Phase 3) and are a different thing that
// happens to share the word.
//
// Where they come from: the RACE record carries, per sex, a list of NPC_
// records flagged IsCharGenFacePreset - TESRace::faceRelatedData[sex]->
// presetNPCs (RPRM/RPRF). Vanilla data, nothing from SKEE: NordRace male has
// ten, NordMalePreset01 to 10. Each preset NPC holds a whole head - the
// eighteen face morphs, the nose/eye/mouth type indices, its head parts, hair
// colour, complexion and weight.
//
// Applying one copies those from the preset's TESNPC onto the player's, the
// same fields Edit/FaceSliders and Edit/Appearance already write one at a
// time, and then goes through Session::Replaced: every editing section drops
// what it kept and re-reads, so the page shows the preset's values instead of
// the ones the player had. The character is shown with the shared
// DoReset3D(true), which is the call that makes morphs, types, head parts,
// weight and complexion appear at all (Edit/Session.h).
//
// What a preset carries and this does NOT copy: the tint layers and the
// texture lighting. Both are Skin & Paint's grandeza - the section the War
// Paint Catalog measured (D141) - and writing them from here would reach
// across a category boundary into a control that is already measured. So the
// player's war paint and skin tone survive applying a preset, which is a
// declared limit and not an accident.
//
// Browsing the list costs nothing: the page moves its own index and only the
// Apply button sends. A preset is one expensive redraw, so it is never
// applied by dragging through the list.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "race-presets-get"    {}
//   dll -> page  "race-presets"        { "count": N, "names": [ text, ... ] }
//                                        names are the preset NPCs' editor
//                                        IDs; every preset's full name is
//                                        "Prisoner", which names nothing
//   page -> dll  "race-preset-apply"   { "i": index }
//   dll -> page  "race-preset-applied" { "i", "ok": bool, "why": text }
//
// The DLL pushes "race-presets" when the panel comes on screen, because
// RaceMenu may have changed race or sex meanwhile and the list is per race
// and sex - the same reason Edit/Appearance rebuilds its lists there.

#pragma once

namespace Lifecast::Edit::RacePresets
{
	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();

	// NOT CALLED at present: the sex change is handed to RaceMenu's own callback
	// (Edit/RaceSex), which does its own reset. Kept, unused, as a fallback.
	//
	// Puts the player on the first preset the race lists for the current sex, as
	// the game does when the sex is swapped: morphs, face parts, head parts,
	// hair colour and complexion, but not the weight. Returns false when there
	// is nothing to apply. Does not redraw by itself beyond what a preset pick does.
	bool ApplyDefault(const char* a_why);
}
