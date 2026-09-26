// Body.cpp
// Atelier

#include "Edit/Body.h"

#include "Core/ObodyApi.h"
#include "Core/ObodyLink.h"
#include "Core/SkeeLink.h"
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
			// Which list: OBody's own IsFemale, not the actor base. OBody is
			// the one that knows bodies - the Atelier delegates body work to
			// whoever owns it - and its header sets the bit whether or not a
			// preset is assigned. The actor base sex is logged beside it: a
			// disagreement between the two is a finding.
			const auto current = Current(obody, player);
			const bool female = current.obodyFemale;
			auto       names = Names(obody, female);
			spdlog::info("[body] {}: sending {} {} presets, current \"{}\" (OBody IsFemale {}, actor base female {})",
				a_why, names.size(), female ? "female" : "male", current.name, female, IsFemale(player));
			Panel::Bridge::Send("body", { { "ready", true }, { "female", female }, { "presets", std::move(names) },
											{ "current", current.name } });
		}

		void OnBodyGet(const nlohmann::json&)
		{
			Push("the page asked");
		}

		// What the last effective body-set of this menu session left on the
		// character. See Body.h, "WHAT THE PANEL WROTE".
		nlohmann::json g_written = nlohmann::json::array();

		void Forget(const char* a_why)
		{
			if (!g_written.empty()) {
				spdlog::info("[body] {}: the {} body morphs kept as the panel's are forgotten", a_why, g_written.size());
			}
			g_written = nlohmann::json::array();
		}

		using MorphKey = std::pair<std::string, std::string>;  // name, key

		std::map<MorphKey, float> ByNameAndKey(const std::vector<Core::SkeeLink::MorphValue>& a_values)
		{
			std::map<MorphKey, float> out;
			for (const auto& v : a_values) {
				out[{ v.name, v.key }] = v.value;
			}
			return out;
		}

		void Keep(RE::PlayerCharacter* a_player, const std::vector<Core::SkeeLink::MorphValue>& a_before,
			bool a_readBefore)
		{
			std::vector<Core::SkeeLink::MorphValue> afterValues;
			if (!a_readBefore || !Core::SkeeLink::ReadBodyMorphs(a_player, afterValues)) {
				Forget("SKEE could not be read around the body-set");
				return;
			}
			const auto before = ByNameAndKey(a_before);
			const auto after = ByNameAndKey(afterValues);

			std::set<std::string> touched;
			for (const auto& [id, value] : after) {
				const auto it = before.find(id);
				if (it == before.end() || it->second != value) {
					touched.insert(id.second);
				}
			}
			for (const auto& [id, value] : before) {
				if (!after.contains(id)) {
					touched.insert(id.second);
				}
			}
			if (touched.empty()) {
				spdlog::info("[body] the body-set changed no body morph ({} before, {} after) - {} kept as before",
					before.size(), after.size(), g_written.size());
				return;
			}

			std::map<std::string, nlohmann::json> byName;
			std::size_t kept = 0;
			for (const auto& [id, value] : after) {
				if (!touched.contains(id.second)) {
					continue;
				}
				auto& entry = byName[id.first];
				if (entry.is_null()) {
					entry = { { "name", id.first }, { "keys", nlohmann::json::array() } };
				}
				entry["keys"].push_back({ { "key", id.second }, { "value", value } });
				++kept;
			}
			g_written = nlohmann::json::array();
			for (auto& [name, entry] : byName) {
				g_written.push_back(std::move(entry));
			}
			std::string keys;
			for (const auto& k : touched) {
				keys += (keys.empty() ? "" : ", ") + k;
			}
			spdlog::info("[body] the body-set touched the keys [{}]: {} body morph values kept as the panel's "
						 "({} before, {} after, all keys)",
				keys, kept, before.size(), after.size());
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
			std::vector<Core::SkeeLink::MorphValue> morphsBefore;
			const bool readBefore = Core::SkeeLink::ReadBodyMorphs(player, morphsBefore);
			API::AssignPresetPayload payload{};
			payload.flags = API::AssignPresetPayload::Flags::ForceImmediateApplicationOfMorphs;
			payload.presetName = name;
			const auto start = Clock::now();
			const bool found = obody->AssignPresetToActor(player, payload);
			const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
			const auto after = Current(obody, player);
			spdlog::info("[body] body-set \"{}\": OBody {} in {} us; assigned \"{}\" -> \"{}\"", name,
				found ? "applied it" : "does not know that preset", us, before.name, after.name);
			if (found) {
				Keep(player, morphsBefore, readBefore);
			}
			Push(found ? "a body was applied" : "the preset was not found");
		}

		void OnShown() { Push("the panel came on screen"); }

		// A preset load replaced the whole character. The page gets OBody's
		// answer through OnShown right after this; the line here is what OBody
		// says the player carries at that moment, for the log.
		void OnReplaced()
		{
			Forget("a load replaced the character");
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

		void OnMenuClosed() { Forget("the RaceSex Menu closed"); }

		bool g_installed = false;
	}

	const nlohmann::json& Written() { return g_written; }

	void Install()
	{
		if (g_installed) {
			return;
		}
		Session::OnPanelShown(&OnShown);
		Session::OnReplaced(&OnReplaced);
		Session::OnMenuClosed(&OnMenuClosed);
		Core::ObodyLink::OnReadinessChanged(&OnReadiness);
		Panel::Bridge::On("body-get", &OnBodyGet);
		Panel::Bridge::On("body-set", &OnBodySet);
		g_installed = true;
		spdlog::info("[body] installed");
	}
}
