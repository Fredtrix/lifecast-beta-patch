// RaceSex.cpp
// Lifecast

#include "Edit/RaceSex.h"

#include "Edit/Appearance.h"
#include "Edit/Session.h"
#include "Panel/Bridge.h"

namespace Lifecast::Edit::RaceSex
{
	namespace
	{
		using HPType = RE::BGSHeadPart::HeadPartType;

		constexpr HPType kPartTypes[] = {
			HPType::kMisc,
			HPType::kFace,
			HPType::kEyes,
			HPType::kHair,
			HPType::kFacialHair,
			HPType::kScar,
			HPType::kEyebrows,
		};

		std::vector<RE::TESRace*> g_races;
		std::uint32_t             g_changes = 0;

		RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

		RE::TESNPC* Npc()
		{
			auto* player = Player();
			return player ? player->GetActorBase() : nullptr;
		}

		std::string RaceName(const RE::TESRace* a_race)
		{
			const char* name = a_race ? a_race->GetFullName() : nullptr;
			return name && *name ? name : std::string{};
		}

		// The list is built once per menu opening: which races exist does not
		// change while the menu is open, only which one the player has.
		void EnsureRaces()
		{
			if (!g_races.empty()) {
				return;
			}
			auto* data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				return;
			}
			std::size_t seen = 0;
			for (auto* race : data->GetFormArray<RE::TESRace>()) {
				if (!race) {
					continue;
				}
				++seen;
				const auto& flags = race->data.flags;
				if (!flags.all(RE::RACE_DATA::Flag::kPlayable) || flags.any(RE::RACE_DATA::Flag::kChild)) {
					continue;
				}
				if (race->HasKeywordString("Vampire")) {
					continue;
				}
				g_races.push_back(race);
			}
			spdlog::info("[race-sex] race list built: {} playable of {} races", g_races.size(), seen);
			for (std::size_t i = 0; i < g_races.size(); ++i) {
				spdlog::info("[race-sex]   [{}] {:08X} {}", i, g_races[i]->GetFormID(), RaceName(g_races[i]));
			}
		}

