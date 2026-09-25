// ObodyApi.h
// Atelier
//
// Wraps the vendored OBody plugin-API header so the rest of the project can
// use it without the vendored file being edited.
//
// The vendored copy at extern/obody-api/API.h is byte for byte upstream's
// file at tag 4.4.3 - the version installed on this instance - and its sha256
// is recorded in the ORIGEM.md next to it. It is meant to stay that way.
//
// WHY A WRAPPER AT ALL. The header says of itself that Skyrim game types such
// as Actor and TESForm "are used by this header, but are left undefined so
// that you can define them yourself before including this header". So the
// aliases below are not a workaround for the header - they are the header's
// own documented calling convention, and supplying them from outside is what
// keeps the vendored file verbatim.
//
// WHY AT GLOBAL SCOPE, WHICH Skee.h DELIBERATELY AVOIDS. The sibling wrapper
// includes the SKEE header inside "namespace Skee" and says exactly why that
// is safe there: "the vendored header includes nothing at all". This one is
// not in that position - it includes <limits> and <string_view>, and pulling
// the standard library into a namespace is ill-formed. So the namespace trick
// is off the table here, and the aliases go where the header asks for them.
//
// WHAT THAT COSTS, stated rather than discovered later: ::Actor and ::TESForm
// become names at global scope for every translation unit that includes this
// file. CommonLibSSE-NG keeps its own types in RE::, so nothing it declares
// collides, and Skee.h keeps its forward declarations inside namespace Skee.
// Two .cpp files include it, and only .cpp files may: Core/ObodyLink.cpp,
// which asks OBody for the interface, and Edit/Body.cpp, the body section of
// the panel. Reconsidered when the second one arrived: the aliases reach
// those two translation units and nothing else, because no header includes
// this one (Core/ObodyLink.h forward-declares the one type it names). Keep it
// that way - a header including this file would carry the cost everywhere.
//
// WHAT THE PROJECT GETS IN EXCHANGE, and it is the reason the hand-written
// mirror of the interface had to go: here OBody::API::Actor is RE::Actor
// here, not an incomplete type of some private namespace, so an actor can be
// passed straight through and the interface's own methods can be called. A
// vtable transcribed by hand is right until it is not, and it fails as a
// wrong call inside somebody else's process rather than as a compile error.

#pragma once

// The vendored header includes these itself. They are named here as well so
// that the dependency is visible at the point of use rather than inherited
// from whatever the PCH happened to pull in.
#include <cstdint>
#include <limits>
#include <string_view>

// The two types the vendored header leaves undefined, supplied the way it
// asks. Aliases, not forward declarations: a forward declaration would make
// these distinct incomplete types and every call taking an Actor* would need
// a cast that hides whether the conversion is even correct.
using Actor = RE::Actor;
using TESForm = RE::TESForm;

#include "obody-api/API.h"
