// Tints.h
// Atelier
//
// The tint masks: skin tone, lips, cheeks, eye sockets, eyeliner, the other
// face tints, and the two picked ones - war paint and dirt.
//
// They are the player's own tint list (PlayerCharacter::GetTintList), one
// mask per texture, each with a colour and an intensity. A mask is found by
// its type, never by its position: the list differs per race and sex (33
// masks for a Nord woman, 34 for a Nord man, measured), and RaceMenu rebuilds
// it on a race or sex change.
//
//   plain tint   one mask of its type. The page edits its colour and its
//                intensity.
//   picked tint  several masks of one type, one texture each (war paint has
//                fifteen for a Nord). The control shows the first one with
//                any intensity; picking another moves the colour and the
//                intensity to it and clears the one it came from. Picking
//                "none" clears every mask of the type.
//
// The war paint offered here is the race's own paint masks. RaceMenu's list
// is larger - a catalog of paint textures, filled by its scripts and by
// other mods, applied by changing a mask's texture - and that catalog is not
// reachable from a plugin; the page sends the player to RaceMenu for it.
//
// Swatches are the race's own preset colours for that texture, when the race
// has them; any colour can be set regardless.
//
// Redraw: a tint is shown by Game.UpdateTintMaskColors(), called through the
// Papyrus VM the way RaceMenu's own script does it (Core/TintRefresh.h). It
// used to be the shared DoReset3D(true) (Edit/Session.h), which leaves HDT-SMP
// hair nodes behind on every call. Actor::UpdateSkinColor() alone did nothing
// visible to the face with RaceMenu open; it now only carries the skin tone
// to the body.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "tints-get"  {}
//   dll -> page  "tints"      { "ready": bool, "layers": [ <layer>, ... ] }
//                 layer: { "k": key, "label": text, "kind": "plain" | "pick",
//                          "color": "#RRGGBB", "alpha": 0..1,
//                          "swatches": [ "#RRGGBB", ... ],
//                          pick only: "count": N, "index": i (-1 = none) }
//   page -> dll  "tint-set"   { "k": key, "final": bool, and any of
//                               "color": "#RRGGBB", "alpha": 0..1,
//                               "index": i (pick only, -1 = none) }

#pragma once

namespace Atelier::Edit::Tints
{
	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();
}
