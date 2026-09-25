// Skee.h
// Atelier
//
// Wraps the vendored SKEE ModderResource header so the rest of the project
// can use it without the vendored file being edited.
//
// The vendored copy at extern/skee-modderresource/IPluginInterface.h is byte
// for byte the file the RaceMenu Anniversary Edition install ships - its
// sha256 is recorded in the ORIGEM.md next to it, and it is meant to stay
// that way. It predates CommonLibSSE-NG's RE:: namespace and forward-declares
// TESObjectREFR, Actor and the rest at global scope, which collides with the
// real RE:: types that PCH.h already brings in.
//
// The obvious fix is to edit the copy - delete the forward declarations,
// open "namespace SKEE" inside the file, alias the RE:: types in - and it is
// the wrong one. A copy edited that way can no longer be checked against the
// file it came from, and a vendored copy measured on 2026-09-22 had drifted
// exactly that way: 505 lines against the 536 the ModderResource ships,
// missing kPluginVersion5 and the whole IFormTagInterface, while its own
// preamble still called it verbatim. Including the file from the outside
// costs one wrapper and keeps the claim true: re-vendoring is a copy, and the
// sha256 check catches a stale one.
//
// WHAT THIS COSTS. Skee::Actor, Skee::TESObjectREFR and the other
// forward-declared names are incomplete types of this namespace, NOT aliases
// of RE::Actor and friends. The conversion was decided where the first actor
// crosses over, SkeeLink::ClearSkinOverrides, with the multi-runtime layout
// rule of the shared C++ conventions in hand ("never reach a base class of
// Actor through the C++ hierarchy"): the actor goes over as the address of
// the engine object, reinterpret_cast from the RE::Actor pointer the engine
// handed out, and never through a static_cast to one of its bases. Anything
// else that passes an actor to SKEE converts the same way, and says so.

#pragma once

#include <cstdint>

namespace Skee
{
// The vendored header includes nothing at all - "#pragma once" and then
// declarations - which is what makes including it inside a namespace safe.
// It must never be included anywhere else, at global scope or in a second
// namespace: two different types with one definition is an ODR violation.
#include "skee-modderresource/IPluginInterface.h"
}
