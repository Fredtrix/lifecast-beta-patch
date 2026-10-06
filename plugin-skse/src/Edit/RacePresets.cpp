// RacePresets.cpp
// Lifecast

#include "Edit/RacePresets.h"

#include "Edit/FaceSliders.h"
#include "Edit/Session.h"
#include "Panel/Bridge.h"

namespace Lifecast::Edit::RacePresets
{
	namespace
	{
		using HeadPart = RE::BGSHeadPart;
		using HPType = RE::BGSHeadPart::HeadPartType;
		using FaceData = RE::TESNPC::FaceData;
		using RaceFace = RE::TESRace::FaceRelatedData;

		// The head part types a preset brings over: the same five
		// Edit/Appearance offers one at a time. A type the preset has no part
		// for is left alone rather than cleared - the game's swap wants both
		// sides, and a preset that names no scar is not a preset that removes
		// one.
		constexpr HPType kPartTypes[] = {
			HPType::kHair,
			HPType::kFacialHair,
			HPType::kEyes,
			HPType::kScar,
			HPType::kEyebrows,
		};

		// The list for one race and sex, in the order the race declares it.
		// The page's index points into this, so it is rebuilt whenever either
		// changes - the same reason Edit/Appearance rebuilds its lists.
		struct Lists
		{
			RE::TESRace*             race = nullptr;
			RE::SEX                  sex = RE::SEX::kNone;
			std::vector<RE::TESNPC*> presets;
		} g_lists;

		std::uint32_t g_applied = 0;

		RE::TESNPC* Npc()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			return player ? player->GetActorBase() : nullptr;
		}

		RaceFace* RaceFaceOf(RE::TESNPC* a_npc)
		{
			auto* race = a_npc ? a_npc->GetRace() : nullptr;
			if (!race) {
				return nullptr;
			}
			const auto sex = static_cast<std::uint32_t>(a_npc->GetSex());
			return sex < RE::SEXES::kTotal ? race->faceRelatedData[sex] : nullptr;
		}

		std::string NameOf(const RE::TESNPC* a_preset)
		{
			if (!a_preset) {
				return {};
			}
			// Every preset's full name is "Prisoner", which tells a player
			// nothing: the editor ID is what distinguishes them.
			const char* id = a_preset->GetFormEditorID();
			if (id && *id) {
				return id;
			}
			return fmt::format("{:08X}", a_preset->GetFormID());
		}

		// Rebuilds the list if the player's race or sex is not the one it was
		// built for. Returns false without a player.
		bool EnsureList(const char* a_why)
		{
			auto* npc = Npc();
			if (!npc) {
				return false;
			}
			auto*      race = npc->GetRace();
			const auto sex = npc->GetSex();
			if (race && race == g_lists.race && sex == g_lists.sex) {
				return true;
			}

			g_lists = {};
			g_lists.race = race;
			g_lists.sex = sex;
			if (auto* rf = RaceFaceOf(npc); rf && rf->presetNPCs) {
				for (auto* preset : *rf->presetNPCs) {
					// A null entry would shift every later index by one: the
					// list the page indexes into holds only real presets.
					if (preset) {
						g_lists.presets.push_back(preset);
					}
				}
			}
			spdlog::info("[race-presets] list rebuilt ({}): race {:08X}, sex {}, {} presets", a_why,
				race ? race->GetFormID() : 0, static_cast<std::uint32_t>(sex), g_lists.presets.size());
			for (std::size_t i = 0; i < g_lists.presets.size(); ++i) {
				spdlog::info("[race-presets]   [{}] {:08X} {}", i, g_lists.presets[i]->GetFormID(),
					NameOf(g_lists.presets[i]));
			}
			return true;
		}

		void PushList(const char* a_why)
		{
			if (!EnsureList(a_why)) {
				spdlog::info("[race-presets] {}: no player yet - sending an empty list", a_why);
				Panel::Bridge::Send("race-presets", { { "count", 0 }, { "names", nlohmann::json::array() } });
				return;
			}
			auto names = nlohmann::json::array();
			for (const auto* preset : g_lists.presets) {
				names.push_back(NameOf(preset));
			}
			spdlog::info("[race-presets] {}: sending {} presets", a_why, g_lists.presets.size());
			Panel::Bridge::Send("race-presets",
				{ { "count", static_cast<int>(g_lists.presets.size()) }, { "names", std::move(names) } });
		}

