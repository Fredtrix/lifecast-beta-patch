// Freeze.cpp
// Lifecast

#include "Edit/Freeze.h"

#include "Edit/Session.h"
#include "Panel/Bridge.h"

namespace Lifecast::Edit::Freeze
{
	namespace
	{
		using BoolFlags = RE::Actor::BOOL_FLAGS;
		using CharFlags = RE::CHARACTER_FLAGS;

		// How often the pinned blink timer is written again, while frozen.
		// Chosen, not measured to be the loosest that works.
		constexpr auto kPinGap = std::chrono::milliseconds(250);

		struct Saved
		{
			bool  graphUpdate = false;
			bool  aiEnabled = false;
			bool  notPushable = false;
			bool  recordHits = false;
			bool  hitFlags = false;
			bool  haveController = false;
			bool  haveFace = false;
			float blinkTimer = 0.0f;
		};

		bool                                  g_on = false;
		Saved                                 g_saved;
		std::chrono::steady_clock::time_point g_lastPin{};

		// Event name, label. Every entry was played on the player: the graph
		// answered true AND the character really moved. An event that answers
		// true and moves nothing is worse than one that is absent.
		struct PoseEntry
		{
			const char* event;
			const char* label;
		};
		constexpr PoseEntry kPoses[] = {
			{ "IdleWarmHands", "Warm hands" },
			{ "IdleCiceroDance1", "Dance 1" },
			{ "IdleCiceroDance2", "Dance 2" },
			{ "IdleCiceroDance3", "Dance 3" },
			{ "IdleLaugh", "Laugh" },
			{ "IdleCivilWarCheer", "Cheer" },
			{ "IdleApplaud2", "Applaud" },
			{ "IdleExamine", "Examine" },
			{ "IdleStudy", "Study" },
			{ "IdleLookFar", "Look far" },
			{ "IdleSurrender", "Surrender" },
			{ "IdlePray", "Pray" },
			{ "IdleBracedPain", "Braced" },
			{ "IdleSilentBow", "Silent bow" },
		};

		void PushFreeze(const char* a_why)
		{
			spdlog::info("[freeze] {}: on = {}", a_why, g_on);
			Panel::Bridge::Send("freeze", { { "on", g_on } });
		}

		void PinBlink(RE::Actor* a_actor)
		{
			if (auto* face = a_actor->GetFaceGenAnimationData()) {
				face->eyesBlinkingTimer = (std::numeric_limits<float>::max)();
			}
		}

		void Engage(RE::Actor* a_actor)
		{
			auto& flags = a_actor->GetActorRuntimeData().boolFlags;
			g_saved = {};

			// 1. save
			g_saved.graphUpdate = flags.all(BoolFlags::kShouldAnimGraphUpdate);
			g_saved.aiEnabled = a_actor->IsAIEnabled();
			auto* controller = a_actor->GetCharController();
			if (controller) {
				g_saved.haveController = true;
				g_saved.notPushable = controller->flags.all(CharFlags::kNotPushable);
				g_saved.recordHits = controller->flags.all(CharFlags::kRecordHits);
				g_saved.hitFlags = controller->flags.all(CharFlags::kHitFlags);
			}
			auto* face = a_actor->GetFaceGenAnimationData();
			if (face) {
				g_saved.haveFace = true;
				g_saved.blinkTimer = face->eyesBlinkingTimer;
			}

			// 2. the graph stops updating
			flags.reset(BoolFlags::kShouldAnimGraphUpdate);

			// 3. the controller
			if (controller) {
				controller->flags.set(CharFlags::kNotPushable);
				controller->flags.reset(CharFlags::kRecordHits, CharFlags::kHitFlags);
			}

			// 4. AI off, 5. stop moving
			a_actor->EnableAI(false);
			a_actor->StopMoving(1.0f);

			// 6. the face (7. is OnTick, which pins it again)
			PinBlink(a_actor);
			g_lastPin = std::chrono::steady_clock::now();

			spdlog::info("[freeze] engaged: saved graphUpdate {} ai {} controller {} (notPushable {} recordHits {} hitFlags {}) "
						 "face {} (blinkTimer {})",
				g_saved.graphUpdate, g_saved.aiEnabled, g_saved.haveController, g_saved.notPushable, g_saved.recordHits,
				g_saved.hitFlags, g_saved.haveFace, g_saved.blinkTimer);
		}

