// FilmPaint.h
// Lifecast - RaceMenu's tint, makeup and paint entries, rewritten from the
// game every time RaceMenu comes to the front (D245).
//
// The panel writes tints and paint overlays straight into the game. RaceMenu's
// film keeps its own copy of them on its slider list, and that copy goes stale:
//   - the colour sliders and the makeup rows are pushed to the film by
//     RaceMenu's script, through Papyrus - and on some launches that push never
//     reached the film at all, which then had no makeup or paint rows;
//   - the paint overlay rows are only pushed when the menu opens, and their
//     colour in the game follows the skin tone on every tint change;
//   - the War Paint and Dirt pick sliders are moved by nothing. Their
//     "position" is also the mask index the film's own colour slider writes
//     to, so a stale pick made the next colour change in RaceMenu light a
//     second mask (two paints on one face).
//
// So, right after RaceMenu's film is made visible, this reads the game and
// hands the film what RaceMenu's script would hand it, calling the same film
// functions with text in the script's own format (UpdateColors and
// UpdateOverlay in racemenu.psc): RSM_AddTints for the colour sliders and the
// makeup rows, RSM_AddBodyTints / RSM_AddHandTints / RSM_AddFeetTints /
// RSM_AddFaceTints for the paint overlays. Colours go as the signed decimal
// a Papyrus Int prints; the film reads the alpha from the high byte. Then the
// two pick sliders are set to the first mask of their type that shows (-1 for
// none) and the list redraws. No Papyrus, no 3D reset; only the film's data
// changes, through the engine's movie API - the film itself is never modified.
//
// Not done here, on purpose: RaceMenu's post-preset routine
// (CharGen.ReloadSliders and RSM_RequestTintLoad) rebuilds war paint from the
// NPC record and wipes paint the record does not hold, and RSM_Reinitialized
// adds every plugin slider to the list once more each time it is sent.
//
// Two masks of one picked type showing at once: the pick goes to the first.

#pragma once

namespace Lifecast::Edit::FilmPaint
{
	// Queued on the UI thread; returns at once. Call right after RaceMenu's
	// film is made visible again, after Panel::RaceMenuFilm::Sync(). a_why is
	// logged and must outlive the call (a string literal).
	void Sync(const char* a_why);
}
