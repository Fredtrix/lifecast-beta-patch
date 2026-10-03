// RaceMenuFilm.cpp
// Lifecast

#include "Panel/RaceMenuFilm.h"

namespace Lifecast::Panel::RaceMenuFilm
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		using HPType = RE::BGSHeadPart::HeadPartType;

		constexpr auto kList = "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.itemList";

		struct Label
		{
			const char* text;
			int         index;
		};

		// The game's face sliders by RaceMenu's label, against the faceData
		// morph they show (TESNPC::FaceData::Morphs). Every pair was read in
		// game with both sides agreeing.
		constexpr Label kMorphs[] = {
			{ "Nose Length", 0 },
			{ "Nose Height", 1 },
			{ "Jaw Height", 2 },
			{ "Jaw Width", 3 },
			{ "Jaw Forward", 4 },
			{ "Cheekbone Height", 5 },
			{ "Cheekbone Width", 6 },
			{ "Eye Height", 7 },
			{ "Eye Width", 8 },
			{ "Brow Height", 9 },
			{ "Brow Width", 10 },
			{ "Brow Forward", 11 },
			{ "Mouth Height", 12 },
			{ "Mouth Forward", 13 },
			{ "Chin Width", 14 },
			{ "Chin Length", 15 },
			{ "Chin Forward", 16 },
			{ "Eye Depth", 17 },
		};

		// The race's preset types, against faceData->parts (nose 0, eyes 2,
		// mouth 3). Both sides count them the same way.
		constexpr Label kTypes[] = {
			{ "Nose Type", 0 },
			{ "Eye Shape", 2 },
			{ "Mouth Shape", 3 },
		};

		constexpr HPType kPartTypes[] = { HPType::kHair, HPType::kEyes, HPType::kEyebrows, HPType::kScar,
			HPType::kFacialHair };

		// The list's refresh, in the order tried. InvalidateData is the one
		// the film was seen to have; the others are its usual aliases.
		constexpr const char* kRefresh[] = { "InvalidateData", "UpdateList", "invalidateData" };

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

		int IndexOf(const Label (&a_table)[18], const std::string& a_text)
		{
			for (const auto& l : a_table) {
				if (a_text == l.text) {
					return l.index;
				}
			}
			return -1;
		}

		int TypeOf(const std::string& a_text)
		{
			for (const auto& l : kTypes) {
				if (a_text == l.text) {
					return l.index;
				}
			}
			return -1;
		}

		// The entry's own part list, asked for with its sliderID - the one
		// argument that call takes (without it skee64 asserts and the game
		// crashes). The index of the part whose name carries a_editorID,
		// either alone (eyes) or in brackets after a display name (hair).
		int PartIndex(const RE::GFxValue& a_entry, const std::vector<std::string>& a_editorIDs)
		{
			double id = -1;
			if (a_editorIDs.empty() || !Num(a_entry, "sliderID", id)) {
				return -1;
			}
			RE::GFxValue                      result;
			RE::GFxValue                      parts;
			const std::array<RE::GFxValue, 1> arg{ RE::GFxValue(id) };
			if (!const_cast<RE::GFxValue&>(a_entry).Invoke("GetSliderPartData", &result, arg) || !result.IsObject() ||
				!result.GetMember("parts", &parts) || !parts.IsArray()) {
				return -1;
			}
			const auto n = parts.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue part;
				if (!parts.GetElement(i, &part)) {
					continue;
				}
				const auto name = Str(part, "name");
				for (const auto& editorID : a_editorIDs) {
					if (name == editorID || name.find("[" + editorID + "]") != std::string::npos) {
						double index = i;
						Num(part, "index", index);
						return static_cast<int>(index);
					}
				}
			}
			return -1;
		}

		// The player's face detail set among the race's.
		int ComplexionIndex(RE::TESNPC* a_npc)
		{
			auto*      race = a_npc->GetRace();
			const auto sex = static_cast<std::uint32_t>(a_npc->GetSex());
			auto*      rf = race && sex < RE::SEXES::kTotal ? race->faceRelatedData[sex] : nullptr;
			auto*      hrd = a_npc->headRelatedData;
			if (!rf || !rf->faceDetailsTextureSets || !hrd || !hrd->faceDetails) {
				return -1;
			}
			const auto& list = *rf->faceDetailsTextureSets;
			for (std::uint32_t i = 0; i < list.size(); ++i) {
				if (list[i] == hrd->faceDetails) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		bool Set(RE::GFxValue& a_entry, double a_value, int& a_written)
		{
			double pos = 0;
			if (Num(a_entry, "position", pos) && std::abs(pos - a_value) < 0.0005) {
				return false;
			}
			if (a_entry.SetMember("position", RE::GFxValue(a_value))) {
				++a_written;
				return true;
			}
			return false;
		}

		void SyncNow()
		{
			const auto start = Clock::now();
			auto*      ui = RE::UI::GetSingleton();
			auto       menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto*      movie = menu ? menu->uiMovie.get() : nullptr;
			auto*      player = RE::PlayerCharacter::GetSingleton();
			auto*      npc = player ? player->GetActorBase() : nullptr;
			auto*      face = npc ? npc->faceData : nullptr;
			if (!movie || !face) {
				spdlog::warn("[film] RaceMenu's sliders not updated: film {}, face data {}", movie != nullptr,
					face != nullptr);
				return;
			}

			RE::GFxValue list;
			RE::GFxValue entries;
			if (!movie->GetVariable(&list, kList) || !list.IsObject() || !list.GetMember("_entryList", &entries) ||
				!entries.IsArray()) {
				spdlog::warn("[film] RaceMenu's sliders not updated: no {}._entryList", kList);
				return;
			}

			std::vector<std::string> worn;
			for (const auto type : kPartTypes) {
				auto* part = npc->GetCurrentHeadPartByType(type);
				const std::string editorID = part ? part->GetFormEditorID() : "";
				if (!editorID.empty()) {
					worn.push_back(editorID);
				}
			}

			int           written = 0;
			int           seen = 0;
			std::string   changed;
			std::array<bool, 18> morphsDone{};
			const auto    n = entries.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue entry;
				if (!entries.GetElement(i, &entry) || !entry.IsObject()) {
					continue;
				}
				const auto callback = Str(entry, "callbackName");
				const auto text = Str(entry, "text");
				double     want = 0;
				if (callback == "ChangeDoubleMorph") {
					const int morph = IndexOf(kMorphs, text);
					// RaceMenu's own sliders come after the game's, and some
					// repeat a label ("Brow Width"): the first one is the game's.
					if (morph < 0 || morphsDone[morph]) {
						continue;
					}
					morphsDone[morph] = true;
					want = face->morphs[morph];
					// A new game's face data holds FLT_MAX for a moment: not a value.
					if (!(std::abs(want) <= 10.0)) {
						continue;
					}
				} else if (callback == "ChangePreset") {
					const int type = TypeOf(text);
					if (type < 0) {
						continue;
					}
					want = face->parts[type];
				} else if (callback == "ChangeHeadPart") {
					const int index = PartIndex(entry, worn);
					if (index < 0) {
						continue;
					}
					want = index;
				} else if (callback == "ChangeWeight") {
					// The weight slider runs from 0 to 1; the game keeps 0 to 100.
					want = npc->weight / 100.0;
				} else if (callback == "ChangeFaceDetails") {
					const int index = ComplexionIndex(npc);
					if (index < 0) {
						continue;
					}
					want = index;
				} else {
					continue;
				}
				++seen;
				if (Set(entry, want, written)) {
					changed += fmt::format("{}'{}' {}", changed.empty() ? "" : ", ", text, want);
				}
			}

			const char* refreshed = "-";
			if (written > 0) {
				refreshed = "none found";
				for (const char* name : kRefresh) {
					RE::GFxValue fn;
					if (list.GetMember(name, &fn) && !fn.IsUndefined()) {
						refreshed = list.Invoke(name) ? name : "failed";
						break;
					}
				}
			}
			const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
			spdlog::info("[film] RaceMenu's sliders: {} checked, {} updated, refresh {}, {} us{}{}", seen, written,
				refreshed, us, written > 0 ? ": " : "", changed);
		}

		struct Target
		{
			const char* callback = nullptr;
			const char* text = nullptr;  // nullptr: the first entry with that callback
		};

		Target TargetOf(Slider a_slider, int a_which)
		{
			switch (a_slider) {
			case Slider::kMorph:
				for (const auto& l : kMorphs) {
					if (l.index == a_which) {
						return { "ChangeDoubleMorph", l.text };
					}
				}
				break;
			case Slider::kType:
				for (const auto& l : kTypes) {
					if (l.index == a_which) {
						return { "ChangePreset", l.text };
					}
				}
				break;
			case Slider::kComplexion:
				return { "ChangeFaceDetails", nullptr };
			}
			return {};
		}

		// The sliderID of the first entry with that callback (and label).
		bool FindSliderID(RE::GFxMovieView* a_movie, const Target& a_target, double& a_id)
		{
			RE::GFxValue list;
			RE::GFxValue entries;
			if (!a_movie->GetVariable(&list, kList) || !list.IsObject() || !list.GetMember("_entryList", &entries) ||
				!entries.IsArray()) {
				return false;
			}
			const auto n = entries.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue entry;
				if (!entries.GetElement(i, &entry) || !entry.IsObject() || Str(entry, "callbackName") != a_target.callback) {
					continue;
				}
				if (a_target.text && Str(entry, "text") != a_target.text) {
					continue;
				}
				return Num(entry, "sliderID", a_id);
			}
			return false;
		}

		struct Pending
		{
			double value = 0;
			bool   final = false;
			Done   done;
		};

		using Key = std::pair<Slider, int>;

		std::mutex                 g_lock;
		std::map<Key, Pending>     g_pending;
		RE::GFxMovieView*          g_idsFor = nullptr;
		std::map<Key, double>      g_ids;
		std::map<Key, int>         g_calls;

		// Calls the menu's callback for one slider. Returns whether it was called.
		bool Deliver(const Key& a_key, double a_value, bool a_final)
		{
			const auto target = TargetOf(a_key.first, a_key.second);
			auto*      ui = RE::UI::GetSingleton();
			auto       menu = ui ? ui->GetMenu(RE::RaceSexMenu::MENU_NAME) : nullptr;
			auto*      movie = menu ? menu->uiMovie.get() : nullptr;
			if (!target.callback || !movie || !menu->fxDelegate) {
				spdlog::warn("[film] {} for slider {}: no target, film or GameDelegate - not handed to RaceMenu",
					target.callback ? target.callback : "no callback", a_key.second);
				return false;
			}

			if (movie != g_idsFor) {
				g_ids.clear();
				g_calls.clear();
				g_idsFor = movie;
			}
			auto found = g_ids.find(a_key);
			if (found == g_ids.end()) {
				double id = 0;
				if (!FindSliderID(movie, target, id)) {
					spdlog::warn("[film] {} '{}': no such entry in RaceMenu's list - not handed to RaceMenu",
						target.callback, target.text ? target.text : "(first)");
					return false;
				}
				found = g_ids.emplace(a_key, id).first;
			}

			auto* defn = menu->fxDelegate->callbacks.Get(RE::GString(target.callback));
			if (!defn || !defn->callback) {
				spdlog::warn("[film] {} is not registered in RaceMenu's GameDelegate - not handed to RaceMenu",
					target.callback);
				return false;
			}

			// The two arguments RaceMenu's own slider sends: value, sliderID.
			const std::array<RE::GFxValue, 2> args{ RE::GFxValue(a_value), RE::GFxValue(found->second) };
			const RE::FxDelegateArgs          params(RE::GFxValue(), defn->handler.get(), movie, args.data(),
						 static_cast<std::uint32_t>(args.size()));
			const auto start = Clock::now();
			defn->callback(params);
			const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();

			const int n = ++g_calls[a_key];
			if (n == 1 || a_final) {
				spdlog::info("[film] {}({}, {}) for '{}' took {} us (call {} for this slider{})", target.callback,
					a_value, found->second, target.text ? target.text : "(first)", us, n, a_final ? ", edit ended" : "");
			}
			return true;
		}

		void DeliverPending()
		{
			std::map<Key, Pending> batch;
			{
				std::scoped_lock lock(g_lock);
				batch.swap(g_pending);
			}
			for (auto& [key, p] : batch) {
				const bool delivered = Deliver(key, p.value, p.final);
				if (p.done) {
					p.done(delivered, p.final);
				}
			}
		}
	}

	void Apply(Slider a_slider, int a_which, double a_value, bool a_final, Done a_done)
	{
		bool schedule = false;
		{
			std::scoped_lock lock(g_lock);
			schedule = g_pending.empty();
			auto& p = g_pending[{ a_slider, a_which }];
			p.value = a_value;
			p.final = p.final || a_final;
			p.done = std::move(a_done);
		}
		if (!schedule) {
			return;
		}
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			spdlog::warn("[film] no task interface - RaceMenu's callback called at once");
			DeliverPending();
			return;
		}
		tasks->AddUITask([]() { DeliverPending(); });
	}

	void Sync()
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			spdlog::warn("[film] no task interface - RaceMenu's sliders keep their old values");
			return;
		}
		tasks->AddUITask([]() { SyncNow(); });
	}
}
