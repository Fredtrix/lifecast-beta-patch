// Switcher.cpp
// Atelier

#include "Panel/Switcher.h"

#include "Panel/Bridge.h"
#include "Panel/Panel.h"
#include "Panel/RaceMenuFilm.h"
#include "Panel/RaceMenuTabs.h"
#include "Panel/Stage.h"

namespace Atelier::Panel::Switcher
{
	namespace
	{
		using Key = RE::BSKeyboardDevice::Key;
		using Clock = std::chrono::steady_clock;

		// F4 swaps the two editors - the key the players of the previous
		// RaceMenu front-end already know.
		constexpr std::uint32_t kSwapKey = Key::kF4;

		// How long the panel may stay on screen waiting for Meridian to grant
		// focus. Focus asked for before the page has loaded is applied at
		// load end, so this covers a slow first load; past it, something is
		// wrong and the player gets RaceMenu back rather than a panel that
		// does not answer.
		constexpr auto kFocusGrace = std::chrono::seconds(5);

		enum class Mode
		{
			kClosed,    // the RaceSex Menu is not open
			kAtelier,   // the panel is on screen and owns (or is getting) focus
			kRaceMenu,  // RaceMenu's film is on screen, the panel is hidden
		};

		Mode g_mode = Mode::kClosed;

		// Focus is applied asynchronously, so "not focused" right after the
		// request is normal. Losing focus only counts once it has been seen.
		bool              g_focusSeen = false;
		Clock::time_point g_shownAt{};

		std::uint32_t g_opens = 0;
		std::uint32_t g_swaps = 0;

		const char* ModeName(Mode a_mode)
		{
			switch (a_mode) {
			case Mode::kClosed:
				return "closed";
			case Mode::kAtelier:
				return "atelier";
			case Mode::kRaceMenu:
				return "racemenu";
			default:
				return "?";
			}
		}

		// RaceMenu's film is hidden and shown through the engine's movie API.
		// The film itself is never modified; it keeps running underneath and
		// keeps every value the player set there.
		void SetFilmVisible(bool a_visible)
		{
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto* movie = menu ? menu->uiMovie.get() : nullptr;
			if (!movie) {
				spdlog::warn("[switch] RaceMenu film not reachable - cannot {} it",
					a_visible ? "show" : "hide");
				return;
			}

			movie->SetVisible(a_visible);
			spdlog::info("[switch] RaceMenu film visible = {} (read back {})",
				a_visible, movie->GetVisible());
		}

		// RaceMenu is usually opened from the console (showracemenu), which
		// stays open underneath. Once the panel owns the keyboard, the key
		// that would close the console never reaches the game, and the
		// console sits over the Atelier until the player goes back to
		// RaceMenu. So it is closed before the panel asks for focus.
		void CloseConsole()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui || !ui->IsMenuOpen(RE::Console::MENU_NAME)) {
				return;
			}

			auto* queue = RE::UIMessageQueue::GetSingleton();
			if (!queue) {
				spdlog::warn("[switch] the console is open and there is no UI message queue to close it");
				return;
			}

			queue->AddMessage(RE::Console::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			spdlog::info("[switch] the console was open - asked it to close");
		}

		void SetMode(Mode a_mode)
		{
			spdlog::info("[switch] {} -> {}", ModeName(g_mode), ModeName(a_mode));
			g_mode = a_mode;
		}

		// Runs on every input tick. Puts RaceMenu back when the panel is on
		// screen but no longer owns focus, or never got it.
		void Watch()
		{
			if (g_mode != Mode::kAtelier) {
				return;
			}

			const bool focused = Panel::IsFocused();

			if (focused) {
				if (!g_focusSeen) {
					g_focusSeen = true;
					const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
						Clock::now() - g_shownAt);
					spdlog::info("[switch] focus granted, {} ms after the panel was shown", ms.count());
				}
				return;
			}

			if (g_focusSeen) {
				spdlog::info("[switch] the panel lost focus - back to RaceMenu");
				ShowRaceMenu();
				return;
			}

