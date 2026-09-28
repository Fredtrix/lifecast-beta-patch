// Stage.h
// Atelier - the character view: a frame in the page, and the camera and body
// moves that put the character in it.
//
// Nothing is drawn here. The page is transparent outside the panel and the
// game keeps drawing the RaceSex Menu's character behind it; the frame is a
// region of the page, and what shows inside it is decided by the camera. So
// this module moves the camera and turns the body, and the page only draws a
// border and the controls around it.
//
// Five moves, all relative to what the menu showed when it opened:
//
//   turn      the body, about its own vertical axis - RaceMenu's own
//             SetPlayerRotation, the call its camera mode makes (delegated)
//   distance  the camera, along the line from the player to it, in the
//             ground plane - RaceMenu's SetRaceSexCameraPos
//   lift      the camera, up and down - RaceMenu's SetRaceSexCameraPos
//   pan       the camera, sideways - perpendicular to the player-camera
//             line, in the ground plane - RaceMenu's SetRaceSexCameraPos.
//             E4, Fase 6.5: the right-button drag moved lift only; the
//             author measured that as "so mexe na vertical" and this is
//             the axis that was missing.
//   tilt      the camera node's own rotation, about the horizontal axis
//             across the line of sight. RaceMenu has no call for this (it
//             reads the rotation and never writes it), so the node is
//             written directly: its local rotation, then Update.
//
// Every move is computed from the frame kept at the first move after the menu
// opened - never added to the last written value -, so a long drag does not
// drift. The kept frame is dropped when the menu closes.
//
// A move sets a TARGET and the camera slides to it, because applying a move
// whole in the frame it arrives in reads as jumpy: a button click is 15
// degrees at once, and a drag arrives as steps 50 ms apart. So "stage-move"
// moves the target and nothing else; a tick moves the current position a
// fraction of the gap that is left and writes camera, body and tilt; and the
// state sent back to the page is the TARGET, which is what the player asked
// for and what a button sitting at a limit has to reflect. Both are offsets
// from the kept frame - the easing lives between them, and neither is ever
// read back from the engine.
//
// So "reframe" is no longer exact at the instant of the click: it sets the
// target to the kept frame, and the kept frame is written when the current
// position gets there. Arrival is real and not asymptotic - inside the snap
// epsilon the current position is set to the target - which is what lets a
// button at a limit come back and what ends the tick.
//
// Limits are clamped on the TARGET, never on the current position: clamping
// what is about to be written would stop a button for a reason the page has
// no way to show. A target that gets clamped says so in the log.
//
// Limits are part of the design, not a detail: the camera never comes closer
// to the player than kMinDistance in the ground plane, never goes below the
// player's origin plus kFloorMargin, never strays sideways past kPanMax, and
// never tilts past kTiltMax. The one measured way to lose the character is
// the camera ending up almost on top of the player (1 unit away in the plane
// and 124 above the origin). kPanMax is not measured the same way - it has
// no failure mode like that to bound it against, so it is set to the same
// order of magnitude as kLiftUp and is the author's to retune at OLHO DO
// AUTOR (E4, Fase 6.5).
//
// Every RaceMenu call and every node write runs on the UI thread, like
// RaceMenu's own calls from its film; the answer to the page is sent from
// the game thread. Moves that arrive while one is queued are folded into it.
//
// Refusal: without the RaceSex Menu's film, without RaceMenu's CharGen object
// or any of the five calls, or with the camera on top of the player, the
// page is told "not ready" with the reason, and the frame shows it. Nothing
// else in the panel depends on this module.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "stage-get"    {}
//   page -> dll  "stage-move"   { "turn": deg, "distance": units,
//                                 "lift": units, "pan": units,
//                                 "tilt": deg }  (deltas, any subset)
//   page -> dll  "stage-reset"  {}
//   dll -> page  "stage"        { "ready": false, "why": text }
//                            or { "ready": true, "tilt": bool,
//                                 "turn": deg, "distance": units,
//                                 "lift": units, "pan": units,
//                                 "tiltDeg": deg,
//                                 "limits": { "minDistance", "maxDistance",
//                                             "liftMin", "liftMax",
//                                             "panMin", "panMax",
//                                             "tiltMax" } }
//   dll -> page  "stage-visible" { "on": bool }  hides or shows the frame and
//                                 its controls; the view itself is untouched.
//                                 Not sent by this module - it is there for
//                                 whoever needs the panel without the frame.

#pragma once

namespace Atelier::Panel::Stage
{
	inline constexpr double kMinDistance = 33.56;   // camera to player, ground plane
	inline constexpr double kMaxDistance = 320.0;
	inline constexpr double kLiftUp = 80.0;         // above the opening height
	inline constexpr double kFloorMargin = 12.0;    // above the player's origin
	inline constexpr double kPanMax = 80.0;         // sideways, either way - see note above
	inline constexpr double kTiltMax = 35.0;        // degrees, either way

	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();

	// The same requests as "stage-move" and "stage-reset", for code that is
	// not the page. Game thread; queued like the page's, answered to the page
	// like the page's.
	void Move(double a_turn, double a_distance, double a_lift, double a_pan, double a_tilt);
	void Reset();

	// One step of the slide, when there is one under way. Game thread, and a
	// single atomic read when there is not - it is called from the input tick
	// the Switcher already runs, measured at about 66 a second whether the
	// panel holds focus or not, rather than from a second tick of its own.
	// The step itself is queued to the UI thread like every other move.
	void Tick();
}
