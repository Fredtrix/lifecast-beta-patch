// FilmSliders.cpp
// Lifecast

#include "Edit/FilmSliders.h"

#include "Edit/Session.h"
#include "Panel/Bridge.h"
#include "Panel/Panel.h"
#include "Panel/RaceMenuFilm.h"

namespace Lifecast::Edit::FilmSliders
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		using json = nlohmann::json;

		constexpr auto kPanels = "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance";
		constexpr auto kItems = "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.itemList";

		constexpr int kEntryRace = 1;
		constexpr int kEntrySlider = 2;
		// RaceMenu numbers the sliders a mod registers from here up.
		constexpr double kModSliderBase = 1000.0;
		constexpr auto   kMorphCallback = "ChangeDoubleMorph";

		// RaceMenu's own slider sends its mod event at most once per 100 ms
		// while it is dragged. A mod's script answers each one by rebuilding
		// the body, so the page's faster stream is held to the same gap here;
		// the value that ends a gesture is never held back.
		constexpr auto kEventGap = std::chrono::milliseconds(100);
		constexpr auto kWatchGap = std::chrono::seconds(1);

		// A position outside this is not a value (a fresh character's face
		// holds FLT_MAX for a moment).
		constexpr double kPlausible = 1.0e4;

		struct Tab
		{
			std::string   id;  // text for a tab by bit, textFilter for a tab by key
			std::string   text;
			std::string   label;
			std::string   key;
			std::uint32_t flag = 0;
		};

		struct Entry
		{
			std::string   k;
			std::string   cb;
			std::string   text;
			std::string   label;
			const char*   kind = "";
			std::string   tab;
			int           tabCount = 0;
			double        min = 0;
			double        max = 0;
			double        step = 0;
			double        value = 0;
			bool          hasValue = false;
			std::uint32_t at = 0;  // index in _entryList when last read
		};

		// Everything below is touched on the UI thread only, except where a
		// lock says otherwise.
		std::vector<Tab>                        g_tabs;
		std::vector<Entry>                      g_entries;
		std::unordered_map<std::string, size_t> g_byKey;
		std::size_t                             g_shape = 0;  // which keys, in which order
		bool                                    g_ready = false;
		std::unordered_map<std::string, std::string> g_labels;  // translation cache

		struct Pending
		{
			double value = 0;
			bool   final = false;
		};
		std::mutex                                g_lock;
		std::map<std::string, Pending>            g_pending;
		bool                                      g_queued = false;

		// Per key: when the mod event was last sent, and how many values the
		// gesture has had, so a drag logs its first value and its last.
		std::unordered_map<std::string, std::pair<Clock::time_point, int>> g_sent;

		Clock::time_point g_looked{};
		std::atomic<bool> g_watching = false;

		struct Counts
		{
			int       sets = 0;
			int       censuses = 0;
			long long censusMax = 0;
			int       watches = 0;
			long long watchMax = 0;
		};
		Counts g_counts;

		long long Since(Clock::time_point a_t0)
		{
			return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - a_t0).count();
		}

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

		RE::GFxMovieView* Movie()
		{
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			return menu ? menu->uiMovie.get() : nullptr;
		}

		// The film shows a list's entries at "_entryList", not at the public
		// name its ActionScript reads them by.
		bool Entries(RE::GFxMovieView* a_movie, const char* a_list, RE::GFxValue& a_listOut, RE::GFxValue& a_out)
		{
			const auto where = std::string(kPanels) + "." + a_list;
			if (!a_movie->GetVariable(&a_listOut, where.c_str()) || !a_listOut.IsObject()) {
				return false;
			}
			return (a_listOut.GetMember("_entryList", &a_out) && a_out.IsArray()) ||
			       (a_listOut.GetMember("entryList", &a_out) && a_out.IsArray());
		}

		// skyui.util.Translator.translate, the film's own: a "$" label comes
		// back as RaceMenu draws it. Every text is asked once per game.
		std::string Label(RE::GFxMovieView* a_movie, const std::string& a_text)
		{
			if (a_text.empty() || a_text[0] != '$') {
				return a_text;
			}
			if (const auto hit = g_labels.find(a_text); hit != g_labels.end()) {
				return hit->second;
			}
			std::string  out;
			RE::GFxValue tr;
			if (a_movie->GetVariable(&tr, "_global.skyui.util.Translator") && !tr.IsUndefined() && !tr.IsNull()) {
				RE::GFxValue arg;
				a_movie->CreateString(&arg, a_text.c_str());
				RE::GFxValue res;
				if (tr.Invoke("translate", &res, &arg, 1) && res.IsString()) {
					out = res.GetString();
				}
			}
			// No translation: the key without its "$" is still readable, and
			// it is not cached, so the next census asks again.
			if (out.empty() || out == a_text) {
				return a_text.substr(1);
			}
			g_labels.emplace(a_text, out);
			return out;
		}

		std::vector<std::string> TextFilters(const RE::GFxValue& a_entry)
		{
			std::vector<std::string> out;
			RE::GFxValue             tf;
			if (a_entry.GetMember("textFilters", &tf) && tf.IsArray()) {
				const auto n = tf.GetArraySize();
				for (std::uint32_t i = 0; i < n; ++i) {
					RE::GFxValue s;
					if (tf.GetElement(i, &s) && s.IsString() && *s.GetString() != '\0') {
						out.emplace_back(s.GetString());
					}
				}
			}
			return out;
		}

		std::vector<Tab> ReadTabs(RE::GFxMovieView* a_movie)
		{
			std::vector<Tab> tabs;
			RE::GFxValue     list, cats;
			if (!Entries(a_movie, "categoryList", list, cats)) {
				return tabs;
			}
			const auto n = cats.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue c;
				if (!cats.GetElement(i, &c) || !c.IsObject()) {
					continue;
				}
				Tab t;
				t.text = Str(c, "text");
				t.key = Str(c, "textFilter");
				double flag = 0;
				Num(c, "flag", flag);
				t.flag = flag > 0 ? static_cast<std::uint32_t>(flag) : 0;
				// The "all" tab carries every bit at once: it is every tab, not one.
				if (t.key.empty() && (t.flag == 0 || (t.flag & (t.flag - 1)) != 0)) {
					continue;
				}
				t.id = t.key.empty() ? t.text : t.key;
				t.label = Label(a_movie, t.text);
				tabs.push_back(std::move(t));
			}
			return tabs;
		}

		const char* KindOf(const std::string& a_text, double a_id, double a_min, double a_step)
		{
			if (a_id >= kModSliderBase) {
				return "plugin";
			}
			if (a_text.empty() || a_text[0] != '$') {
				return "engine";
			}
			return (a_min == 0.0 && a_step == 1.0) ? "skee-whole" : "skee";
		}

		bool Taken(int a_type, const std::string& a_cb, bool a_hasID, double a_id)
		{
			return a_type == kEntrySlider && (a_cb == kMorphCallback || (a_hasID && a_id >= kModSliderBase));
		}

		std::size_t Mix(std::size_t a_h, const std::string& a_s)
		{
			return a_h ^ (std::hash<std::string>{}(a_s) + 0x9e3779b97f4a7c15ULL + (a_h << 6) + (a_h >> 2));
		}

		bool Usable(double a_v) { return std::isfinite(a_v) && std::abs(a_v) <= kPlausible; }

		json ValueOf(const Entry& a_e) { return a_e.hasValue ? json(a_e.value) : json(nullptr); }

		json Message()
		{
			auto tabs = json::array();
			for (const auto& t : g_tabs) {
				tabs.push_back({ { "id", t.id }, { "text", t.text }, { "label", t.label }, { "key", t.key },
					{ "flag", t.flag } });
			}
			auto sliders = json::array();
			for (const auto& e : g_entries) {
				sliders.push_back({ { "k", e.k }, { "text", e.text }, { "label", e.label }, { "kind", e.kind },
					{ "tab", e.tab }, { "min", e.min }, { "max", e.max }, { "step", e.step }, { "value", ValueOf(e) } });
			}
			return { { "ready", g_ready }, { "tabs", std::move(tabs) }, { "sliders", std::move(sliders) } };
		}

		// The whole read: tabs, every entry, labels. Replaces the model and sends
		// it. a_why goes to the log.
		void Census(const std::string& a_why)
		{
			const auto t0 = Clock::now();
			auto*      movie = Movie();
			RE::GFxValue list, items;
			if (!movie || !Entries(movie, "itemList", list, items)) {
				spdlog::info("[sliders] census ({}): no film list yet{}", a_why,
					g_ready ? " - the page keeps the last one" : "");
				return;
			}

			std::vector<Tab>                        tabs = ReadTabs(movie);
			std::vector<Entry>                      entries;
			std::unordered_map<std::string, size_t> byKey;
			std::unordered_map<std::string, int>    repeats;
			std::map<std::string, int>              leftOut;
			std::map<std::string, int>              byKind;
			std::map<std::string, int>              byTab;
			int                                     noTab = 0;
			int                                     twoTabs = 0;
			std::size_t                             shape = 0;

			const auto n = items.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue e;
				if (!items.GetElement(i, &e) || !e.IsObject()) {
					continue;
				}
				double typeD = -1, id = -1;
				Num(e, "type", typeD);
				const int  type = static_cast<int>(typeD);
				const auto cb = Str(e, "callbackName");
				const bool hasID = Num(e, "sliderID", id);
				if (!Taken(type, cb, hasID, id)) {
					if (type == kEntrySlider) {
						++leftOut[cb.empty() ? "(no callback)" : cb];
					} else if (type == kEntryRace) {
						++leftOut["race"];
					}
					continue;
				}

				Entry x;
				x.cb = cb;
				x.text = Str(e, "text");
				x.at = i;
				Num(e, "sliderMin", x.min);
				Num(e, "sliderMax", x.max);
				if (!Num(e, "interval", x.step) || !(x.step > 0)) {
					// Drawn by the film without snapping; RaceMenu's own default is
					// a hundredth of the range.
					x.step = x.max > x.min ? (x.max - x.min) / 100.0 : 0.01;
				}
				double pos = 0;
				x.hasValue = Num(e, "position", pos) && Usable(pos);
				x.value = x.hasValue ? pos : 0;
				x.kind = KindOf(x.text, hasID ? id : -1, x.min, x.step);
				x.label = Label(movie, x.text);

				std::string base = x.text + "|" + cb;
				const int   rep = ++repeats[base];
				x.k = rep == 1 ? base : fmt::format("{}#{}", base, rep);

				double filterFlag = 0;
				const bool hasFlag = Num(e, "filterFlag", filterFlag);
				const auto filters = TextFilters(e);
				for (const auto& t : tabs) {
					const bool byBit = hasFlag && t.flag != 0 && t.key.empty() &&
					                   (static_cast<std::uint32_t>(filterFlag) & t.flag) != 0;
					const bool byText = !t.key.empty() && std::find(filters.begin(), filters.end(), t.key) != filters.end();
					if (byBit || byText) {
						if (x.tabCount++ == 0) {
							x.tab = t.id;
						}
					}
				}
				if (x.tabCount == 0) {
					++noTab;
				} else {
					++byTab[x.tab];
					twoTabs += x.tabCount > 1 ? 1 : 0;
				}
				++byKind[x.kind];
				shape = Mix(shape, x.k);
				byKey.emplace(x.k, entries.size());
				entries.push_back(std::move(x));
			}

			g_tabs = std::move(tabs);
			g_entries = std::move(entries);
			g_byKey = std::move(byKey);
			g_shape = shape;
			g_ready = true;

			const auto us = Since(t0);
			++g_counts.censuses;
			g_counts.censusMax = (std::max)(g_counts.censusMax, us);

			const auto kindN = [&](const char* k) {
				const auto it = byKind.find(k);
				return it == byKind.end() ? 0 : it->second;
			};
			std::string out;
			int         outN = 0;
			for (const auto& [cb, c] : leftOut) {
				out += fmt::format("{}{}={}", out.empty() ? "" : ", ", cb, c);
				outN += c;
			}
			spdlog::info("[sliders] census ({}): {} list entries, {} sliders (engine {}, skee {}, skee whole {}, "
						 "plugin {}) in {} tabs, {} us; left out {}: {}",
				a_why, n, g_entries.size(), kindN("engine"), kindN("skee"), kindN("skee-whole"), kindN("plugin"),
				g_tabs.size(), us, outN, out.empty() ? "none" : out);
			std::string tabsLine;
			for (const auto& t : g_tabs) {
				const auto it = byTab.find(t.id);
				tabsLine += fmt::format("{}'{}'={}", tabsLine.empty() ? "" : ", ", t.id, it == byTab.end() ? 0 : it->second);
			}
			spdlog::info("[sliders] by tab: {}; in no tab {}; in more than one tab {} (drawn in the first)", tabsLine,
				noTab, twoTabs);

			Panel::Bridge::Send("sliders", Message());
		}

		// The cheap read: which keys, in which order, and every position. A
		// different key set is a new list - census. Otherwise only the values
		// that moved go to the page. Returns how many moved.
		int Watch(const char* a_why, bool a_logQuiet)
		{
			const auto   t0 = Clock::now();
			auto*        movie = Movie();
			RE::GFxValue list, items;
			if (!movie || !Entries(movie, "itemList", list, items)) {
				return 0;
			}
			std::unordered_map<std::string, int> repeats;
			std::size_t                          shape = 0;
			std::vector<std::pair<size_t, double>> seen;
			std::vector<size_t>                    lost;
			const auto                             n = items.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue e;
				if (!items.GetElement(i, &e) || !e.IsObject()) {
					continue;
				}
				double typeD = -1, id = -1;
				Num(e, "type", typeD);
				const auto cb = Str(e, "callbackName");
				const bool hasID = Num(e, "sliderID", id);
				if (!Taken(static_cast<int>(typeD), cb, hasID, id)) {
					continue;
				}
				std::string base = Str(e, "text") + "|" + cb;
				const int   rep = ++repeats[base];
				const auto  k = rep == 1 ? base : fmt::format("{}#{}", base, rep);
				shape = Mix(shape, k);
				const auto hit = g_byKey.find(k);
				if (hit == g_byKey.end()) {
					continue;
				}
				g_entries[hit->second].at = i;
				double pos = 0;
				if (Num(e, "position", pos) && Usable(pos)) {
					seen.emplace_back(hit->second, pos);
				} else {
					lost.push_back(hit->second);
				}
			}

			const auto us = Since(t0);
			++g_counts.watches;
			g_counts.watchMax = (std::max)(g_counts.watchMax, us);

			if (shape != g_shape || !g_ready) {
				Census(fmt::format("{}: the film's list changed", a_why));
				return -1;
			}
			json values = json::object();
			int  moved = 0;
			for (const auto& [idx, pos] : seen) {
				auto& x = g_entries[idx];
				if (!x.hasValue || std::abs(x.value - pos) > 1e-6) {
					x.value = pos;
					x.hasValue = true;
					values[x.k] = pos;
					++moved;
				}
			}
			for (const auto idx : lost) {
				auto& x = g_entries[idx];
				if (x.hasValue) {
					x.hasValue = false;
					values[x.k] = nullptr;
					++moved;
				}
			}
			if (moved > 0 || a_logQuiet) {
				spdlog::info("[sliders] read back ({}): {} values changed since the page last had them, {} us", a_why,
					moved, us);
			}
			if (moved > 0) {
				Panel::Bridge::Send("slider-values", { { "values", std::move(values) } });
			}
			return moved;
		}

		// The entry under a_key, read from the film now: the index the last read
		// left is tried first, and the whole list only if that entry moved.
		bool Find(RE::GFxValue& a_items, const Entry& a_x, RE::GFxValue& a_out, double& a_id)
		{
			const auto matches = [&](const RE::GFxValue& e) {
				return Str(e, "text") == a_x.text && Str(e, "callbackName") == a_x.cb && Num(e, "sliderID", a_id);
			};
			if (a_items.GetElement(a_x.at, &a_out) && a_out.IsObject() && matches(a_out) && a_x.k.find('#') == std::string::npos) {
				return true;
			}
			// A repeat (#n) is the n-th entry with that text and callback.
			int want = 1;
			if (const auto hash = a_x.k.rfind('#'); hash != std::string::npos) {
				want = std::atoi(a_x.k.c_str() + hash + 1);
			}
			int        seen = 0;
			const auto n = a_items.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				if (a_items.GetElement(i, &a_out) && a_out.IsObject() && matches(a_out) && ++seen == want) {
					return true;
				}
			}
			return false;
		}

		// RaceMenu's own slider routine, in its order (see the header).
		void WriteNow(const std::string& a_key, double a_value, bool a_final)
		{
			const auto t0 = Clock::now();
			const auto hit = g_byKey.find(a_key);
			if (hit == g_byKey.end()) {
				spdlog::warn("[sliders] set '{}': no such slider on the last list - nothing written", a_key);
				return;
			}
			auto&        x = g_entries[hit->second];
			auto*        movie = Movie();
			RE::GFxValue list, items, e;
			double       id = -1;
			if (!movie || !Entries(movie, "itemList", list, items) || !Find(items, x, e, id)) {
				spdlog::warn("[sliders] set '{}': the entry is no longer on the film - nothing written", a_key);
				return;
			}

			double v = std::clamp(a_value, (std::min)(x.min, x.max), (std::max)(x.min, x.max));
			if (std::strcmp(x.kind, "skee-whole") == 0) {
				v = std::round(v);
			}

			bool         called = false;
			RE::GFxValue gd;
			if (movie->GetVariable(&gd, "_global.gfx.io.GameDelegate") && !gd.IsUndefined() && !gd.IsNull()) {
				RE::GFxValue name, params;
				movie->CreateString(&name, x.cb.c_str());
				movie->CreateArray(&params);
				params.PushBack(RE::GFxValue(v));
				params.PushBack(RE::GFxValue(id));
				const std::array<RE::GFxValue, 2> args{ name, params };
				called = gd.Invoke("call", nullptr, args);
			}
			const bool positioned = e.SetMember("position", RE::GFxValue(v));

			const auto now = Clock::now();
			auto&      sent = g_sent[a_key];
			const int  step = ++sent.second;
			bool       evented = false;
			bool       held = false;
			if (a_final || step == 1 || now - sent.first >= kEventGap) {
				RE::GFxValue skse;
				if (movie->GetVariable(&skse, "_global.skse") && skse.IsObject()) {
					RE::GFxValue ev, cbv;
					movie->CreateString(&ev, "RSM_SliderChange");
					movie->CreateString(&cbv, x.cb.c_str());
					const std::array<RE::GFxValue, 3> args{ ev, cbv, RE::GFxValue(v) };
					evented = skse.Invoke("SendModEvent", nullptr, args);
				}
				sent.first = now;
			} else {
				held = true;
			}
			bool redrawn = false;
			if (a_final) {
				redrawn = list.Invoke("InvalidateData");
			}

			double back = v;
			Num(e, "position", back);
			x.value = back;
			x.hasValue = Usable(back);
			++g_counts.sets;

			if (step == 1 || a_final) {
				spdlog::info("[sliders] set '{}' ({}) {} sliderID {} callback {}: {} -> position {} (GameDelegate.call {}, "
							 "position {}, RSM_SliderChange {}, InvalidateData {}), {} us, value {} of the gesture{}",
					x.text, x.label, x.kind, id, x.cb, a_value, back, called, positioned,
					held ? "held" : (evented ? "true" : "false"), a_final ? (redrawn ? "true" : "false") : "-", Since(t0),
					step, a_final ? " (final)" : "");
			}
			if (a_final) {
				sent.second = 0;
				// What the film holds now, read back: the page marks a row changed
				// where a value arrives.
				Panel::Bridge::Send("slider-values", { { "values", { { x.k, ValueOf(x) } } } });
			}
		}

		void Drain()
		{
			std::map<std::string, Pending> work;
			{
				std::scoped_lock lock(g_lock);
				work.swap(g_pending);
				g_queued = false;
			}
			for (const auto& [k, p] : work) {
				WriteNow(k, p.value, p.final);
			}
		}

		void Queue(std::function<void()> a_fn)
		{
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddUITask(std::move(a_fn));
			} else {
				spdlog::warn("[sliders] no task interface - the film is not reached");
			}
		}

		void OnSet(const json& a_data)
		{
			if (!a_data.contains("k") || !a_data["k"].is_string() || !a_data.contains("v") || !a_data["v"].is_number()) {
				spdlog::warn("[sliders] slider-set without a key and a value: {}", a_data.dump());
				return;
			}
			const bool final = a_data.contains("final") && a_data["final"].is_boolean() && a_data["final"].get<bool>();
			bool       queue = false;
			{
				std::scoped_lock lock(g_lock);
				auto&            p = g_pending[a_data["k"].get<std::string>()];
				p.value = a_data["v"].get<double>();
				p.final = p.final || final;
				queue = !g_queued;
				g_queued = true;
			}
			if (queue) {
				Queue(&Drain);
			}
		}

		void OnGet(const json&)
		{
			Queue([]() {
				if (g_ready) {
					Panel::Bridge::Send("sliders", Message());
				} else {
					Census("the page asked");
				}
			});
		}

		void OnDrawn(const json& a_data)
		{
			const auto rows = a_data.value("rows", 0);
			std::string sections;
			if (a_data.contains("sections") && a_data["sections"].is_object()) {
				for (const auto& [k, v] : a_data["sections"].items()) {
					sections += fmt::format("{}{}={}", sections.empty() ? "" : ", ", k, v.dump());
				}
			}
			std::string missing;
			int         notDrawn = 0;
			if (a_data.contains("notDrawn") && a_data["notDrawn"].is_array()) {
				for (const auto& k : a_data["notDrawn"]) {
					++notDrawn;
					if (notDrawn <= 10) {
						missing += fmt::format("{}{}", missing.empty() ? "" : ", ", k.is_string() ? k.get<std::string>() : k.dump());
					}
				}
			}
			spdlog::info("[sliders] page drew {} rows for {} sliders: {}; not drawn {}{}{}", rows,
				a_data.value("sliders", 0), sections, notDrawn, notDrawn ? ": " : "", missing);
		}

		void OnShown()
		{
			g_looked = Clock::now();
			Queue([]() {
				if (Watch("the panel came on screen", true) == 0 && !g_ready) {
					Census("the panel came on screen");
				}
			});
		}

		// A preset or a race change replaced the character behind the film's
		// back. The game's own 18 morphs are put back on their entries first
		// (RaceMenuFilm, queued ahead of this), so what is read is the face.
		// The panel-shown callback that runs right after reads the list.
		void OnReplaced()
		{
			Panel::RaceMenuFilm::Sync();
		}

		void OnTick()
		{
			if (!Session::MenuOpen() || !Panel::IsVisible() || g_watching.load()) {
				return;
			}
			const auto now = Clock::now();
			if (now - g_looked < kWatchGap) {
				return;
			}
			g_looked = now;
			g_watching.store(true);
			Queue([]() {
				Watch("watch", false);
				g_watching.store(false);
			});
		}

		void OnOpened()
		{
			Queue([]() {
				g_counts = {};
				g_sent.clear();
				g_ready = false;
				g_shape = 0;
				Census("the editor opened");
			});
		}

		void OnClosed()
		{
			Queue([]() {
				spdlog::info("[sliders] RaceSex Menu closed: {} sets, {} censuses (max {} us), {} watches (max {} us)",
					g_counts.sets, g_counts.censuses, g_counts.censusMax, g_counts.watches, g_counts.watchMax);
			});
		}

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Session::OnMenuOpened(&OnOpened);
		Session::OnReplaced(&OnReplaced);
		Session::OnPanelShown(&OnShown);
		Session::OnTick(&OnTick);
		Session::OnMenuClosed(&OnClosed);
		Panel::Bridge::On("sliders-get", &OnGet);
		Panel::Bridge::On("slider-set", &OnSet);
		Panel::Bridge::On("sliders-drawn", &OnDrawn);
		g_installed = true;
		spdlog::info("[sliders] installed");
	}
}
