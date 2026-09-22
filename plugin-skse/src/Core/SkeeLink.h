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
}
