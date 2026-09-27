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
// Four moves, all relative to what the menu showed when it opened:
//
//   turn      the body, about its own vertical axis - RaceMenu's own
//             SetPlayerRotation, the call its camera mode makes (delegated)
//   distance  the camera, along the line from the player to it, in the
//             ground plane - RaceMenu's SetRaceSexCameraPos
//   lift      the camera, up and down - RaceMenu's SetRaceSexCameraPos
//   tilt      the camera node's own rotation, about the horizontal axis
//             across the line of sight. RaceMenu has no call for this (it
//             reads the rotation and never writes it), so the node is
//             written directly: its local rotation, then Update.
//
// Every move is computed from the frame kept at the first move after the menu
// opened - never added to the last written value -, so a long drag does not
// drift and "reframe" is exact: it writes the kept frame back. The kept frame
// is dropped when the menu closes.
//
// Limits are part of the design, not a detail: the camera never comes closer
// to the player than kMinDistance in the ground plane, never goes below the
// player's origin plus kFloorMargin, and never tilts past kTiltMax. The one
// measured way to lose the character is the camera ending up almost on top of
// the player (1 unit away in the plane and 124 above the origin).
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
//                                 "lift": units, "tilt": deg }  (deltas, any
//                                 subset)
//   page -> dll  "stage-reset"  {}
//   dll -> page  "stage"        { "ready": false, "why": text }
//                            or { "ready": true, "tilt": bool,
//                                 "turn": deg, "distance": units,
//                                 "lift": units, "tiltDeg": deg,
//                                 "limits": { "minDistance", "maxDistance",
//                                             "liftMin", "liftMax",
//                                             "tiltMax" } }
//   dll -> page  "stage-visible" { "on": bool }  hides or shows the frame and
//                                 its controls; the view itself is untouched.
//                                 Not sent by this module - it is there for
//                                 whoever needs the panel without the frame.

#pragma once

namespace Atelier::Panel::Stage
{
	inline constexpr double kMinDistance = 24.0;    // camera to player, ground plane
	inline constexpr double kMaxDistance = 320.0;
	inline constexpr double kLiftUp = 80.0;         // above the opening height
	inline constexpr double kFloorMargin = 12.0;    // above the player's origin
	inline constexpr double kTiltMax = 35.0;        // degrees, either way

	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();

	// The same requests as "stage-move" and "stage-reset", for code that is
	// not the page. Game thread; queued like the page's, answered to the page
	// like the page's.
	void Move(double a_turn, double a_distance, double a_lift, double a_tilt);
	void Reset();
}
