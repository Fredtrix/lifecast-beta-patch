// FaceSliders.cpp
// Atelier

#include "Edit/FaceSliders.h"

#include "Panel/Bridge.h"
#include "Panel/Panel.h"

namespace Atelier::Edit::FaceSliders
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		using FaceData = RE::TESNPC::FaceData;

		static_assert(kUsableMorphs < FaceData::Morphs::kTotal,
			"the usable morphs stop before the VampireMorph entry");

		// While a slider is being dragged, the face is redrawn at most this
		// often. One redraw measured 23 ms in round A; four a second keeps
		// the face following the hand without spending every frame on it.
		constexpr auto kMinRedrawGap = std::chrono::milliseconds(250);

		bool              g_redrawPending = false;
		bool              g_redrawNow = false;
		Clock::time_point g_lastRedraw{};

		bool g_wasVisible = false;

		// Whether the last "face" sent to the page carried real values. While
		// it did not, every tick checks whether they have arrived.
		bool g_sentReady = false;

		// Redraw cost, per RaceSex Menu session.
		struct Cost
		{
			std::uint32_t count = 0;
			long long     totalUs = 0;
			long long     minUs = 0;
			long long     maxUs = 0;
		} g_cost;

		std::uint32_t g_sets = 0;

		bool MenuOpen()
		{
			auto* ui = RE::UI::GetSingleton();
			return ui && ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME);
		}

		FaceData* PlayerFace()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* npc = player ? player->GetActorBase() : nullptr;
			return npc ? npc->faceData : nullptr;
		}

		bool Plausible(float a_value)
		{
			return std::isfinite(a_value) && std::abs(a_value) <= kPlausibleLimit;
		}

		bool Ready(const FaceData* a_face)
		{
			if (!a_face) {
				return false;
			}
			for (int i = 0; i < kUsableMorphs; ++i) {
				if (!Plausible(a_face->morphs[i])) {
					return false;
				}
			}
			return true;
		}

		void PushFace(const char* a_why)
		{
			auto* face = PlayerFace();
			if (!face) {
				spdlog::warn("[face] {}: the player has no face data - nothing to send", a_why);
				g_sentReady = false;
				Panel::Bridge::Send("face",
					{ { "usable", 0 }, { "ready", false }, { "morphs", nlohmann::json::array() } });
				return;
			}

			int  placeholders = 0;
			auto morphs = nlohmann::json::array();
			for (int i = 0; i < kUsableMorphs; ++i) {
				const float v = face->morphs[i];
				if (Plausible(v)) {
					morphs.push_back(v);
				} else {
					morphs.push_back(nullptr);
					++placeholders;
				}
			}

			g_sentReady = placeholders == 0;
			if (g_sentReady) {
				spdlog::info("[face] {}: sending the {} morphs", a_why, kUsableMorphs);
			} else {
				spdlog::info("[face] {}: sending the {} morphs - {} are placeholders, not ready yet",
					a_why, kUsableMorphs, placeholders);
			}
			Panel::Bridge::Send("face",
				{ { "usable", kUsableMorphs }, { "ready", g_sentReady }, { "morphs", std::move(morphs) } });
		}

		void Redraw(const char* a_why)
		{
			g_redrawPending = false;
			g_redrawNow = false;

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				spdlog::warn("[face] redraw ({}): no player", a_why);
				return;
			}

			const auto start = Clock::now();
			player->DoReset3D(true);
			const auto end = Clock::now();
			g_lastRedraw = end;

			const auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
			if (g_cost.count == 0 || us < g_cost.minUs) {
				g_cost.minUs = us;
			}
			if (g_cost.count == 0 || us > g_cost.maxUs) {
				g_cost.maxUs = us;
			}
			++g_cost.count;
			g_cost.totalUs += us;

			spdlog::info("[face] redraw #{} ({}): Actor::DoReset3D(true) took {} us",
				g_cost.count, a_why, us);
		}

		void LogCost(const char* a_when)
		{
			if (g_cost.count == 0) {
				spdlog::info("[face] {}: {} values written, no redraw", a_when, g_sets);
				return;
			}
			spdlog::info(
				"[face] {}: {} values written, {} redraws - min {} us, max {} us, mean {} us",
				a_when, g_sets, g_cost.count, g_cost.minUs, g_cost.maxUs,
				g_cost.totalUs / g_cost.count);
		}

		void OnFaceGet(const nlohmann::json&)
		{
			PushFace("the page asked");
		}

		void OnFaceSet(const nlohmann::json& a_data)
		{
			if (!MenuOpen()) {
				spdlog::warn("[face] face-set with the RaceSex Menu closed - ignored");
				return;
			}

			if (!a_data.is_object() || !a_data.contains("i") || !a_data["i"].is_number_integer() ||
				!a_data.contains("v") || !a_data["v"].is_number()) {
				spdlog::warn("[face] face-set without an integer \"i\" and a number \"v\" - ignored");
				return;
			}

			const auto index = a_data["i"].get<int>();
			if (index < 0 || index >= kUsableMorphs) {
				spdlog::warn("[face] face-set for morph {} - outside [0, {}), ignored", index, kUsableMorphs);
				return;
			}

			const auto raw = a_data["v"].get<float>();
			if (!std::isfinite(raw)) {
				spdlog::warn("[face] face-set for morph {} with a non-finite value - ignored", index);
				return;
			}
			const float value = std::clamp(raw, kMinValue, kMaxValue);

			auto* face = PlayerFace();
			if (!face) {
				spdlog::warn("[face] face-set for morph {}: the player has no face data", index);
				return;
			}

			if (!Ready(face)) {
				spdlog::warn("[face] face-set for morph {}: the face data is not ready yet - ignored", index);
				return;
			}

			const bool final = a_data.contains("final") && a_data["final"].is_boolean() && a_data["final"].get<bool>();

			face->morphs[index] = value;
			++g_sets;
			g_redrawPending = true;
			if (final) {
				g_redrawNow = true;
				spdlog::info("[face] morph[{}] = {:.3f} (drag ended)", index, value);
			}
		}

		// Runs on every input tick - about once a frame, whether the panel
		// has focus or not.
		void Tick()
		{
			const bool visible = Panel::IsVisible();
			if (visible && !g_wasVisible && MenuOpen()) {
				PushFace("the panel came on screen");
			}
			g_wasVisible = visible;

			// The values were placeholders when last sent; send again as soon
			// as the engine has filled them in.
			if (visible && !g_sentReady && MenuOpen() && Ready(PlayerFace())) {
				PushFace("the face data became ready");
			}

			if (!g_redrawPending) {
				return;
			}
			if (g_redrawNow) {
				Redraw("drag ended");
			} else if (Clock::now() - g_lastRedraw >= kMinRedrawGap) {
				Redraw("dragging");
			}
		}

		class InputSink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static InputSink* GetSingleton()
			{
				static InputSink s_instance;
				return &s_instance;
			}

			RE::BSEventNotifyControl ProcessEvent(
				RE::InputEvent* const*,
				RE::BSTEventSource<RE::InputEvent*>*) override
			{
				Tick();
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			InputSink() = default;
		};

		class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuSink* GetSingleton()
			{
				static MenuSink s_instance;
				return &s_instance;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent* a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event || a_event->menuName != RE::RaceSexMenu::MENU_NAME) {
					return RE::BSEventNotifyControl::kContinue;
				}

				if (a_event->opening) {
					g_cost = {};
					g_sets = 0;
					g_redrawPending = false;
					g_redrawNow = false;
					g_sentReady = false;
				} else {
					// A value written in the last moments of a drag must not
					// stay invisible until something else resets the actor.
					if (g_redrawPending) {
						Redraw("menu closing");
					}
					LogCost("RaceSex Menu closed");
				}
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			MenuSink() = default;
		};

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}

		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
		} else {
			spdlog::error("[face] no UI singleton - a value written just before the menu closes may not show");
		}

		if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
			input->AddEventSink(InputSink::GetSingleton());
		} else {
			// Values are still written; they just never get redrawn.
			spdlog::error("[face] no BSInputDeviceManager - face sliders will write but never redraw");
		}

		Panel::Bridge::On("face-get", &OnFaceGet);
		Panel::Bridge::On("face-set", &OnFaceSet);

		g_installed = true;
		spdlog::info("[face] installed - {} face morph sliders, redraw at most every {} ms while dragging",
			kUsableMorphs, kMinRedrawGap.count());
	}
}
