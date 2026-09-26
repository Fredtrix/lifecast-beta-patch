// SkeeLink.h
// Atelier
//
// The handshake with SKEE, the plugin half of RaceMenu. The Atelier dresses
// the RaceMenu chargen editor and hands the expensive work back to it, so
// what SKEE does and does not expose to an outside plugin is a load-bearing
// fact of the design, not a detail.
//
// The vendored header is included through Skee.h, inside a namespace. See the
// note there before touching the types.

#pragma once

#include "Skee.h"

namespace Atelier::Core::SkeeLink
{
	// Dispatches InterfaceExchangeMessage to "skee" and caches the map.
	// Idempotent: only dispatches while it has no map.
	//
	// Returns nullptr when SKEE did not answer, which is the same answer as
	// "RaceMenu is not installed". The log line distinguishes the two as far
	// as anything here can.
	Skee::IInterfaceMap* GetInterfaceMap();

	// Queries every interface SKEE is known to register and writes one log
	// line per name saying whether it came back.
	//
	// a_moment labels the round in the log. The moment matters and is the
	// reason this takes a label at all: an earlier measurement got
	// QueryInterface("Preset") == 0x0 and never recorded whether the RaceSex
	// Menu was open at the time, which left the null unreadable - absent, or
	// merely not yet there. That can only be settled by asking at more than
	// one moment and writing the moment down next to the answer.
	void ReportInterfaces(const char* a_moment);

	// Removes every skin override SKEE holds for a_actor, through SKEE's
	// Override interface (RemoveAllSkinOverridesByReference). Loading a
	// preset does not do this: a preset without skin overrides, or with an
	// empty list, leaves the ones already on the character, and one with a
	// different index adds to them.
	//
	// False, and nothing removed, when the interface is not there or reports
	// a version older than the vendored header describes.
	bool ClearSkinOverrides(RE::Actor* a_actor);

	// One body morph value SKEE holds for an actor: the morph name, the key
	// it was written under (the mod that wrote it - "OBody", for instance)
	// and the value. The same pair a .jslot keeps under "bodyMorphs".
	struct MorphValue
	{
		std::string name;
		std::string key;
		float       value{ 0.0f };
	};

	// Every body morph value SKEE holds for a_actor, read through SKEE's
	// BodyMorph interface (VisitMorphValues) - nothing is written.
	//
	// False, and a_out left empty, when the interface is not there or reports
	// a version older than the vendored header describes.
	bool ReadBodyMorphs(RE::Actor* a_actor, std::vector<MorphValue>& a_out);
}
