// RaceMenuTabs.h
// Atelier - opening RaceMenu on the category a delegated control lives in.
//
// The panel's "Open" buttons hand a control back to RaceMenu. Without this
// RaceMenu comes back on whatever category it was on, which after a fresh
// open is its first one. Here the film is asked, through the engine's movie
// API, to press the matching entry of its own category list - the same thing
// a click on that entry does. The film is never modified.
//
// Best effort: the category list is found by name, its entries are read
// back and logged, and when nothing matches RaceMenu is simply left where
// it is. Presets and Sculpt are RaceMenu modes, not categories: for those the
// matching button of the film's mode selector is pressed instead.

#pragma once

namespace Atelier::Panel::RaceMenuTabs
{
	// a_for is the key the page sends with "show-racemenu". Queued on the
	// UI thread; returns at once.
	void Select(const std::string& a_for);
}
