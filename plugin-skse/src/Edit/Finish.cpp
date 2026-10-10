// Finish.cpp
// Lifecast

#include "Edit/Finish.h"

#include "Edit/Session.h"
#include "Panel/Bridge.h"
#include "Preset/Load.h"
#include "Preset/Save.h"

namespace Lifecast::Edit::Finish
{
	namespace
	{
		using json = nlohmann::json;
		using Clock = std::chrono::steady_clock;

		// The vanilla start quest, and Alternate Start's. Local FormIDs, read
		// from the names of their compiled quest fragments
		// (QF_MQ101_0003372B, ARTH_QF_LALStartQuest_02000DAF).
		constexpr RE::FormID kMq101 = 0x03372B;
		constexpr auto       kMq101File = "Skyrim.esm";
		constexpr RE::FormID kLalStart = 0x000DAF;
		constexpr auto       kLalFile = "Alternate Start - Live Another Life.esp";
		// Alternate Start sets this stage right after its menu closes.
		constexpr std::uint16_t kLalStageAfterMenu = 10;

		struct Signs
		{
			bool          saving = false;    // byCharGenFlag has kDisableSaving
			std::uint8_t  charGenFlags = 0;  // the whole flag byte, for the log
			bool          mq101Found = false;
			std::uint16_t mq101Stage = 0;
			bool          mq101Running = false;
			bool          lalFound = false;
			std::uint16_t lalStage = 0;
			bool          lalRunning = false;

			bool Alternate() const { return lalFound && lalRunning && lalStage < kLalStageAfterMenu; }
			bool NewGame() const { return saving || Alternate(); }
		};

		Signs Read()
		{
			Signs s;
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				const auto& flags = player->GetGameStatsData().byCharGenFlag;
				s.charGenFlags = flags.underlying();
				s.saving = flags.any(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving);
			}
			if (auto* data = RE::TESDataHandler::GetSingleton()) {
				if (auto* q = data->LookupForm<RE::TESQuest>(kMq101, kMq101File)) {
					s.mq101Found = true;
					s.mq101Stage = q->GetCurrentStageID();
					s.mq101Running = q->IsRunning();
				}
				if (auto* q = data->LookupForm<RE::TESQuest>(kLalStart, kLalFile)) {
					s.lalFound = true;
					s.lalStage = q->GetCurrentStageID();
					s.lalRunning = q->IsRunning();
				}
			}
			return s;
		}

		void Log(const char* a_moment, const Signs& a_s)
		{
			spdlog::info("[finish] {}: new game {} - charGenFlags {:#04x} (saving disabled {}), MQ101 {} stage {} "
						 "running {}, Alternate Start {} stage {} running {} (its menu {})",
				a_moment, a_s.NewGame(), a_s.charGenFlags, a_s.saving, a_s.mq101Found ? "found" : "NOT found",
				a_s.mq101Stage, a_s.mq101Running, a_s.lalFound ? "found" : "not installed", a_s.lalStage,
				a_s.lalRunning, a_s.Alternate() ? "yes" : "no");
		}

