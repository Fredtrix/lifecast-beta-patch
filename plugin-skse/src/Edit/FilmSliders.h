// FilmSliders.h
// Lifecast - every slider of RaceMenu's list, drawn, written and read back
// through RaceMenu's own film (D243).
//
// WHAT IS A SLIDER HERE. RaceMenu's film keeps one list of entries for all
// its tabs (itemList._entryList): categories (type 0), races (type 1) and
// sliders (type 2). Of the sliders, this module takes every entry that is a
// value on a range and nothing else:
//   - the game's 18 face morphs and every face morph RaceMenu adds - callback
//     ChangeDoubleMorph. The game's carry a plain label ("Nose Length"),
//     RaceMenu's a "$" one ("$CME_LipType"); RaceMenu's whole-number ones
//     (0 to N by 1) pick a mesh part, and are the same kind of entry;
//   - every slider a mod registered through RaceMenu's script interface -
//     sliderID 1000 and up - body morphs and the body scales among them.
// The rest of type 2 (the lists, the colours, sex, weight, the race preset)
// has its own section in the panel and stays out; what stays out is counted
// by callback in the log, every census.
//
// THE KEY IS THE LABEL, not the sliderID: the ID is a position in the list,
// and it moves when a mod adds entries before it. The key is the label plus
// the callback (two mods can use one label under different callbacks), and a
// repeat of both gets "#2", "#3"... in list order.
//
// WHERE AN ENTRY GOES. The film puts an entry in a tab by bit (the entry's
// filterFlag against the tab's flag) or by text key (the entry's textFilters
// against the tab's textFilter). The page gets the tab, read the same way;
// the "all" tab, whose flag carries every bit, is not a tab of its own here.
// Labels go through the film's own translator, the one RaceMenu draws with.
//
// HOW A VALUE IS WRITTEN. The way RaceMenu's own slider writes it, in its
// order: the GameDelegate callback with the value and the entry's sliderID
// (that is what moves the game's morphs and RaceMenu's), the entry's
// "position", the mod event "RSM_SliderChange" with the callback and the
// value (that is what a mod's script listens for), and on the last value of
// a gesture the list is told to redraw. Nothing is written to the actor from
// here. The sliderID is looked up again for every write, by key.
//
// HOW A VALUE IS READ BACK. From the entry's "position", which is what
// RaceMenu shows: when the panel comes on screen, after a preset or a race
// change replaced the character, and once a second while the panel is on
// screen (a mod registers its tab a moment after the menu opens, and changing
// the sex makes it register a different list under the same key).
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "sliders-get"    {}
//   dll -> page  "sliders"        { "ready": bool,
//                                   "tabs": [ { "id", "text", "label", "key", "flag" }, ... ],
//                                   "sliders": [ { "k", "text", "label", "kind", "tab",
//                                                  "min", "max", "step", "value" }, ... ] }
//                 kind: "engine" | "skee" | "skee-whole" | "plugin"; tab: a tab id or "";
//                 value: null when the film holds no usable number
//   dll -> page  "slider-values"  { "values": { k: value, ... } }   - only what moved
//   page -> dll  "slider-set"     { "k": key, "v": value, "final": bool }
//   page -> dll  "sliders-drawn"  { "rows", "sections": { key: n }, "notDrawn": [ k, ... ] }
//                 logged as it comes: what the page did with the last list.

#pragma once

namespace Lifecast::Edit::FilmSliders
{
	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();
}
