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
// Applying one does not copy anything: it calls the "Presets" slider of RaceMenu's
// own film (ChangeHeadPreset, through Panel/RaceMenuFilm), which is the call the
// native menu makes. The native path writes the tint masks and clears what the
// new preset does not declare, so the panel and the native menu end up showing
// the same values. Then Session::Replaced makes every editing section drop what
// it kept and re-read.
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
//   dll -> page  "race-preset-native"  { "i" }   the player moved RaceMenu's own
//                                        Presets slider while the panel was away
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

	// Logs, one line per field, the player's appearance as the structure holds
	// it now: morphs, head parts, tint layers and masks, complexion, hair colour,
	// weight. A probe; it changes nothing.
	void LogPlayerFields(const char* a_tag);
}