			if (Clock::now() - g_shownAt > kFocusGrace) {
				spdlog::warn("[switch] focus was never granted within {} s - back to RaceMenu",
					kFocusGrace.count());
				ShowRaceMenu();
			}
		}

		void OnMenuOpened()
		{
			++g_opens;
			spdlog::info("[switch] RaceSex Menu opened (#{})", g_opens);

			if (!Panel::Ensure()) {
				// No panel this time. RaceMenu stays exactly as it was, and
				// the player loses nothing but the Atelier.
				SetMode(Mode::kRaceMenu);
				return;
			}

			SetMode(Mode::kRaceMenu);
			ShowAtelier();
		}

		void OnMenuClosed()
		{
			spdlog::info("[switch] RaceSex Menu closed");

			// What the page shows for a moment - a refusal, a save preview, a
			// way to RaceMenu - belongs to the menu session it was shown in,
			// and the page is told that session ended. The hidden panel is not
			// that signal: F4 hides it too, and the player comes back to it.
			Bridge::Send("menu-closed");

			// The film goes away with the menu. The panel is hidden, not
			// released: re-creating it would make a new browser instance.
			Panel::DisarmFocusChord();
			Panel::SetVisible(false);
			SetMode(Mode::kClosed);
		}

		// "show-racemenu" { "for": what }, where "for" is optional: the
		// control the page hands over to RaceMenu, logged so a round shows
		// which way the player went.
		void OnPageShowRaceMenu(const nlohmann::json& a_data)
		{
			const bool hasFor = a_data.is_object() && a_data.contains("for") && a_data["for"].is_string();
			spdlog::info("[switch] the page asked for RaceMenu{}{}", hasFor ? " - for " : "",
				hasFor ? a_data["for"].get<std::string>() : std::string());
			ShowRaceMenu();
			if (hasFor) {
				RaceMenuTabs::Select(a_data["for"].get<std::string>());
			}
		}

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
				if (a_event && a_event->menuName == RE::RaceSexMenu::MENU_NAME) {
					if (a_event->opening) {
						OnMenuOpened();
					} else {
						OnMenuClosed();
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			MenuSink() = default;
		};

		class InputSink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static InputSink* GetSingleton()
			{
				static InputSink s_instance;
				return &s_instance;
			}

			RE::BSEventNotifyControl ProcessEvent(
				RE::InputEvent* const* a_event,
				RE::BSTEventSource<RE::InputEvent*>*) override
			{
				Watch();

				// This sink runs about 66 times a second, whether the panel
				// holds focus or not, so it is also where the character view
				// slides. A second tick of its own would buy nothing and
				// cost a frame's worth of dispatch; the stage reads one
				// atomic and returns when nothing is moving.
				Stage::Tick();

				// [e5] disposable sonda, Fase 6.4 E5: the ~66/s in the
				// comment above is a claim, not a measurement. Counts this
				// sink's own cadence for one second at a time, only while
				// the Atelier is the mode on screen, and logs the count.
				// Strip this block once E5 has a real number.
				if (g_mode == Mode::kAtelier) {
					static std::uint32_t   s_e5Count = 0;
					static Clock::time_point s_e5WindowAt{};
					if (s_e5WindowAt.time_since_epoch().count() == 0) {
						s_e5WindowAt = Clock::now();
					}
					++s_e5Count;
					const auto elapsed = Clock::now() - s_e5WindowAt;
					if (elapsed >= std::chrono::seconds(1)) {
						const double secs = std::chrono::duration<double>(elapsed).count();
						spdlog::info("[e5] input sink cadence with the Atelier on screen: {:.1f}/s ({} in {:.3f} s)",
							s_e5Count / secs, s_e5Count, secs);
						s_e5Count = 0;
						s_e5WindowAt = Clock::now();
					}
				}

				if (!a_event || g_mode != Mode::kRaceMenu) {
					// With the Atelier on screen, F4 never gets here: it is
					// the panel's focus chord and Meridian consumes it.
					return RE::BSEventNotifyControl::kContinue;
				}

				for (auto* event = *a_event; event; event = event->next) {
					if (event->GetDevice() != RE::INPUT_DEVICES::kKeyboard) {
						continue;
					}

					const auto* button = event->AsButtonEvent();
					if (button && button->IsDown() && button->GetIDCode() == kSwapKey) {
						spdlog::info("[switch] F4 from the game");
						ShowAtelier();
						break;
					}
				}

				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			InputSink() = default;
		};

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			spdlog::error("[switch] no UI singleton - the Atelier will never open");
			return;
		}
		ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());

		auto* input = RE::BSInputDeviceManager::GetSingleton();
		if (input) {
			input->AddEventSink(InputSink::GetSingleton());
		} else {
			// The panel still opens with the menu and the page can still send
			// the player back, but F4 from RaceMenu is dead and so is the
			// focus watch. Said here so it is not discovered by pressing F4.
			spdlog::error("[switch] no BSInputDeviceManager - F4 from RaceMenu will not work");
		}

		Bridge::On("show-racemenu", &OnPageShowRaceMenu);

		g_installed = true;
		spdlog::info("[switch] installed - F4 swaps the Atelier and RaceMenu");
	}

	void ShowAtelier()
	{
		if (g_mode != Mode::kRaceMenu || !Panel::Browser()) {
			return;
		}

		++g_swaps;
		spdlog::info("[switch] show the Atelier (swap #{})", g_swaps);

		// The order is the point. The way out exists before the panel can
		// take the keyboard; focus is the very last thing asked for.
		CloseConsole();
		Panel::ArmFocusChord(kSwapKey, 0);
		Panel::SetVisible(true);
		SetFilmVisible(false);

		g_focusSeen = false;
		g_shownAt = Clock::now();
		SetMode(Mode::kAtelier);

		Panel::RequestFocus();
	}

	void ShowRaceMenu()
	{
		if (g_mode != Mode::kAtelier) {
			return;
		}

		++g_swaps;
		spdlog::info("[switch] show RaceMenu (swap #{})", g_swaps);

		// Hiding the browser releases its focus. The chord comes off with it,
		// so F4 reaches the game again and brings the Atelier back.
		Panel::SetVisible(false);
		Panel::DisarmFocusChord();
		SetFilmVisible(true);
		RaceMenuFilm::Sync();

		SetMode(Mode::kRaceMenu);
	}
}
