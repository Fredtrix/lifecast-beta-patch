// RaceMenuFilm.h
// Lifecast - showing in RaceMenu's sliders what the panel changed.
//
// RaceMenu's film draws its sliders from its own list of entries, and each
// entry keeps the value it last showed in "position". An edit made in the
// panel reaches the game, but not that list: back in RaceMenu the slider
// shows the old value, and touching it writes from the old value.
//
// So every time RaceMenu comes back to the front, the entries this module
// knows are compared with the game's state, the ones that differ get their
// "position" set, and the list is asked to redraw. It is the film's data
// that changes, through the engine's movie API; the film itself is never
// modified.
//
// What is kept in step:
//   - the game's 18 face morphs (entries with callback ChangeDoubleMorph),
//     by label, first entry of each label only - RaceMenu adds sliders of
//     its own under the same callback, and some share a vanilla label;
//   - the nose, eye and mouth types (ChangePreset), by label;
//   - the head parts (ChangeHeadPart): each entry's own part list is asked
//     for (GetSliderPartData, with the entry's sliderID), and the part the
//     player wears is found in it by editor ID - no label needed;
//   - the complexion (ChangeFaceDetails), as its index among the race's
//     face detail sets.
//   - the weight (ChangeWeight), which that slider shows from 0 to 1;
//   - the Presets slider (ChangeHeadPreset), which only moves when the Lifecast
//     hands it a preset: the game does not keep which one is worn, so this
//     remembers the last index handed over.
// Hair colour and tint masks are not here: RaceMenu re-reads those itself
// on the mod events the Lifecast sends (Core/RaceMenuSync).
//
// Labels are the English ones RaceMenu shows. An entry whose label is not
// known is left alone.

//
// The other direction: a face edit made in the panel can be handed to the
// same GameDelegate callback RaceMenu's own slider calls, with the two
// arguments that slider sends - the value and the entry's sliderID. The
// menu's handler then reshapes the head in place, where DoReset3D rebuilds
// the whole actor (and, with HDT-SMP hair, leaves nodes behind every time).

#pragma once

namespace Lifecast::Panel::RaceMenuFilm
{
	// Queued on the UI thread; returns at once. Call right after RaceMenu's
	// film is made visible again.
	void Sync();

	// Queued on the UI thread; returns at once. Call when the panel comes back
	// on screen. Reads RaceMenu's own Presets slider and, if the player moved it
	// while the panel was away, calls a_found with the preset index it rests on
	// (and remembers it, so the next Sync writes it back unchanged).
	void ReadNativePreset(std::function<void(int)> a_found);

	enum class Slider
	{
		kMorph,       // a_which: the faceData morph index, 0 to 17
		kType,        // a_which: the faceData part index (nose 0, eyes 2, mouth 3)
		kComplexion,  // a_which: unused
		kHeadPreset,  // a_which: unused; the value is the race's preset index
	};

	// a_delivered: whether RaceMenu's callback was called. When it was not
	// (no film, no such entry, no such callback), the caller shows the value
	// its own way. a_final: whether any request folded into this call was a
	// finished edit.
	using Done = std::function<void(bool a_delivered, bool a_final)>;

	// Queued on the UI thread; returns at once. Requests for the same slider
	// that arrive before the queued call runs fold into one, with the last
	// value and the last a_done.
	void Apply(Slider a_slider, int a_which, double a_value, bool a_final, Done a_done);

	// The sex and race changes, handed to the same GameDelegate callbacks
	// RaceMenu's own Sex slider and race list call ("ChangeSex": value 0 male,
	// 1 female, and the entry's sliderID; "ChangeRace": the race's entry in the
	// film's race list, and -1). The game then does what it does for those
	// controls; nothing here decides what the new race or sex keeps. The race
	// is found in the film's list by its name. Queued on the UI thread; a_done
	// runs there with whether the callback was called.
	void ChangeSex(bool a_female, std::function<void(bool a_called)> a_done);
	void ChangeRace(std::string a_raceName, std::function<void(bool a_called)> a_done);

	// Queued on the UI thread; returns at once. Logs which row the film's race list
	// marks as the player's own (listState.activeEntry), and why it was read.
	void LogRaceMarker(std::string a_why);
}
