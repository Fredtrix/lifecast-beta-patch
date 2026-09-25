// SkeeLink.cpp
// Atelier

#include "SkeeLink.h"

#include <array>

namespace Atelier::Core::SkeeLink
{
	namespace
	{
		Skee::IInterfaceMap* g_interfaceMap = nullptr;

		// The names SKEE registers its interfaces under. Derived two ways,
		// not guessed:
		//
		//  - the class names in the ModderResource header, minus the leading
		//    I and the trailing Interface (IBodyMorphInterface -> BodyMorph,
		//    IActorUpdateManager -> ActorUpdateManager);
		//  - each of the ten appears as a literal string in the installed
		//    skee64.dll (RaceMenu Anniversary Edition, 2026-09-22).
		//
		// Two are confirmed against a real round rather than by derivation:
		// "BodyMorph" answered non-null and "Preset" answered null, measured
		// in game with the RaceSex Menu closed.
		//
		// A null answer here therefore means one of two things and the log
		// cannot separate them: the interface is not reachable from outside,
		// or this is not the key it is registered under. Anything concluded
		// from a null is stated with that scope attached.
		constexpr std::array kInterfaceNames{
			"BodyMorph",
			"NiTransform",
			"Attachment",
			"ItemData",
			"Command",
			"ActorUpdateManager",
			"Overlay",
			"Override",
			"Preset",
			"FormTag",
		};
	}

	Skee::IInterfaceMap* GetInterfaceMap()
	{
		if (g_interfaceMap) {
			return g_interfaceMap;
		}

		Skee::InterfaceExchangeMessage msg;
		msg.interfaceMap = nullptr;

		const bool dispatched = SKSE::GetMessagingInterface()->Dispatch(
			static_cast<std::uint32_t>(Skee::InterfaceExchangeMessage::kMessage_ExchangeInterface),
			&msg,
			sizeof(msg),
			"skee");

		spdlog::info(
			"[skee] Dispatch(kMessage_ExchangeInterface, \"skee\") returned {}, "
			"interfaceMap = {}",
			dispatched, static_cast<const void*>(msg.interfaceMap));

		g_interfaceMap = msg.interfaceMap;
		return g_interfaceMap;
	}

	void ReportInterfaces(const char* a_moment)
	{
		auto* map = GetInterfaceMap();
		if (!map) {
			spdlog::error(
				"[skee] [{}] no interface map - SKEE did not answer the "
				"exchange. Either RaceMenu/skee64 is not installed, or it "
				"loads after the Atelier in the plugin order.",
				a_moment);
			return;
		}

		for (const auto* name : kInterfaceNames) {
			auto* iface = map->QueryInterface(name);
			if (iface) {
				spdlog::info("[skee] [{}] QueryInterface(\"{}\") = {} version {}",
					a_moment, name, static_cast<const void*>(iface), iface->GetVersion());
			} else {
				spdlog::info("[skee] [{}] QueryInterface(\"{}\") = null",
					a_moment, name);
			}
		}
	}

	bool ClearSkinOverrides(RE::Actor* a_actor)
	{
		auto* map = GetInterfaceMap();
		auto* iface = map ? map->QueryInterface("Override") : nullptr;
		if (!a_actor || !iface) {
			spdlog::warn("[skee] skin overrides NOT cleared: actor {}, Override interface {}",
				static_cast<const void*>(a_actor), static_cast<const void*>(iface));
			return false;
		}
		// The vendored header describes version 2 of the interface; an older
		// one may lay its table out differently, so it is not called.
		const auto version = iface->GetVersion();
		if (version < Skee::IOverrideInterface::kPluginVersion2) {
			spdlog::warn("[skee] skin overrides NOT cleared: Override interface version {}, header describes {}",
				version, static_cast<std::uint32_t>(Skee::IOverrideInterface::kPluginVersion2));
			return false;
		}
		// The actor goes to SKEE as the address of the engine object, which
		// is what SKEE's own TESObjectREFR* is. No C++ base-class conversion
		// is involved: under the multi-runtime build CommonLib's compile-time
		// layout matches no runtime, so reaching a base through the
		// hierarchy is exactly what the shared conventions forbid.
		auto* refr = reinterpret_cast<Skee::TESObjectREFR*>(a_actor);
		static_cast<Skee::IOverrideInterface*>(iface)->RemoveAllSkinOverridesByReference(refr);
		return true;
	}
}
