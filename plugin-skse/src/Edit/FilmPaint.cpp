// FilmPaint.cpp
// Lifecast

#include "Edit/FilmPaint.h"

#include "Edit/Overlays.h"
#include "Edit/Tints.h"

namespace Lifecast::Edit::FilmPaint
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		using Type = RE::TintMask::Type;

		// RaceMenu's panel object: the film functions RaceMenu's script calls
		// live on it, and so does the slider list.
		constexpr auto kPanels = "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance";

		// The film's slider entries are type 2; the two pick sliders are found
		// by the sliderID the film gives them.
		constexpr double kSliderEntry = 2.0;

		struct Pick
		{
			const char* label;
			double      sliderID;
			Type        type;
		};

		constexpr Pick kPicks[] = {
			{ "War Paint", 7.0, Type::kWarPaint },
			{ "Dirt", 4.0, Type::kDirt },
		};

		bool Num(const RE::GFxValue& a_obj, const char* a_name, double& a_out)
		{
			RE::GFxValue v;
			if (a_obj.GetMember(a_name, &v) && v.IsNumber()) {
				a_out = v.GetNumber();
				return true;
			}
			return false;
		}

		// One film function, one string argument per row - the shape of
		// RaceMenu's ui.InvokeStringA.
		bool CallWithRows(RE::GFxMovie* a_movie, RE::GFxValue& a_panels, const char* a_function,
			const std::vector<std::string>& a_rows)
		{
			std::vector<RE::GFxValue> args(a_rows.size());
			for (std::size_t i = 0; i < a_rows.size(); ++i) {
				a_movie->CreateString(&args[i], a_rows[i].c_str());
			}
			return a_panels.Invoke(a_function, nullptr, args.data(), args.size());
		}

		// The first slider entry whose member a_name equals a_value.
		bool FindSlider(RE::GFxValue& a_entries, const char* a_name, double a_value, RE::GFxValue& a_out)
		{
			const auto n = a_entries.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				double type = -1;
				double v = -1;
				if (a_entries.GetElement(i, &a_out) && a_out.IsObject() && Num(a_out, "type", type) &&
					type == kSliderEntry && Num(a_out, a_name, v) && v == a_value) {
					return true;
				}
			}
			return false;
		}

		std::string SetPicks(RE::GFxValue& a_entries)
		{
			std::string out;
			for (const auto& p : kPicks) {
				if (!out.empty()) {
					out += ", ";
				}
				int          showing = 0;
				std::int32_t argb = 0;
				const int    want = Tints::ShownMask(p.type, showing, argb);
				RE::GFxValue entry;
				if (!FindSlider(a_entries, "sliderID", p.sliderID, entry)) {
					out += fmt::format("{} pick: no entry", p.label);
					continue;
				}
				double     before = 0;
				const bool had = Num(entry, "position", before);
				const bool set = entry.SetMember("position", RE::GFxValue(static_cast<double>(want)));
				out += fmt::format("{} pick {} -> {} ({} showing){}", p.label, had ? fmt::format("{}", before) : "?",
					want, showing, set ? "" : " NOT SET");

				// The type's colour slider. RSM_AddTints gives it the colour of
				// the first mask of the type in the list, and a later mask's
				// colour only when it is above 0xFFFFFF - which a mask with an
				// alpha above half never is, as the signed Int the script hands
				// over (negative). So it keeps showing the first mask's colour while
				// another one is showing; it is given the shown mask's colour
				// here, the way that function stores it. With none showing it
				// keeps what RSM_AddTints gave it.
				if (want < 0) {
					continue;
				}
				RE::GFxValue colour;
				if (!FindSlider(a_entries, "tintType", static_cast<double>(p.type), colour)) {
					out += ", no colour entry";
					continue;
				}
				const bool coloured = colour.SetMember("fillColor", RE::GFxValue(static_cast<double>(argb)));
				out += fmt::format(", colour {:08X}{}", static_cast<std::uint32_t>(argb), coloured ? "" : " NOT SET");
			}
			return out;
		}

		void SyncNow(const char* a_why)
		{
			const auto t0 = Clock::now();
			auto*      ui = RE::UI::GetSingleton();
			auto       menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto*      movie = menu ? menu->uiMovie.get() : nullptr;
			RE::GFxValue panels;
			if (!movie || !movie->GetVariable(&panels, kPanels) || !panels.IsObject()) {
				spdlog::warn("[filmpaint] {}: no film - RaceMenu's tints and paints keep their old values", a_why);
				return;
			}

			// No tint list means nothing to say about the masks: an empty call
			// would clear the film's makeup rows instead.
			const auto  tints = Tints::FilmTintRows();
			std::string tinted = "no tint list";
			if (!tints.empty()) {
				tinted = CallWithRows(movie, panels, "RSM_AddTints", tints) ? "true" : "false";
			}

			std::string painted;
			for (const auto& area : Overlays::FilmOverlayRows()) {
				const bool ok = CallWithRows(movie, panels, area.function, area.rows);
				painted += fmt::format("{}{} {} ({})", painted.empty() ? "" : ", ", area.label, area.rows.size(), ok);
			}
			if (painted.empty()) {
				painted = "none (no SKEE or no skeleton)";
			}

			std::string  picks = "no item list";
			bool         redrawn = false;
			RE::GFxValue list;
			RE::GFxValue entries;
			if (panels.GetMember("itemList", &list) && list.IsObject() && list.GetMember("_entryList", &entries) &&
				entries.IsArray()) {
				picks = SetPicks(entries);
				redrawn = list.Invoke("InvalidateData");
			}

			const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
			spdlog::info("[filmpaint] {}: tints {} (RSM_AddTints {}), overlays {}, {}, InvalidateData {}, {} us", a_why,
				tints.size(), tinted, painted, picks, redrawn, us);
		}
	}

	void Sync(const char* a_why)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			spdlog::warn("[filmpaint] no task interface - RaceMenu's tints and paints keep their old values");
			return;
		}
		tasks->AddUITask([a_why]() { SyncNow(a_why); });
	}
}
