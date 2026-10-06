// Overlays.h
// Lifecast
//
// The paint overlays: body, hand, foot and face. Four of RaceMenu's five
// paint categories - the fifth, $MAKEUP, is a tint mask and lives in
// Edit/Tints.h, which is a different surface with a different writer.
//
// An overlay is a skin node the SKEE plugin installs over the character, one
// per layer, named by the layer's area and number ("Body [Ovl0]"). How many
// exist is the player's setting, not ours: skee64.ini declares a count per
// area, and IOverlayInterface::GetOverlayCount reports it. A layer the
// player has not painted still exists - it carries the default transparent
// texture - so the count is the number of slots, not the number in use.
//
// What a layer holds is not on the layer: it is a set of node overrides SKEE
// keeps for the actor, addressed by the node's name and a numeric key. The
// keys are RaceMenu's own, read from its script rather than guessed:
//
//   0  glow colour   int, ARGB; the alpha byte is the glow strength
//   1  glow multiple float, the alpha byte over ten
//   7  tint colour   int, ARGB; the alpha byte is the intensity
//   8  alpha         float, the alpha byte over 255
//   9  texture       string, at texture index 0
//
// Colour and strength are written twice on purpose, once packed into the int
// and once as the float: that is the pair RaceMenu writes and the pair its
// own read expects to find, and writing only one leaves the other stale.
//
// Reading follows RaceMenu too, and the order matters: when the actor
// actually carries the node, the value is read from the node itself
// (GetNodeProperty) because that is what the character is showing; only when
// the node is absent - a layer not yet installed on this body - does the
// stored override answer (GetNodeOverride).
//
// The catalog of textures a layer can take is NOT ours and is not on any
// SKEE interface: mods register their paints into RaceMenu's own film
// through Papyrus, and the film keeps them in a list of five arrays, one per
// paint category. It is read the same way Panel/RaceMenuFilm reads the
// slider list - through the engine's movie API, on the UI thread, without
// the film being modified. That read is why a catalog arrives a moment after
// the layers do.
//
// Spell overlays ("Body [SOvl0]") are left alone. They exist, and SKEE
// counts them separately, but they belong to whatever spell installed them;
// RaceMenu does not offer them either.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "overlays-get"  {}
//   dll -> page  "overlays"      { "ready": bool, "areas": [ <area>, ... ] }
//                 area:  { "k": key, "label": text,
//                          "catalog": [ { "name": text, "texture": path }, ... ],
//                          "layers":  [ <layer>, ... ] }
//                 layer: { "i": index, "texture": path, "name": text,
//                          "color": "#RRGGBB", "alpha": 0..1,
//                          "glow": "#RRGGBB", "glowAlpha": 0..1 }
//   page -> dll  "overlay-set"   { "k": key, "i": index, "final": bool, and any
//                                  of "texture" (path, "" clears), "color",
//                                  "alpha", "glow", "glowAlpha" }

#pragma once

namespace Lifecast::Edit::Overlays
{
	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();

	// The war paint catalog: the first of the film's five paint lists, read
	// with the other four. War paint is a tint mask, not an overlay, so it is
	// not offered here but on the War Paint layer of Edit/Tints. Empty until
	// the film has been read; Edit/Tints is told each time it is read again.
	struct WarPaint
	{
		std::string name;
		std::string texture;
	};
	std::vector<WarPaint> WarPaintCatalog();
}
