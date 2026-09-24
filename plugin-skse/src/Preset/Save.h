// Save.h
// Atelier
//
// Saving a preset: RaceMenu writes the file, the Atelier asks it to.
//
// The call is RaceMenu's own Papyrus native CharGen.SaveCharacterPreset
// (Actor, string), dispatched through the Papyrus VM. RaceMenu writes every
// section it knows - including its extended sliders, sculpt and overrides,
// which no interface reachable from a plugin can read - into
// Data\SKSE\Plugins\CharGen\Presets\<name>.jslot. A preset written by anyone
// else would look like a RaceMenu preset and silently lose those.
//
// The Atelier never writes a preset file itself, and never rewrites one it
// has read: rewriting what the model does not fully describe truncates it.
//
// The call returns nothing useful, so success is not taken from it: after it
// returns, the file must exist, be non-empty, and - when a preset of that
// name was already there - have changed. Only then does the page hear
// "saved". If the native is not there (an older RaceMenu), the dispatch is
// refused, the call never returns, or the file is not born, the page is told
// so and offers RaceMenu's own save instead.
//
// Saving over an existing preset is guarded (Guard.h): the existing file is
// read first, and the overwrite is refused when it would destroy parts of it
// the live character may not hold. The refusal names them, and the page
// offers a new name or RaceMenu's own save.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "preset-save"   { "name": text, "overwrite": bool }
//   dll -> page  "preset-saved"  { "name", "ok": bool, "why": text,
//                                  "fallback": bool, "size", "ms",
//                                  "loss": [{ "section", "count" }] }
//                                  fallback = saving here is not available;
//                                  the page points to RaceMenu instead
//                                  loss = only on a refused overwrite: what
//                                  it would have destroyed

#pragma once

namespace Atelier::Preset::Save
{
	// Registers the bridge handler. Call once, after kDataLoaded.
	void Install();
}