		void Disengage(RE::Actor* a_actor)
		{
			auto& flags = a_actor->GetActorRuntimeData().boolFlags;
			// Each flag goes back only if it was set before.
			if (g_saved.graphUpdate) {
				flags.set(BoolFlags::kShouldAnimGraphUpdate);
			}
			if (g_saved.haveController) {
				if (auto* controller = a_actor->GetCharController()) {
					// notPushable was turned ON by us: back off unless it was on.
					if (!g_saved.notPushable) {
						controller->flags.reset(CharFlags::kNotPushable);
					}
					if (g_saved.recordHits) {
						controller->flags.set(CharFlags::kRecordHits);
					}
					if (g_saved.hitFlags) {
						controller->flags.set(CharFlags::kHitFlags);
					}
				}
			}
			a_actor->EnableAI(g_saved.aiEnabled);
			if (g_saved.haveFace) {
				if (auto* face = a_actor->GetFaceGenAnimationData()) {
					face->eyesBlinkingTimer = g_saved.blinkTimer;
				}
			}
			spdlog::info("[freeze] released");
		}

		void SetFreeze(bool a_on, const char* a_why)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || a_on == g_on) {
				return;
			}
			if (a_on) {
				Engage(player);
			} else {
				Disengage(player);
			}
			g_on = a_on;
			PushFreeze(a_why);
		}

		void OnFreezeGet(const nlohmann::json&) { PushFreeze("the page asked"); }

		void OnFreezeSet(const nlohmann::json& a_data)
		{
			if (!Session::MenuOpen()) {
				spdlog::warn("[freeze] freeze-set with the RaceSex Menu closed - ignored");
				return;
			}
			if (!a_data.is_object() || !a_data.contains("on") || !a_data["on"].is_boolean()) {
				spdlog::warn("[freeze] freeze-set without a boolean \"on\" - ignored");
				return;
			}
			SetFreeze(a_data["on"].get<bool>(), "after freeze-set");
		}

		void OnPoseList(const nlohmann::json&)
		{
			nlohmann::json list = nlohmann::json::array();
			for (const auto& pose : kPoses) {
				list.push_back({ { "event", pose.event }, { "label", pose.label } });
			}
			Panel::Bridge::Send("poses", { { "poses", list } });
		}

		void OnPosePlay(const nlohmann::json& a_data)
		{
			if (!Session::MenuOpen()) {
				spdlog::warn("[freeze] pose-play with the RaceSex Menu closed - ignored");
				return;
			}
			if (!a_data.is_object() || !a_data.contains("event") || !a_data["event"].is_string()) {
				spdlog::warn("[freeze] pose-play without a string \"event\" - ignored");
				return;
			}
			const std::string asked = a_data["event"].get<std::string>();
			std::string       event = asked;
			if (event.empty()) {
				event = "IdleForceDefaultState";   // stop: the vanilla event
			} else {
				bool known = false;
				for (const auto& pose : kPoses) {
					known = known || event == pose.event;
				}
				if (!known) {
					spdlog::warn("[freeze] pose-play with an event not in the list: {} - ignored", event);
					return;
				}
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			// A graph that is not updating does not move: undo first.
			SetFreeze(false, "before a pose");
			const bool accepted = player->NotifyAnimationGraph(RE::BSFixedString(event.c_str()));
			spdlog::info("[freeze] pose {}: graph answered {}", event, accepted);
			Panel::Bridge::Send("pose",
				{ { "event", asked }, { "accepted", accepted },
					{ "why", accepted ? "" : "The character cannot play this one." } });
		}

		void OnTick()
		{
			if (!g_on) {
				return;
			}
			const auto now = std::chrono::steady_clock::now();
			if (now - g_lastPin < kPinGap) {
				return;
			}
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				PinBlink(player);
			}
			g_lastPin = now;
		}

		void OnOpened() { PushFreeze("menu opened"); }

		void OnClosed()
		{
			if (g_on) {
				SetFreeze(false, "the RaceSex Menu closed while frozen");
			}
		}

		bool g_installed = false;
	}

	void Release(const char* a_why)
	{
		if (g_on) {
			SetFreeze(false, a_why);
		}
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Session::OnTick(&OnTick);
		Session::OnMenuOpened(&OnOpened);
		Session::OnMenuClosed(&OnClosed);
		Panel::Bridge::On("freeze-get", &OnFreezeGet);
		Panel::Bridge::On("freeze-set", &OnFreezeSet);
		Panel::Bridge::On("pose-list", &OnPoseList);
		Panel::Bridge::On("pose-play", &OnPosePlay);
		g_installed = true;
		spdlog::info("[freeze] installed");
	}
}
