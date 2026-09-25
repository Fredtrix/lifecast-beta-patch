// Load.h
// Atelier
//
// Loading a whole preset: RaceMenu applies the file, the Atelier asks it to.
//
// The call is RaceMenu's own Papyrus native CharGen.LoadCharacterPresetEx
// (Actor, string, ColorForm hairColor, int flags), dispatched through the
// Papyrus VM, with the same hair colour form RaceMenu's own CharGen.LoadPreset
// passes (RaceMenuHairColor). RaceMenu reads every section it wrote -
// extended sliders and sculpt included, which no plugin interface can set -
// and redraws the head itself.
//
// Editing one value at a time stays the panel's own (Edit/); a whole preset
// is RaceMenu's, the same way saving one is (Save.h).
//
// After it returns true, the Atelier does what RaceMenu's own LoadPreset does
// next - it asks RaceMenu's script to take a fresh copy of the hair colour and
// tint masks - and then tells every editing section the character was
// replaced, so the page shows the loaded values. The file it loaded is kept,
// byte for byte, as the provenance that lets a later save overwrite it
// (Guard.h).
//
// If the native is not there, the dispatch is refused, the call does not
// return in time, or it returns false, the page is told so and offers
// RaceMenu's own preset page instead. A call given up on can still return
// later and apply the preset: that is logged as LATE with its time, the
// character is re-read, the page is told again ("late": true), and no
// provenance is kept, so a late load never authorizes an overwrite.
//
// Every load is timed and counted, so its cost is measured on every run and
// the first load of a session can be told from a later one.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "preset-load"    { "name": text }
//   dll -> page  "preset-loaded"  { "name", "ok": bool, "why": text,
//                                   "fallback": bool, "ms", "late": bool }

#pragma once

namespace Atelier::Preset::Load
{
	// The flags passed to LoadCharacterPresetEx: every bit (0xFFFFFFFF, which
	// Papyrus's 32-bit int holds as -1). That is the native's own declared
	// default, and what RaceMenu's own LoadCharacterPreset passes - so a load
	// from here applies everything RaceMenu's Load button does. The known bits
	// are overrides 1, body morphs 2, transforms 4, skin overrides 8; picking
	// only those would silently drop any bit not known here.
	inline constexpr std::int32_t kFlags = -1;

	// Registers the bridge handler. Call once, after kDataLoaded.
	void Install();
}
