// BodyMorphs.h
// Atelier - the slider tabs a mod injects into RaceMenu, body morphs among them.
//
// RaceMenu lets a mod add a whole tab of its own: AddCategory registers it
// under a text key, and AddSliderEx hangs sliders on that key. On this
// machine that is how "CBBE 3BA MORPHS" and "HIMBO Morphs" exist, but the
// mechanism is not about those two - the set of tabs belongs to the player's
// mod order, so nothing here names a mod or carries a list of its sliders.
//
// WHERE THE CATALOG COMES FROM. Not from SKEE: IBodyMorphInterface visits the
// morphs an actor already carries, which on a fresh character is none, and it
// has no call that lists what a mod offers. The catalog is on RaceMenu's own
// film. Papyrus sends each slider as one ";;"-joined string, and the film
// turns it into a list entry carrying text, callbackName, sliderMin,
// sliderMax, interval, position and textFilters - the last being the category
// keys that slider belongs to. A category entry carries the same key as
// textFilter. So the tab a slider belongs to is read, not guessed, and the
// key is stable where the label is not: two installed mods ship the same
// script under different labels, and a translation rewrites any label with a
// "$" prefix.
//
// The built-in categories (RACE, FACE, $COLORS, the paints) carry no
// textFilter at all - they are created by the film itself and identified by
// flag. A category WITH a key is therefore exactly "a tab some mod added",
// and that is the whole population this module reports.
//
// One key is left out: the body scale tab. The Atelier builds that one from a
// node table of its own (Edit/BodyScales), and a second section over the same
// sliders is the duplicate D082 was written to avoid.
//
// HOW A VALUE IS WRITTEN. Not through SKEE either, and the reason is the key.
// SetMorph takes a morph name AND a morph key, and the key is the mod's own
// ("RaceMenuMorphsCBBE.esp" for one of the two here). Nothing in the
// interface hands out a third party's key before that mod has written with
// it, and writing under a key of ours would ADD to the mod's value instead of
// replacing it: the same slider moved here and in RaceMenu would sum.
//
// So the change goes back the way RaceMenu's own slider sends it - the mod
// event "RSM_SliderChange", with the slider's callback name and its value.
// The script that registered the slider is listening for exactly that, and it
// applies the change with its own key and redraws the body the way it always
// does. The Atelier delegates and does not reimplement (project rule 1).
//
// The value is also written back onto the film's own entry, which is what the
// film does when the player drags the slider there. Without it RaceMenu would
// show the value the slider was registered with while the body shows ours.
//
// NOTHING READS THE VALUE BACK, and that is not an oversight. The morph is
// stored under the mod's key, which is the very thing this module does not
// have; the film's entry holds what was last written to it, which is ours
// again. So after a write the page is told the value it asked for. RaceMenu's
// own slider works the same way - it keeps what it sent - so the two never
// disagree with each other, only with a third party that wrote the same morph
// behind both of them. The value IS re-read from the film when the panel
// comes back on screen, which is what catches a mod resetting its own tab.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "body-morphs-get"  {}
//   dll -> page  "body-morphs"      { "ready": bool, "categories": [ <cat>, ... ] }
//                 cat:    { "key": text, "label": text, "sliders": [ <slider>, ... ] }
//                 slider: { "c": callback, "label": text,
//                           "min": num, "max": num, "step": num, "value": num }
//   page -> dll  "body-morph-set"   { "c": callback, "v": num, "final": bool }
//
// "ready" says the film was READ, not that it had tabs. A machine with no
// such mod reads fine and reports none, and that is a different answer from a
// film that could not be reached.

#pragma once

namespace Atelier::Edit::BodyMorphs
{
	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();
}
