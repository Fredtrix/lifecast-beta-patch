#pragma once

// Precompiled header required by CommonLibSSE-NG.
// Force-included in every file of the target, ensuring CommonLib headers are
// parsed before any use of REL/Relocation.h - which defines the stl
// namespace.

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

using namespace std::literals;
