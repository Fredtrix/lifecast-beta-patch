// TintRefresh.h
// Lifecast - showing a tint mask change without rebuilding the actor.
//
// A tint mask edit used to show only after Actor::DoReset3D(true), and every
// reset leaves HDT-SMP hair physics nodes behind (about 210 per reset,
// measured in the game): the redraw grew slower the longer the menu stayed
// open. RaceMenu's own script shows a mask colour change with the SKSE
// Papyrus function Game.UpdateTintMaskColors() followed by the mod event
// "RSM_ShadersInvalidated", and no reset. This does the same from C++: the
// Papyrus function is called through the VM, so the game address it needs is
// SKSE's business, not ours.
//
// The call is asynchronous. One is in flight at a time; requests that arrive
// meanwhile are folded into one more call when it returns. Every call is
// timed from dispatch to return and logged ("[tintfix]").

#pragma once

namespace Lifecast::Core::TintRefresh
{
	// Asks for a refresh. a_final marks the last write of a drag; it only
	// changes what is logged.
	void Request(bool a_final);

	// Per-menu-session totals, logged and reset. Call when the menu closes.
	void LogAndReset();
}