		void Answer(int a_index, bool a_ok, std::string a_why)
		{
			if (!a_ok) {
				spdlog::warn("[race-presets] apply {} refused: {}", a_index, a_why);
			}
			Panel::Bridge::Send("race-preset-applied",
				{ { "i", a_index }, { "ok", a_ok }, { "why", std::move(a_why) } });
		}

		// The eighteen chargen morphs and the nose/eye/mouth type indices.
		// Logged as one line of before and one of after, both read back out of
		// the structure, because the measurement that this control and not the
		// editor's own idle changed the face is the same field read twice.
		std::string MorphLine(const FaceData* a_face)
		{
			if (!a_face) {
				return "(no face data)";
			}
			std::string out;
			for (int i = 0; i < FaceSliders::kUsableMorphs; ++i) {
				out += fmt::format("{}{:.3f}", i ? " " : "", a_face->morphs[i]);
			}
			out += fmt::format(" | parts {} {} {}", a_face->parts[FaceData::Parts::kNose],
				a_face->parts[FaceData::Parts::kEyes], a_face->parts[FaceData::Parts::kMouth]);
			return out;
		}

		std::string PartLine(RE::TESNPC* a_npc)
		{
			std::string out;
			for (const auto type : kPartTypes) {
				auto* part = a_npc ? a_npc->GetCurrentHeadPartByType(type) : nullptr;
				const char* id = part ? part->GetFormEditorID() : nullptr;
				out += fmt::format("{}{}={}", out.empty() ? "" : " ", static_cast<int>(type), id && *id ? id : "-");
			}
			return out;
		}

		// The write itself, shared by the page's pick and the sex swap's reset.
		// a_setWeight: the sex swap does not touch the weight in the game, a
		// preset picked by hand does set it.
		void ApplyTo(RE::TESNPC* npc, RE::TESNPC* preset, int index, const std::string& name, bool a_setWeight)
		{
			spdlog::info("[race-presets] applying [{}] {} {:08X}", index, name, preset->GetFormID());
			spdlog::info("[race-presets]   morphs before: {}", MorphLine(npc->faceData));
			spdlog::info("[race-presets]   parts before:  {}", PartLine(npc));
			for (int i = 0; i < FaceSliders::kUsableMorphs; ++i) {
				npc->faceData->morphs[i] = preset->faceData->morphs[i];
			}
			// The three slots the game names, and only those. The fourth
			// (Parts::kUnknown) held -1 on every preset read, and nothing here
			// knows what it is for: copying a field no one understands is not
			// fidelity to the preset, it is a write without a reason.
			npc->faceData->parts[FaceData::Parts::kNose] = preset->faceData->parts[FaceData::Parts::kNose];
			npc->faceData->parts[FaceData::Parts::kEyes] = preset->faceData->parts[FaceData::Parts::kEyes];
			npc->faceData->parts[FaceData::Parts::kMouth] = preset->faceData->parts[FaceData::Parts::kMouth];

			int swapped = 0;
			for (const auto type : kPartTypes) {
				auto* wanted = preset->GetHeadPartByType(type);
				if (!wanted) {
					continue;
				}
				if (npc->GetCurrentHeadPartByType(type) == wanted) {
					continue;
				}
				npc->ChangeHeadPart(wanted);
				++swapped;
			}

			const auto* hrd = preset->headRelatedData;
			if (hrd && hrd->hairColor) {
				npc->SetHairColor(hrd->hairColor);
			}
			if (hrd && hrd->faceDetails) {
				npc->SetFaceTexture(hrd->faceDetails);
			}
			const float weightBefore = npc->weight;
			if (a_setWeight) {
				npc->weight = preset->weight;
			}

			++g_applied;
			// DoReset3D(true) is what makes all of this show (Edit/Session.h);
			// a_now, because applying a preset is a finished edit and not a
			// drag. Replaced runs first so every section drops the values it
			// kept from this session's edits before it re-reads.
			Session::Replaced("a race preset was applied");
			Session::RequestRedraw(true);

			// Read back out of the structure, not echoed from the preset.
			spdlog::info("[race-presets]   morphs after:  {}", MorphLine(npc->faceData));
			spdlog::info("[race-presets]   parts after:   {}", PartLine(npc));
			spdlog::info("[race-presets] applied [{}] {}: {} head parts swapped, weight {:.1f} -> {:.1f}, "
				"hair colour {}, complexion {}",
				index, name, swapped, weightBefore, npc->weight, hrd && hrd->hairColor ? "set" : "unchanged",
				hrd && hrd->faceDetails ? "set" : "unchanged");
		}