		int IndexOf(const RE::TESRace* a_race)
		{
			for (std::size_t i = 0; i < g_races.size(); ++i) {
				if (g_races[i] == a_race) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		void Push(const char* a_why)
		{
			auto* player = Player();
			auto* npc = Npc();
			if (!player || !npc) {
				spdlog::info("[race-sex] {}: no player yet - not ready", a_why);
				Panel::Bridge::Send("race-sex", { { "ready", false } });
				return;
			}
			EnsureRaces();
			auto races = nlohmann::json::array();
			for (const auto* race : g_races) {
				races.push_back({ { "name", RaceName(race) }, { "id", fmt::format("{:08X}", race->GetFormID()) } });
			}
			const auto* race = player->GetRace();
			const bool  female = npc->GetSex() == RE::SEX::kFemale;
			spdlog::info("[race-sex] {}: sending race {:08X} (index {}), {}", a_why, race ? race->GetFormID() : 0,
				IndexOf(race), female ? "female" : "male");
			Panel::Bridge::Send("race-sex", { { "ready", true }, { "races", std::move(races) },
												{ "race", IndexOf(race) }, { "female", female } });
		}

		void Answer(const char* a_what, bool a_ok, std::string a_why)
		{
			if (!a_ok) {
				spdlog::warn("[race-sex] {} refused: {}", a_what, a_why);
			}
			Panel::Bridge::Send("race-sex-applied", { { "what", a_what }, { "ok", a_ok }, { "why", std::move(a_why) } });
		}

		// The fields a change is measured by, read out of the game every time:
		// the actor's race and the base's, the sex flag, the head parts, and
		// the 3D root - its address and name tell a rebuilt actor from one
		// that was left alone.
		std::string State()
		{
			auto* player = Player();
			auto* npc = Npc();
			if (!player || !npc) {
				return "(no player)";
			}
			const auto* race = player->GetRace();
			const auto* baseRace = npc->GetRace();
			const auto* root = player->Get3D();
			std::string parts;
			for (const auto type : kPartTypes) {
				const auto* part = npc->GetCurrentHeadPartByType(type);
				parts += fmt::format("{}{}={:08X}", parts.empty() ? "" : " ", static_cast<int>(type),
					part ? part->GetFormID() : 0);
			}
			return fmt::format("race {:08X} base {:08X} sex {} | 3d {} {} | parts {}", race ? race->GetFormID() : 0,
				baseRace ? baseRace->GetFormID() : 0, static_cast<int>(npc->GetSex()),
				static_cast<const void*>(root), root ? root->name.c_str() : "-", parts);
		}

		void Finish(const char* a_what)
		{
			++g_changes;
			// Replaced first, so every section drops what it kept from the old
			// character before it re-reads; then the whole-actor redraw.
			Session::Replaced(a_what);
			Session::RequestRedraw(true);
			spdlog::info("[race-sex]   after:  {}", State());
		}

		void OnRaceSet(const nlohmann::json& a_data)
		{
			if (!Session::MenuOpen()) {
				spdlog::warn("[race-sex] race-set with the RaceSex Menu closed - ignored");
				return;
			}
			if (!a_data.is_object() || !a_data.contains("i") || !a_data["i"].is_number_integer()) {
				spdlog::warn("[race-sex] race-set without an integer \"i\" - ignored");
				return;
			}
			const int index = a_data["i"].get<int>();
			auto*     player = Player();
			EnsureRaces();
			if (!player || !Npc()) {
				Answer("race", false, "no player");
				return;
			}
			if (index < 0 || index >= static_cast<int>(g_races.size())) {
				Answer("race", false, fmt::format("outside [0, {})", g_races.size()));
				return;
			}
			auto* race = g_races[index];
			if (player->GetRace() == race) {
				Answer("race", false, "already that race");
				return;
			}
			spdlog::info("[race-sex] race-set [{}] {:08X} {}", index, race->GetFormID(), RaceName(race));
			spdlog::info("[race-sex]   before: {}", State());
			player->SwitchRace(race, true);
			spdlog::info("[race-sex]   switched: {}", State());
			Appearance::FitToRace("the race was changed");
			Finish("the race was changed");
			Answer("race", true, RaceName(race));
			Push("after a race change");
		}

		void OnSexSet(const nlohmann::json& a_data)
		{
			if (!Session::MenuOpen()) {
				spdlog::warn("[race-sex] sex-set with the RaceSex Menu closed - ignored");
				return;
			}
			if (!a_data.is_object() || !a_data.contains("female") || !a_data["female"].is_boolean()) {
				spdlog::warn("[race-sex] sex-set without a boolean \"female\" - ignored");
				return;
			}
			const bool female = a_data["female"].get<bool>();
			auto*      npc = Npc();
			if (!npc) {
				Answer("sex", false, "no player");
				return;
			}
			if ((npc->GetSex() == RE::SEX::kFemale) == female) {
				Answer("sex", false, "already that sex");
				return;
			}
			spdlog::info("[race-sex] sex-set {}", female ? "female" : "male");
			spdlog::info("[race-sex]   before: {}", State());
			npc->SetActorBaseFlag(RE::ACTOR_BASE_DATA::Flag::kFemale, female, true);
			spdlog::info("[race-sex]   flag:   {}", State());
			Appearance::FitToRace("the sex was changed");
			Finish("the sex was changed");
			Answer("sex", true, female ? "female" : "male");
			Push("after a sex change");
		}

		// The redraw runs on a later tick than the change: this is the line
		// that says whether the 3D root came back different.
		void OnRedrawn()
		{
			if (g_changes > 0) {
				spdlog::info("[race-sex] redrawn: {}", State());
			}
		}

		void OnGet(const nlohmann::json&) { Push("the page asked"); }

		void OnShown() { Push("the panel came on screen"); }

		void OnOpened()
		{
			g_changes = 0;
			g_races.clear();
		}

		void OnClosed() { spdlog::info("[race-sex] RaceSex Menu closed: {} changes", g_changes); }

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Session::OnPanelShown(&OnShown);
		Session::OnMenuOpened(&OnOpened);
		Session::OnMenuClosed(&OnClosed);
		Session::OnRedrawn(&OnRedrawn);
		Panel::Bridge::On("race-sex-get", &OnGet);
		Panel::Bridge::On("race-set", &OnRaceSet);
		Panel::Bridge::On("sex-set", &OnSexSet);
		g_installed = true;
		spdlog::info("[race-sex] installed");
	}
}
