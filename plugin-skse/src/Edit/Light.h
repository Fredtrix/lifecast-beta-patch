// Light.h
// Atelier - the stage light RaceMenu already creates.
//
// RaceMenu creates and lights its own chargen light when the menu opens, and
// toggles it with "RSM_ToggleLight" (RaceMenu.as:1389-1398, racemenu.psc:67,
// :566-575; Docs/DECISIONS.md D065). We never read its state back - there is
// no such event - so "on" at menu open is an INFERENCE from that construction,
// not a measurement. The button we offer is a toggle that says what pressing
// it does, not a label that claims a state, because the game's own hotkey
// (LCtrl, "Light Off") can flip the real light without telling us: our
// tracked g_on can go stale the first time the player uses it, same as
// RaceMenu's own bar would.
//
// The rule: whatever this phase leaves off, it turns back on before the menu
// closes. No light state this feature changed survives the close.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "light-get"  {}
//   dll -> page  "light"      { "on": bool }
//   page -> dll  "light-set"  { "on": bool }

#pragma once

namespace Atelier::Edit::Light
{
	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();
}
