// BodyScales.h
// Lifecast
//
// The body scale sliders: height, head, breasts, glutes, biceps and the
// weapon nodes, each a scale on one skeleton node.
//
// They are the same scales RaceMenu's own body sliders write, through the
// same SKEE NiTransform interface and under the same key ("RSMPlugin").
// RaceMenu keeps no copy of these values - it reads the key back from SKEE
// when it builds its sliders - so a value set here is the value RaceMenu
// shows, and the other way round. There is one state, not two to reconcile.
//
// Writing follows RaceMenu: a scale other than 1.0 is added for both the
// third- and first-person skeleton; 1.0 removes the key instead, so an
// untouched node carries nothing. Each write is followed by SKEE's
// UpdateNodeTransforms for that node, which shows it at once and costs a
// fraction of a millisecond - no DoReset3D.
//
// A slider is offered only if its node exists on the player's third-person
// skeleton, and the breast and glute sliders only for a female player. Both
// are read again every time the list is sent, because RaceMenu can change
// race and sex while the panel is open.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "scales-get"  {}
//   dll -> page  "scales"      { "ready": bool,
//                                "nodes": [ { "k": key, "label": text,
//                                             "min": a, "max": b,
//                                             "value": v }, ... ] }
//   page -> dll  "scales-set"  { "k": key, "v": value, "final": bool }

#pragma once

namespace Lifecast::Edit::BodyScales
{
	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();
}
