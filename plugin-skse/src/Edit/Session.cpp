// Session.cpp
// Atelier

#include "Edit/Session.h"

#include "Panel/Panel.h"

namespace Atelier::Edit::Session
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		std::vector<Callback> g_onTick;
		std::vector<Callback> g_onShown;
		std::vector<Callback> g_onOpened;
		std::vector<Callback> g_onClosed;
		std::vector<Callback> g_onRedrawn;
		std::vector<Callback> g_onReplaced;

		bool              g_redrawPending = false;
		bool              g_redrawNow = false;
		Clock::time_point g_lastRedraw{};

		bool g_wasVisible = false;

		// Redraw cost, per RaceSex Menu session.
		struct Cost
		{
			std::uint32_t count = 0;
			long long     totalUs = 0;
			long long     minUs = 0;
			long long     maxUs = 0;
		} g_cost;

		void RunAll(const std::vector<Callback>& a_list)
		{
			for (auto* fn : a_list) {
				fn();
			}
		}

		void Redraw(const char* a_why)
		{
			g_redrawPending = false;
			g_redrawNow = false;

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				spdlog::warn("[redraw] ({}): no player", a_why);
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

			spdlog::info("[redraw] #{} ({}): Actor::DoReset3D(true) took {} us", g_cost.count, a_why, us);
			RunAll(g_onRedrawn);
		}

		void LogCost()
		{
			if (g_cost.count == 0) {
				spdlog::info("[redraw] RaceSex Menu closed: no redraw");
				return;
			}
			spdlog::info("[redraw] RaceSex Menu closed: {} redraws - min {} us, max {} us, mean {} us",
				g_cost.count, g_cost.minUs, g_cost.maxUs, g_cost.totalUs / g_cost.count);
		}

		// Runs on every input tick - about once a frame, whether the panel
		// has focus or not.
		void Tick()
		{
			const bool visible = Panel::IsVisible();
			if (visible && !g_wasVisible && MenuOpen()) {
				RunAll(g_onShown);
			}
			g_wasVisible = visible;

			RunAll(g_onTick);

			if (!g_redrawPending) {
				return;
			}
			// The gap holds for a finished edit too: a run of clicks (a
			// list stepped with its arrows) would otherwise rebuild the actor
			// on every click. The last value always gets its redraw, at most
			// kMinRedrawGap late.
			if (Clock::now() - g_lastRedraw >= kMinRedrawGap) {
				Redraw(g_redrawNow ? "edit ended" : "dragging");
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
					g_redrawPending = false;
					g_redrawNow = false;
					RunAll(g_onOpened);
				} else {
					// A value written in the last moments of a drag must not
					// stay invisible until something else resets the actor.
					if (g_redrawPending) {
						Redraw("menu closing");
					}
					LogCost();
					RunAll(g_onClosed);
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
			spdlog::error("[session] no UI singleton - a value written just before the menu closes may not show");
		}

		if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
			input->AddEventSink(InputSink::GetSingleton());
		} else {
			// Values are still written; they just never get redrawn.
			spdlog::error("[session] no BSInputDeviceManager - edits will write but never redraw");
		}

		g_installed = true;
		spdlog::info("[session] installed - redraw at most every {} ms", kMinRedrawGap.count());
	}

	bool MenuOpen()
	{
		auto* ui = RE::UI::GetSingleton();
		return ui && ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME);
	}

	void RequestRedraw(bool a_now)
	{
		g_redrawPending = true;
		if (a_now) {
			g_redrawNow = true;
		}
	}

	void OnTick(Callback a_fn) { g_onTick.push_back(a_fn); }
	void OnPanelShown(Callback a_fn) { g_onShown.push_back(a_fn); }
	void OnMenuOpened(Callback a_fn) { g_onOpened.push_back(a_fn); }
	void OnMenuClosed(Callback a_fn) { g_onClosed.push_back(a_fn); }
	void OnRedrawn(Callback a_fn) { g_onRedrawn.push_back(a_fn); }
	void OnReplaced(Callback a_fn) { g_onReplaced.push_back(a_fn); }

	void Replaced(const char* a_why)
	{
		// A redraw requested for an edit made before the replacement would
		// rebuild the actor for nothing.
		g_redrawPending = false;
		g_redrawNow = false;
		RunAll(g_onReplaced);
		const bool shown = Panel::IsVisible();
		if (shown) {
			RunAll(g_onShown);
		}
		spdlog::info("[session] character replaced ({}): {} sections reset, panel {}", a_why,
			g_onReplaced.size(), shown ? "refreshed" : "not on screen");
	}
}