		void OnApply(const nlohmann::json& a_data)
		{
			if (!Session::MenuOpen()) {
				spdlog::warn("[race-presets] race-preset-apply with the RaceSex Menu closed - ignored");
				return;
			}
			if (!a_data.is_object() || !a_data.contains("i") || !a_data["i"].is_number_integer()) {
				spdlog::warn("[race-presets] race-preset-apply without an integer \"i\" - ignored");
				return;
			}
			const int index = a_data["i"].get<int>();

			auto* npc = Npc();
			if (!npc || !npc->faceData) {
				Answer(index, false, "the player has no face data");
				return;
			}
			// The page's index points into the list it was last sent. If race
			// or sex changed since, it points into the wrong list: refuse, and
			// send the new one.
			if (npc->GetRace() != g_lists.race || npc->GetSex() != g_lists.sex) {
				Answer(index, false, "race or sex changed since the list was sent");
				PushList("race or sex changed");
				return;
			}
			if (index < 0 || index >= static_cast<int>(g_lists.presets.size())) {
				Answer(index, false, fmt::format("outside [0, {})", g_lists.presets.size()));
				return;
			}
			auto* preset = g_lists.presets[index];
			if (!preset || !preset->faceData) {
				Answer(index, false, "that preset has no face data");
				return;
			}

			const auto name = NameOf(preset);
			ApplyTo(npc, preset, index, name, true);
			Answer(index, true, name);
		}

		void OnGet(const nlohmann::json&) { PushList("the page asked"); }

		void OnShown() { PushList("the panel came on screen"); }

		void OnOpened()
		{
			g_applied = 0;
			g_lists = {};
		}

		void OnClosed()
		{
			spdlog::info("[race-presets] RaceSex Menu closed: {} presets applied", g_applied);
		}

		bool g_installed = false;
	}

	bool ApplyDefault(const char* a_why)
	{
		auto* npc = Npc();
		if (!npc || !npc->faceData || !EnsureList(a_why) || g_lists.presets.empty()) {
			spdlog::warn("[race-presets] default preset ({}): nothing to apply (no player, no face data or no presets)",
				a_why);
			return false;
		}
		// What the race lists first is the one a fresh character starts on.
		auto* preset = g_lists.presets.front();
		if (!preset || !preset->faceData) {
			spdlog::warn("[race-presets] default preset ({}): the first preset has no face data", a_why);
			return false;
		}
		for (std::size_t i = 0; i < g_lists.presets.size(); ++i) {
			if (g_lists.presets[i] && g_lists.presets[i]->faceData) {
				spdlog::info("[race-presets]   candidate [{}] {}: {}", i, NameOf(g_lists.presets[i]),
					MorphLine(g_lists.presets[i]->faceData));
			}
		}
		ApplyTo(npc, preset, 0, NameOf(preset), false);
		return true;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Session::OnPanelShown(&OnShown);
		Session::OnMenuOpened(&OnOpened);
		Session::OnMenuClosed(&OnClosed);
		Panel::Bridge::On("race-presets-get", &OnGet);
		Panel::Bridge::On("race-preset-apply", &OnApply);
		g_installed = true;
		spdlog::info("[race-presets] installed");
	}
}
