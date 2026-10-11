// Tints.h
// Lifecast
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
// WRONG, on the last clause, and kept because it is the written reason war
// paint was left a door: the catalog IS reachable. It is not on any SKEE
// interface, which is what that sentence was reasoning from, but RaceMenu's
// own film holds it, and Edit/Overlays reads it there for the four paint
// overlays. Whether the war paint half is worth offering here is a separate
// question, open and named in D084 - the claim of impossibility is not.
//
// It is offered here now (Fase 7.5, D125): the War Paint layer carries the
// catalog Edit/Overlays reads off the film, and choosing an entry changes the
// TEXTURE of the mask that is showing - the same field SKSE's
// Game.SetTintMaskTexturePath writes when RaceMenu applies a catalog paint.
// RaceMenu is then asked to take a fresh copy of the masks
// ("RSM_RequestTintSave"), because its script re-applies the textures it
// holds when a save is loaded.
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
//                          pick only: "count": N, "index": i (-1 = none),
//                          war paint only: "texture": path of the mask
//                          showing ("" when none), "catalog": [ { "name":
//                          text, "texture": path }, ... ] }
//   page -> dll  "tint-set"   { "k": key, "final": bool, and any of
//                               "color": "#RRGGBB", "alpha": 0..1,
//                               "index": i (pick only, -1 = none),
//                               "texture": path (war paint only, from the
//                               catalog; with nothing showing, the first
//                               mask takes it at full intensity) }

#pragma once

namespace Lifecast::Edit::Tints
{
	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();

	// Sends the layers to the page again. Edit/Overlays calls it when the war
	// paint catalog has been read off the film.
	void Resend(const char* a_why);

	// What RaceMenu's script hands its film for the masks (UpdateColors in
	// racemenu.psc), read from the game now, for Edit/FilmPaint: the hair
	// colour first, then one "type;;ARGB;;texture" row per mask in the
	// player's list order, ARGB as the signed decimal a Papyrus Int prints.
	// Empty when the player has no tint list yet.
	std::vector<std::string> FilmTintRows();

	// The first mask of a type that shows, as its index among the player's
	// masks of that type, or -1; a_showing gets how many show, and a_argb
	// that mask's colour in the FilmTintRows form (untouched when none shows).
	int ShownMask(RE::TintMask::Type a_type, int& a_showing, std::int32_t& a_argb);
}
