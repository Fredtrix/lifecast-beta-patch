// Body.cpp
// Atelier

#include "Edit/Body.h"

#include "Core/ObodyApi.h"
#include "Core/ObodyLink.h"
#include "Edit/Session.h"
#include "Panel/Bridge.h"
#include "Panel/Panel.h"

namespace Atelier::Edit::Body
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		namespace API = OBody::API;

		bool IsFemale(RE::PlayerCharacter* a_player)
		{
			auto* npc = a_player->GetActorBase();
			return npc && npc->GetSex() == RE::SEX::kFemale;
		}

		// OBody's string_views stay valid only until it withdraws; every name
		// is copied before it leaves this function.
		std::vector<std::string> Names(API::IPluginInterface* a_obody, bool a_female)
		{
			API::PresetCounts counts{};
			a_obody->GetPresetCounts(counts);
			const std::size_t count = a_female ? counts.female : counts.male;
			std::vector<std::string_view> views(count);
			const std::size_t got = count ? a_obody->GetPresetNames(
				a_female ? API::PresetCategoryFemale : API::PresetCategoryMale, views.data(), views.size()) : 0;
			std::vector<std::string> names;
			names.reserve(got);
			for (std::size_t i = 0; i < got && i < views.size(); ++i) {
				names.emplace_back(views[i]);
			}
			return names;
		}

		struct Assigned
		{
			std::string name;
			bool        obodyFemale = false;
		};

		Assigned Current(API::IPluginInterface* a_obody, RE::PlayerCharacter* a_player)
		{
			// The header asks for flags to be initialized before the call.
			API::PresetAssignmentInformation info{};
			info.flags = API::PresetAssignmentInformation::Flags::None;
			a_obody->GetPresetAssignedToActor(a_player, info);
			return { std::string(info.presetName),
				(info.flags & API::PresetAssignmentInformation::Flags::IsFemale) != 0 };
		}

		void Push(const char* a_why)
		{
			auto* obody = Core::ObodyLink::Ready();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!obody || !player) {
				spdlog::info("[body] {}: {} - sending not ready", a_why, obody ? "no player" : "OBody not ready");
				Panel::Bridge::Send("body", { { "ready", false } });
				return;
			}
			const bool female = IsFemale(player);
			auto       names = Names(obody, female);
			const auto current = Current(obody, player);
			spdlog::info("[body] {}: sending {} {} presets, current \"{}\" (actor base female {}, OBody IsFemale {})",
				a_why, names.size(), female ? "female" : "male", current.name, female, current.obodyFemale);
			Panel::Bridge::Send("body", { { "ready", true }, { "female", female }, { "presets", std::move(names) },
											{ "current", current.name } });
		}

		void OnBodyGet(const nlohmann::json&)
		{
			Push("the page asked");
		}

		void OnBodySet(const nlohmann::json& a_data)
		{
			if (!Session::MenuOpen()) {
				spdlog::warn("[body] body-set with the RaceSex Menu closed - ignored");
				return;
			}
			if (!a_data.is_object() || !a_data.contains("name") || !a_data["name"].is_string()) {
				spdlog::warn("[body] body-set without a string \"name\" - ignored");
				return;
			}
			const auto name = a_data["name"].get<std::string>();
			if (name.empty()) {
				// An empty name unassigns in OBody; the section does not offer
				// that, so an empty one is a page bug, not a request.
				spdlog::warn("[body] body-set with an empty name - ignored");
				return;
			}
			auto* obody = Core::ObodyLink::Ready();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!obody || !player) {
				spdlog::warn("[body] body-set \"{}\": {} - ignored, resending", name,
					obody ? "no player" : "OBody not ready");
				Push("body-set could not be applied");
				return;
			}

			const auto before = Current(obody, player);
			API::AssignPresetPayload payload{};
			payload.flags = API::AssignPresetPayload::Flags::ForceImmediateApplicationOfMorphs;
			payload.presetName = name;
			const auto start = Clock::now();
			const bool found = obody->AssignPresetToActor(player, payload);
			const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
			const auto after = Current(obody, player);
			spdlog::info("[body] body-set \"{}\": OBody {} in {} us; assigned \"{}\" -> \"{}\"", name,
				found ? "applied it" : "does not know that preset", us, before.name, after.name);
			Push(found ? "a body was applied" : "the preset was not found");
		}

		void OnShown() { Push("the panel came on screen"); }

		// A preset load replaced the whole character. The page gets OBody's
		// answer through OnShown right after this; the line here is what OBody
		// says the player carries at that moment, for the log.
		void OnReplaced()
		{
			auto* obody = Core::ObodyLink::Ready();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!obody || !player) {
				return;
			}
			spdlog::info("[body] character replaced: OBody says the player carries \"{}\"",
				Current(obody, player).name);
		}

		void OnReadiness()
		{
			if (Panel::IsVisible()) {
				Push("OBody readiness changed");
			}
		}

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Session::OnPanelShown(&OnShown);
		Session::OnReplaced(&OnReplaced);
		Core::ObodyLink::OnReadinessChanged(&OnReadiness);
		Panel::Bridge::On("body-get", &OnBodyGet);
		Panel::Bridge::On("body-set", &OnBodySet);
		g_installed = true;
		spdlog::info("[body] installed");
	}
}
