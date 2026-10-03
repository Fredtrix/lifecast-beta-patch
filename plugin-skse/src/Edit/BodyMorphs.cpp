// BodyMorphs.cpp
// Lifecast

#include "Edit/BodyMorphs.h"

#include "Core/RaceMenuSync.h"
#include "Edit/Session.h"
#include "Panel/Bridge.h"

namespace Lifecast::Edit::BodyMorphs
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr auto kPanels = "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance";

		// The tab the Lifecast already builds from its own node table. See the
		// header: reporting it here would stack a second body-scales section
		// on the one Edit/BodyScales draws.
		constexpr auto kBodyScales = "rsm_body_scale";

		// RaceMenu's film waits this long between two sends of the same slider
		// while it is being dragged (its own slider starts a 100 ms interval
		// and sends the latest value on each tick). The page sends faster than
		// that, so the gap is kept here instead: a mod's script answers every
		// one of these by rebuilding the body, and there is no reason to ask
		// for that more often than moving the slider in RaceMenu does. The
		// value that ends a gesture is never held back.
		constexpr auto kGap = std::chrono::milliseconds(100);

		struct Slider
		{
			std::string callback;
			std::string label;
			double      min = 0;
			double      max = 0;
			double      step = 0;
			double      value = 0;
		};

		struct Category
		{
			std::string          key;
			std::string          label;
			std::vector<Slider>  sliders;
		};

		std::mutex            g_lock;
		std::vector<Category> g_categories;
		bool                  g_read = false;

		// Per callback: when it was last sent, and how many times, so a drag
		// logs its first step and its last instead of one line per step.
		std::map<std::string, std::pair<Clock::time_point, int>> g_sent;
		int                                                      g_sets = 0;

		// The film's tab set as it was last seen, and when it was last looked
		// at. See Signature(): the tabs arrive after the menu opens, so they
		// are watched rather than read once.
		std::string       g_signature;
		Clock::time_point g_looked{};

		// How often the film is looked at while the menu is open. The measured
		// gap between the menu opening and the mod tabs appearing was under
		// five seconds; a second is well inside that and costs twenty
		// GetMember calls.
		constexpr auto kLookGap = std::chrono::seconds(1);

		std::string Str(const RE::GFxValue& a_obj, const char* a_name)
		{
			RE::GFxValue v;
			return a_obj.GetMember(a_name, &v) && v.IsString() ? std::string(v.GetString()) : std::string();
		}

		bool Num(const RE::GFxValue& a_obj, const char* a_name, double& a_out)
		{
			RE::GFxValue v;
			if (a_obj.GetMember(a_name, &v) && v.IsNumber()) {
				a_out = v.GetNumber();
				return true;
			}
			return false;
		}

		// The film shows a list's entries at "_entryList" and not at the
		// public name its ActionScript reads them by - the same lookup
		// Panel/RaceMenuTabs makes.
		bool Entries(RE::GFxMovieView* a_movie, const char* a_list, RE::GFxValue& a_out)
		{
			const auto   where = std::string(kPanels) + "." + a_list;
			RE::GFxValue list;
			if (!a_movie->GetVariable(&list, where.c_str()) || !list.IsObject()) {
				spdlog::warn("[morphs] no {} on the film", where);
				return false;
			}
			if ((list.GetMember("entryList", &a_out) && a_out.IsArray()) ||
				(list.GetMember("_entryList", &a_out) && a_out.IsArray())) {
				return true;
			}
			spdlog::warn("[morphs] {} has no entryList array", where);
			return false;
		}

		// Every category key a slider entry belongs to. A built-in slider has
		// no such member and is not ours: it belongs to a category identified
		// by flag, which the rest of the panel already covers.
		std::vector<std::string> KeysOf(const RE::GFxValue& a_entry)
		{
			std::vector<std::string> keys;
			RE::GFxValue             filters;
			if (!a_entry.GetMember("textFilters", &filters) || !filters.IsArray()) {
				return keys;
			}
			const auto n = filters.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue key;
				if (filters.GetElement(i, &key) && key.IsString()) {
					std::string text = key.GetString();
					if (!text.empty()) {
						keys.push_back(std::move(text));
					}
				}
			}
			return keys;
		}

		bool ReadSlider(const RE::GFxValue& a_entry, Slider& a_out)
		{
			a_out.callback = Str(a_entry, "callbackName");
			a_out.label = Str(a_entry, "text");
			if (a_out.callback.empty()) {
				return false;
			}
			if (!Num(a_entry, "sliderMin", a_out.min) || !Num(a_entry, "sliderMax", a_out.max) ||
				!(a_out.max > a_out.min)) {
				return false;
			}
			if (!Num(a_entry, "interval", a_out.step) || !(a_out.step > 0)) {
				// A slider the film would draw without snapping. The page needs
				// a step, and the range over a hundred is the one RaceMenu's
				// own default slider uses.
				a_out.step = (a_out.max - a_out.min) / 100.0;
			}
			if (!Num(a_entry, "position", a_out.value)) {
				a_out.value = a_out.min;
			}
			return true;
		}

		// What the film's tab set looks like right now, cheaply: the key of
		// every mod category, plus the size of the slider list. Twenty
		// GetMember calls, no slider is read, nothing is allocated per entry.
		//
		// This exists because the first measured run read ZERO mod tabs and
		// the mechanism was fine: a mod registers its category from Papyrus
		// when the menu opens, and that had not happened yet. Round 1, at the
		// instant the panel came on screen, saw 17 categories and none of them
		// a mod's; round 2, five seconds later, saw 20 (Dev\Medicoes\Fase-6.7).
		// So the read cannot happen once, at a moment we pick - the film is
		// still being built. It is watched instead, and re-read when it moves.
		//
		// The slider count is in the signature because the tab set alone does
		// not move when a mod re-registers under the same key with a different
		// slider list - which is what changing the character's sex does.
		std::string Signature()
		{
			auto*        ui = RE::UI::GetSingleton();
			auto         menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto*        movie = menu ? menu->uiMovie.get() : nullptr;
			RE::GFxValue categories;
			if (!movie || !Entries(movie, "categoryList", categories)) {
				return {};
			}
			std::string out;
			const auto  n = categories.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue entry;
				if (!categories.GetElement(i, &entry) || !entry.IsObject()) {
					continue;
				}
				const auto key = Str(entry, "textFilter");
				if (!key.empty()) {
					out += key + ",";
				}
			}
			RE::GFxValue items;
			if (Entries(movie, "itemList", items)) {
				out += fmt::format("|{}", items.GetArraySize());
			}
			return out;
		}

		void Push(const char* a_why)
		{
			auto out = nlohmann::json::array();
			bool ready = false;
			{
				std::scoped_lock lock(g_lock);
				ready = g_read;
				for (const auto& cat : g_categories) {
					auto sliders = nlohmann::json::array();
					for (const auto& s : cat.sliders) {
						sliders.push_back({ { "c", s.callback }, { "label", s.label }, { "min", s.min },
							{ "max", s.max }, { "step", s.step }, { "value", s.value } });
					}
					out.push_back({ { "key", cat.key }, { "label", cat.label }, { "sliders", std::move(sliders) } });
				}
			}
			spdlog::info("[morphs] sending {} tabs ({}): ready {}", out.size(), a_why, ready);
			Panel::Bridge::Send("body-morphs", { { "ready", ready }, { "categories", std::move(out) } });
		}

		void ReadNow()
		{
			std::vector<Category> found;
			bool                  ready = false;
			auto*                 ui = RE::UI::GetSingleton();
			auto                  menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto*                 movie = menu ? menu->uiMovie.get() : nullptr;
			RE::GFxValue          categories;
			RE::GFxValue          items;
			if (!movie) {
				spdlog::warn("[morphs] RaceMenu film not reachable - no tabs read");
			} else if (Entries(movie, "categoryList", categories)) {
				// The categories are what decides "ready": a tab whose sliders
				// could not be read is still a tab, and it still needs a home
				// (the page draws it empty). Failing the whole read over the
				// slider list would leave those tabs with no home at all.
				ready = true;
				const bool haveItems = Entries(movie, "itemList", items);
				const auto catCount = categories.GetArraySize();
				for (std::uint32_t i = 0; i < catCount; ++i) {
					RE::GFxValue entry;
					if (!categories.GetElement(i, &entry) || !entry.IsObject()) {
						continue;
					}
					// A category with no textFilter is one of the film's own,
					// identified by flag; a category with one was added by a mod.
					const auto key = Str(entry, "textFilter");
					if (key.empty() || key == kBodyScales) {
						continue;
					}
					found.push_back({ key, Str(entry, "text"), {} });
				}

				int        skipped = 0;
				const auto itemCount = haveItems ? items.GetArraySize() : 0;
				for (std::uint32_t i = 0; i < itemCount; ++i) {
					RE::GFxValue entry;
					if (!items.GetElement(i, &entry) || !entry.IsObject()) {
						continue;
					}
					const auto keys = KeysOf(entry);
					if (keys.empty()) {
						continue;
					}
					Slider slider;
					if (!ReadSlider(entry, slider)) {
						++skipped;
						continue;
					}
					// A slider can name more than one key, and then it shows in
					// each of those tabs - which is what the film does with it.
					for (const auto& key : keys) {
						for (auto& cat : found) {
							if (cat.key == key) {
								cat.sliders.push_back(slider);
							}
						}
					}
				}

				std::string tabs;
				for (const auto& cat : found) {
					tabs += fmt::format("{}'{}'/{}={}", tabs.empty() ? "" : ", ", cat.label, cat.key,
						cat.sliders.size());
				}
				spdlog::info("[morphs] {} categories and {} on the film: {} mod tabs{}{}", catCount,
					haveItems ? fmt::format("{} list entries", itemCount) : std::string("NO slider list"),
					found.size(), tabs.empty() ? "" : ": ", tabs);
				if (skipped > 0) {
					spdlog::warn("[morphs] {} entries had no usable range and were left out", skipped);
				}
			}

			std::scoped_lock lock(g_lock);
			g_categories = std::move(found);
			g_read = ready;
		}

		void Read(const char* a_why)
		{
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) {
				spdlog::warn("[morphs] no task interface - the page gets no tabs");
				return;
			}
			const std::string why = a_why;
			tasks->AddUITask([why]() {
				ReadNow();
				Push(why.c_str());
			});
		}

		// What the film does after the player drags one of its own sliders:
		// the entry keeps the new value, so RaceMenu shows it too.
		void WriteBackNow(const std::string& a_callback, double a_value)
		{
			auto*        ui = RE::UI::GetSingleton();
			auto         menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto*        movie = menu ? menu->uiMovie.get() : nullptr;
			RE::GFxValue items;
			if (!movie || !Entries(movie, "itemList", items)) {
				return;
			}
			const auto n = items.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue entry;
				if (items.GetElement(i, &entry) && entry.IsObject() && Str(entry, "callbackName") == a_callback) {
					entry.SetMember("position", RE::GFxValue(a_value));
					return;
				}
			}
			spdlog::warn("[morphs] '{}' is no longer on the film - RaceMenu keeps its old value", a_callback);
		}

		void OnGet(const nlohmann::json&) { Read("the page asked"); }

		void OnSet(const nlohmann::json& a_data)
		{
			if (!a_data.contains("c") || !a_data["c"].is_string() || !a_data.contains("v") ||
				!a_data["v"].is_number()) {
				spdlog::warn("[morphs] body-morph-set without a callback and a value: {}", a_data.dump());
				return;
			}
			const auto callback = a_data["c"].get<std::string>();
			const auto value = a_data["v"].get<double>();
			const bool final = a_data.contains("final") && a_data["final"].is_boolean() && a_data["final"].get<bool>();

			const auto now = Clock::now();
			auto&      sent = g_sent[callback];
			if (!final && sent.second > 0 && now - sent.first < kGap) {
				return;
			}
			sent.first = now;
			const int n = ++sent.second;
			++g_sets;

			// The mod that registered this slider is listening for its own
			// callback name, and applies the value with its own morph key.
			Core::RaceMenuSync::SliderMoved(callback, static_cast<float>(value), n == 1 || final);

			if (!final) {
				return;
			}
			sent.second = 0;
			auto* tasks = SKSE::GetTaskInterface();
			if (tasks) {
				tasks->AddUITask([callback, value]() { WriteBackNow(callback, value); });
			}

			// The page is told the value it asked for, not one read back: what
			// holds a body morph is keyed by the mod's own key, and this
			// plugin does not have it (that is why the write is delegated at
			// all). It is the same thing RaceMenu's own slider does - it keeps
			// the value it sent and never asks the mod what came of it - so
			// the two agree. Without this the page would never mark the row
			// changed: that flag is written where a value ARRIVES.
			{
				std::scoped_lock lock(g_lock);
				for (auto& cat : g_categories) {
					for (auto& slider : cat.sliders) {
						if (slider.callback == callback) {
							slider.value = value;
						}
					}
				}
			}
			Push("after a final write");
		}

		void OnShown() { Read("the panel came on screen"); }

		// Watching the film, which is the whole reason the first run read no
		// tabs at all. Cheap by construction: a signature a second, and the
		// read that walks six hundred slider entries only when it moves.
		//
		// This also covers a case no round had reached yet - changing the
		// character's sex makes a mod register a different slider list under
		// the same key, and the signature carries the slider count for that
		// reason.
		void OnTick()
		{
			if (!Session::MenuOpen()) {
				g_signature.clear();
				return;
			}
			const auto now = Clock::now();
			if (now - g_looked < kLookGap) {
				return;
			}
			g_looked = now;
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) {
				return;
			}
			tasks->AddUITask([]() {
				const auto signature = Signature();
				if (signature == g_signature) {
					return;
				}
				const bool first = g_signature.empty();
				g_signature = signature;
				ReadNow();
				Push(first ? "the film was read" : "the film changed");
			});
		}

		void OnOpened()
		{
			g_sets = 0;
			g_sent.clear();
			// A new menu is a new film, and a mod registers its sliders again
			// each time - under conditions that can have changed, sex among
			// them.
			g_signature.clear();
			std::scoped_lock lock(g_lock);
			g_categories.clear();
			g_read = false;
		}

		void OnClosed() { spdlog::info("[morphs] RaceSex Menu closed: {} values sent", g_sets); }

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Session::OnPanelShown(&OnShown);
		Session::OnMenuOpened(&OnOpened);
		Session::OnTick(&OnTick);
		Session::OnMenuClosed(&OnClosed);
		Panel::Bridge::On("body-morphs-get", &OnGet);
		Panel::Bridge::On("body-morph-set", &OnSet);
		g_installed = true;
		spdlog::info("[morphs] installed");
	}
}
