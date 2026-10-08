// Freeze.h
// Lifecast - stops the player character where it stands, and plays a pose.
//
// Freeze is not one flag. The animation graph's update bit is cleared, the
// controller is made unpushable and stops recording hits, the AI is turned
// off, the actor stops moving, and the face's blink timer is pinned at its
// maximum (without that last step the face keeps blinking). Every value is
// saved before it is touched, and undoing puts back only what was true
// before - restoring a bit that was already off would switch on something
// nobody asked for.
//
// The freeze never outlives its cause: it is undone when the button is
// pressed again, when a pose is played, when the RaceSex Menu closes, and
// when a game is loaded or started.
//
// Pose is PlayerCharacter::NotifyAnimationGraph with an event name. The
// graph answers true or false; the page is told which, because an event the
// graph refuses must not look like one that worked.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "freeze-get"  {}
//   page -> dll  "freeze-set"  { "on": bool }
//   dll -> page  "freeze"      { "on": bool }
//   page -> dll  "pose-list"   {}
//   dll -> page  "poses"       { "poses": [ { "event": str, "label": str } ] }
//   page -> dll  "pose-play"   { "event": str }       (empty "event" = stop)
//   dll -> page  "pose"        { "event": str, "accepted": bool, "why": str }

#pragma once

namespace Lifecast::Edit::Freeze
{
	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();

	// Undoes a freeze that is on. Safe to call at any time, from the game
	// thread; used by the game-load and new-game messages.
	void Release(const char* a_why);
}
