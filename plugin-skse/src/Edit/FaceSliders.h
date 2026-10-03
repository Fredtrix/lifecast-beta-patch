// FaceSliders.h
// Lifecast
//
// The face morph sliders: the page drags, this module writes the value into
// the player's face data and has the engine redraw it, with the RaceSex Menu
// open.
//
// The values are the player's TESNPC::FaceData::morphs - the chargen face
// morphs, in the order of the NPC_ record's NAM9 field (nose, jaw, cheeks,
// eyes, brows, lips, chin; xEdit names every entry). Only the first
// kUsableMorphs entries are chargen sliders. The last one is the one xEdit
// calls VampireMorph; on the player it held FLT_MAX, and it is never read or
// written here.
//
// A value is written at once; the redraw that makes it show is throttled and
// shared with the other sections (Edit/Session.h).
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "face-get"  {}
//   dll -> page  "face"      { "usable": N, "ready": bool, "morphs": [v0, v1, ...] }
//                             an entry that is not a plausible value is null
//   page -> dll  "face-set"  { "i": index, "v": value, "final": bool }
//
// The DLL also pushes "face" by itself every time the panel comes on screen,
// because the player may have changed the same values in RaceMenu meanwhile,
// and after every final write, read back from the structure.

#pragma once

namespace Lifecast::Edit::FaceSliders
{
	// Morphs [0, kUsableMorphs) are sliders; the entry after them is not.
	// Measured in the game: 18 plausible values, then FLT_MAX.
	inline constexpr int kUsableMorphs = 18;

	// The range a value is clamped to. The player's own values seen so far
	// sat between -0.46 and 0.70, and a write of +-1.0 drew a plausible
	// face; nothing has been measured outside this range.
	inline constexpr float kMinValue = -1.0F;
	inline constexpr float kMaxValue = 1.0F;

	// A stored value beyond this is not a slider position but a placeholder.
	// Measured in the game: on a new game the RaceSex Menu opens with all 18
	// morphs at FLT_MAX, and real values arrive about 110 ms later. Until
	// every usable morph is inside this bound the face is "not ready": the
	// page gets null for those entries (its sliders stay disabled), writes
	// are refused, and the values are sent again the moment they arrive.
	inline constexpr float kPlausibleLimit = 10.0F;

	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();
}
