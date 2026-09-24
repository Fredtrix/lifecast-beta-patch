// RaceMenuSync.cpp
// Atelier

#include "Core/RaceMenuSync.h"

namespace Atelier::Core::RaceMenuSync
{
	namespace
	{
		constexpr RE::FormID kHairColorId = 0x801;
		constexpr auto       kRaceMenuPlugin = "RaceMenu.esp"sv;

		void Send(const char* a_event, const std::string& a_str)
		{
			auto* source = SKSE::GetModCallbackEventSource();
			if (!source) {
				spdlog::warn("[sync] no mod callback source - {} \"{}\" not sent", a_event, a_str);
				return;
			}
			SKSE::ModCallbackEvent event{ a_event, a_str.c_str(), 0.0F, nullptr };
			source->SendEvent(&event);
			spdlog::info("[sync] sent {} \"{}\"", a_event, a_str);
		}
	}

	RE::BGSColorForm* HairColorForm()
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		return data ? data->LookupForm<RE::BGSColorForm>(kHairColorId, kRaceMenuPlugin) : nullptr;
	}

	void HairColorChanged(const RE::Color& a_color)
	{
		// RaceMenu reads the string "as int": a signed decimal of 0xFFRRGGBB,
		// the form GetColor() + 0xFF000000 gives in its own script.
		const std::uint32_t argb = 0xFF000000u | (static_cast<std::uint32_t>(a_color.red) << 16) |
		                           (static_cast<std::uint32_t>(a_color.green) << 8) | a_color.blue;
		Send("RSM_HairColorChange", std::to_string(static_cast<std::int32_t>(argb)));
	}

	void TintsChanged() { Send("RSM_SliderChange", "ChangeMaskColor"); }

	void HairPresetChanged() { Send("RSM_SliderChange", "ChangeHairColorPreset"); }

	void ShadersInvalidated() { Send("RSM_ShadersInvalidated", ""); }

	void PresetLoaded() { Send("RSM_RequestTintSave", ""); }
}
