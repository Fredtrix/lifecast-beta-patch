// Session.h
// Atelier
//
// What every editing section shares while the RaceSex Menu is open: whether
// the menu is open, a tick that runs about once a frame, the moment the panel
// comes on screen, and the one expensive redraw.
//
// The redraw is Actor::DoReset3D(true). It rebuilds the whole actor and costs
// more than a frame, and it is the only call that makes most edits show with
// RaceMenu open: face morphs, the nose/eye/mouth types, head parts, weight,
// complexion and tint masks all stayed invisible until it ran. (Each has a
// lighter update since, in its own section; face morphs, the nose/eye/mouth
// types and complexion go through RaceMenu's own slider callbacks. This
// redraw stays as their fallback, and for a head part missing on one side.)
//
// A dragged control sends many values a second, and a list stepped with its
// arrows sends one per click, so writing and redrawing are separated:
//
//   every value      -> written at once by the section (cheap)
//   redraw           -> at most once per kMinRedrawGap, dragging or not; a
//                       finished edit is redrawn as soon as the gap allows
//
// Every redraw is timed and logged, so its cost is measured on every run
// rather than known from one sample.

#pragma once

namespace Atelier::Edit::Session
{
	using Callback = void (*)();

	// The actor is redrawn at most this often, while dragging and between
	// finished edits alike.
	inline constexpr auto kMinRedrawGap = std::chrono::milliseconds(250);

	// Registers the menu and input sinks. Call once, at kDataLoaded, before
	// any section installs. Safe to call twice.
	void Install();

	bool MenuOpen();

	// Asks for a DoReset3D(true) on the next tick the throttle allows;
	// a_now marks a finished edit, logged as such. Several requests between
	// two redraws cost one redraw.
	void RequestRedraw(bool a_now);

	// Registration, one callback per call; all run on the game thread.
	void OnTick(Callback a_fn);         // every input tick, menu open or not
	void OnPanelShown(Callback a_fn);   // the panel came on screen, menu open
	void OnMenuOpened(Callback a_fn);
	void OnMenuClosed(Callback a_fn);   // after the last pending redraw
	void OnRedrawn(Callback a_fn);      // right after each DoReset3D this module runs
}