		// The name on the player's actor base. Empty when it cannot be read -
		// see Readable(), which is what tells an unreadable name from an
		// empty one.
		std::string BaseName()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* npc = player ? player->GetActorBase() : nullptr;
			const char* name = npc ? npc->GetFullName() : nullptr;
			return name ? name : "";
		}

		// Whether the name on the actor base can be read at all. The finish
		// checks what it wrote by reading it back, so without this there is
		// no finish: the page is told, and points the player at RaceMenu's
		// own Done button instead of refusing as if the field were empty.
		//
		// This false branch is REACHABLE AND NEVER EXERCISED - not dead code.
		// Nothing in the vanilla game was found to leave the player without an
		// actor base or with a null name, but that was checked only by reading
		// this file and the CommonLibSSE-NG headers it uses; another mod that
		// clears the name was not looked for. Keep it through any cleanup.
		bool Readable()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* npc = player ? player->GetActorBase() : nullptr;
			return npc && npc->GetFullName() != nullptr;
		}

		std::string RefName()
		{
			auto*       player = RE::PlayerCharacter::GetSingleton();
			const char* name = player ? player->GetDisplayFullName() : nullptr;
			return name ? name : "";
		}

		bool OnlySpaces(const std::string& a_text)
		{
			return std::all_of(a_text.begin(), a_text.end(),
				[](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; });
		}

		// A finish was accepted in this menu session: nothing more is sent to
		// the menu until it opens again.
		bool              g_finishing = false;
		Clock::time_point g_finishedAt{};

		// Menu sessions so far. The page keeps a name the player typed across
		// F4 swaps within one session, and drops it when this changes.
		std::uint32_t g_session = 0;

		void Push(const char* a_why)
		{
			const auto signs = Read();
			const auto readable = Readable();
			const auto name = BaseName();
			if (readable) {
				spdlog::info("[finish] {}: sending the name \"{}\" ({} bytes), new game {}", a_why, name, name.size(),
					signs.NewGame());
			} else {
				spdlog::warn("[finish] {}: the name on the player's actor base CANNOT be read - the page is told the "
							 "finish is not available, new game {}",
					a_why, signs.NewGame());
			}
			Panel::Bridge::Send("name", { { "name", name }, { "readable", readable }, { "newGame", signs.NewGame() },
											{ "session", g_session } });
		}

		void Refuse(const std::string& a_why)
		{
			spdlog::info("[finish] refused: {}", a_why);
			Panel::Bridge::Send("finish-result", { { "ok", false }, { "why", a_why } });
		}

		void OnNameGet(const json&) { Push("the page asked"); }

		void OnFinish(const json& a_data)
		{
			if (g_finishing) {
				spdlog::info("[finish] a second finish in the same menu session - ignored");
				return;
			}
			auto* ui = RE::UI::GetSingleton();
			if (!Session::MenuOpen() || !ui || !ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME)) {
				Refuse("the RaceSex Menu is not open");
				return;
			}
			const auto name = a_data.is_object() && a_data.contains("name") && a_data["name"].is_string() ?
			                      a_data["name"].get<std::string>() :
			                      std::string();
			if (name.empty() || OnlySpaces(name)) {
				Refuse("the character needs a name");
				return;
			}
			if (!Readable()) {
				// Not the empty-name case: the player may have typed a name,
				// but what gets written could not be checked.
				Refuse("the character's name cannot be read here, so it cannot be checked - use RaceMenu's Done "
					   "button");
				return;
			}
			if (Preset::Save::Busy()) {
				Refuse("a preset is still being saved - wait for it");
				return;
			}
			// Finishing while a load is out would let that preset land after the menu
			// closed, unseen. So it stays refused, and the page is told the way out:
			// waiting always works; loading another preset only once the running one
			// has gone unanswered long enough to be replaced (Preset::Load).
			if (Preset::Load::Busy()) {
				Refuse(Preset::Load::Replaceable() ?
				           "RaceMenu has not finished applying the last preset - wait for it, or load another preset "
				           "to replace it" :
				           "RaceMenu is still applying the last preset - wait for it to finish");
				return;
			}
			auto* queue = RE::UIMessageQueue::GetSingleton();
			auto  menu = ui->GetMenu<RE::RaceSexMenu>();
			if (!queue || !menu) {
				Refuse(queue ? "the RaceSex Menu cannot be reached" : "the game's menu queue cannot be reached");
				return;
			}

			Log("finish asked", Read());
			const auto beforeBase = BaseName();
			const auto beforeRef = RefName();

			// The name, through the menu first: it is the game's own path.
			const auto start = Clock::now();
			menu->ChangeName(name.c_str());
			const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
			auto afterBase = BaseName();
			spdlog::info("[finish] RaceSexMenu::ChangeName(\"{}\", {} bytes) returned in {} us: base \"{}\" -> \"{}\", "
						 "ref \"{}\" -> \"{}\"",
				name, name.size(), us, beforeBase, afterBase, beforeRef, RefName());

			if (afterBase != name) {
				auto* player = RE::PlayerCharacter::GetSingleton();
				auto* npc = player ? player->GetActorBase() : nullptr;
				if (npc) {
					npc->SetFullName(name.c_str());
					afterBase = BaseName();
				}
				spdlog::warn("[finish] the menu's path did NOT set the name - written on the record instead: base "
							 "\"{}\", ref \"{}\"",
					afterBase, RefName());
			}
			if (afterBase != name) {
				// Neither path took. Closing now would end character creation
				// under a name the player did not give: nothing is closed.
				Refuse("the name could not be set - nothing was closed");
				return;
			}

			g_finishing = true;
			g_finishedAt = Clock::now();
			queue->AddMessage(RE::RaceSexMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			spdlog::info("[finish] kHide queued for the RaceSex Menu");
			Panel::Bridge::Send("finish-result", { { "ok", true }, { "why", "" } });
		}

		void OnMenuOpened()
		{
			g_finishing = false;
			++g_session;
			Log("the RaceSex Menu opened", Read());
		}

		void OnMenuClosed()
		{
			if (!g_finishing) {
				spdlog::info("[finish] the RaceSex Menu closed without the finish button");
				return;
			}
			const auto ms =
				std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_finishedAt).count();
			spdlog::info("[finish] the RaceSex Menu closed {} ms after the finish: base \"{}\", ref \"{}\"", ms,
				BaseName(), RefName());
			Log("after the finish", Read());
			g_finishing = false;
		}

		void OnShown() { Push("the panel came on screen"); }

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Session::OnPanelShown(&OnShown);
		Session::OnMenuOpened(&OnMenuOpened);
		Session::OnMenuClosed(&OnMenuClosed);
		Panel::Bridge::On("name-get", &OnNameGet);
		Panel::Bridge::On("finish", &OnFinish);
		g_installed = true;
		spdlog::info("[finish] installed");
	}
}
