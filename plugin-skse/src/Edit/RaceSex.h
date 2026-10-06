// RaceSex.h
// Lifecast
//
// The player's race and sex - RaceMenu's "Race & Sex" door, moved into the
// Lifecast (D125) as two controls: the race picker in the Race category and
// the sex in Body (D133).
//
// Race: Actor::SwitchRace(race, true), the one public call that changes an
// actor's race. The list is every TESRace flagged playable and not child,
// minus the vampire races (keyword "Vampire"), in load order.
//
// Sex: there is no setter. TESNPC::GetSex() reads the base's kFemale flag, so
// the change is TESActorBaseData::SetActorBaseFlag(kFemale, set, true).
//
// Neither call says what it rebuilds. After either one this goes through
// Session::Replaced - every section drops what it kept and, with the panel on
// screen, re-reads - and then the shared DoReset3D(true) (Edit/Session.h).
// Every change logs the same fields before and after, read back from the
// game: the race's FormID, the sex, the head parts and the actor's 3D root.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "race-sex-get"     {}
//   dll -> page  "race-sex"         { "ready": bool, "races": [ { "name", "id" } ],
//                                     "race": index or -1, "female": bool }
//   page -> dll  "race-set"         { "i": index }
//   page -> dll  "sex-set"          { "female": bool }
//   dll -> page  "race-sex-applied" { "what": "race" | "sex", "ok": bool, "why": text }
//
// The DLL pushes "race-sex" when the panel comes on screen and after every
// change, read back from the game.

#pragma once

namespace Lifecast::Edit::RaceSex
{
	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();
}
