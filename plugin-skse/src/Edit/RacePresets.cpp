// RacePresets.cpp
// Lifecast

#include "Edit/RacePresets.h"

#include "Edit/Session.h"
#include "Core/ListSig.h"
#include "Panel/Bridge.h"
#include "Panel/RaceMenuFilm.h"

namespace Lifecast::Edit::RacePresets
{
	namespace
	{
		using HeadPart = RE::BGSHeadPart;
		using HPType = RE::BGSHeadPart::HeadPartType;
		using FaceData = RE::TESNPC::FaceData;
		using RaceFace = RE::TESRace::FaceRelatedData;

		// The head part types the log reads back, the same five Edit/Appearance
		// offers one at a time.
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
			auto        names = nlohmann::json::array();
			std::string canonical;
			for (const auto* preset : g_lists.presets) {
				const auto name = Core::ListSig::Sanitize(NameOf(preset));
				canonical += name + "\n";
				names.push_back(name);
			}
			spdlog::info("[race-presets] {}: sending {} presets", a_why, g_lists.presets.size());
			Panel::Bridge::Send("race-presets",
				{ { "count", static_cast<int>(g_lists.presets.size()) }, { "names", std::move(names) },
					{ "sig", Core::ListSig::Stamp("race-presets", canonical, g_lists.presets.size()) } });
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
			// The game's face morphs: 18 plausible values, then FLT_MAX.
			for (int i = 0; i < 18; ++i) {
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

		std::string FormText(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return "-";
			}
			const char* id = a_form->GetFormEditorID();
			return fmt::format("{:08X}{}{}", a_form->GetFormID(), id && *id ? " " : "", id && *id ? id : "");
		}

		std::string ColorText(const RE::Color& a_color)
		{
			return fmt::format("{:02X}{:02X}{:02X}{:02X}", a_color.red, a_color.green, a_color.blue, a_color.alpha);
		}

		// Every field a preset is supposed to set, read out of the structure, one
		// line each and the same format before, after and for the preset itself, so
		// the three can be compared line by line. A probe: it changes nothing.
		// a_player: the tint masks are the player's own and a preset has none.
		void LogFields(const char* a_tag, RE::TESNPC* a_npc, bool a_player)
		{
			if (!a_npc) {
				return;
			}
			spdlog::info("[race-presets]   {} morphs: {}", a_tag, MorphLine(a_npc->faceData));
			std::string parts;
			for (int i = 0; i < a_npc->numHeadParts && a_npc->headParts; ++i) {
				const auto* part = a_npc->headParts[i];
				parts += fmt::format("{}{}={}", i ? " " : "", part ? static_cast<int>(part->type.get()) : -1,
					part ? FormText(part) : "-");
			}
			spdlog::info("[race-presets]   {} headparts: {}", a_tag, parts);
			std::string layers;
			int         count = 0;
			if (a_npc->tintLayers) {
				for (const auto* layer : *a_npc->tintLayers) {
					if (!layer) {
						continue;
					}
					++count;
					// The mask type of this layer's index, from the player's race: it is
					// what says which layers are war paint (7) and which are dirt (14).
					int type = -1;
					if (auto* rf = RaceFaceOf(Npc()); rf && rf->tintMasks) {
						for (const auto* asset : *rf->tintMasks) {
							if (asset && asset->texture.index == layer->tintIndex) {
								type = static_cast<int>(asset->texture.skinTone.get());
								break;
							}
						}
					}
					layers += fmt::format(" {}(T{}):{}/{}/{}", layer->tintIndex, type, ColorText(layer->tintColor),
						layer->interpolationValue, layer->preset);
				}
			}
			spdlog::info("[race-presets]   {} tintlayers: n={}{}", a_tag, count, layers);
			if (a_player) {
				auto*       player = RE::PlayerCharacter::GetSingleton();
				auto*       list = player ? player->GetTintList() : nullptr;
				std::string masks;
				int         lit = 0;
				if (list) {
					for (const auto* mask : *list) {
						if (!mask || mask->alpha <= 0.0f) {
							continue;
						}
						++lit;
						masks += fmt::format(" {}:{}:{}:{:.2f}", static_cast<int>(mask->type.get()),
							mask->texture ? mask->texture->textureName.c_str() : "-", ColorText(mask->color), mask->alpha);
					}
				}
				spdlog::info("[race-presets]   {} tintmasks (alpha > 0): n={}{}", a_tag, lit, masks);
			}
			const auto* hrd = a_npc->headRelatedData;
			spdlog::info("[race-presets]   {} complexion: {}", a_tag, hrd ? FormText(hrd->faceDetails) : "(no head data)");
			spdlog::info("[race-presets]   {} haircolour: {} {}", a_tag, hrd ? FormText(hrd->hairColor) : "(no head data)",
				hrd && hrd->hairColor ? ColorText(hrd->hairColor->color) : "-");
			spdlog::info("[race-presets]   {} weight: {:.1f}  bodytint: {}", a_tag, a_npc->weight,
				ColorText(a_npc->bodyTintColor));
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
			spdlog::info("[race-presets] applying [{}] {} {:08X}", index, name, preset->GetFormID());
			LogFields("before", npc, true);
			LogFields("wanted", preset, false);

			// The preset is handed to the callback RaceMenu's own Presets slider
			// calls, and the game does what it does for that slider: nothing here
			// decides which fields a preset carries or which it clears. Queued on
			// the UI thread; the callback below runs there once it has returned.
			Panel::RaceMenuFilm::Apply(Panel::RaceMenuFilm::Slider::kHeadPreset, 0, index, true,
				[index, name](bool a_delivered, bool) {
					if (!a_delivered) {
						Answer(index, false, "RaceMenu's Presets callback could not be called");
						return;
					}
					auto* now = Npc();
					++g_applied;
					// Replaced runs so every section drops the values it kept from
					// this session's edits before it re-reads.
					Session::Replaced("a race preset was applied");
					LogFields("after", now, true);
					spdlog::info("[race-presets] applied [{}] {}: through RaceMenu's ChangeHeadPreset", index, name);
					Answer(index, true, name);
				});
		}

		void OnGet(const nlohmann::json&) { PushList("the page asked"); }

		void OnShown()
		{
			PushList("the panel came on screen");
			// The player may have chosen a preset on RaceMenu's own slider meanwhile;
			// the face already shows it (the other sections re-read), the row must too.
			Panel::RaceMenuFilm::ReadNativePreset([](int a_index) {
				if (a_index < 0 || a_index >= static_cast<int>(g_lists.presets.size())) {
					spdlog::warn("[race-presets] RaceMenu's own slider rests on {}, outside the list of {}", a_index,
						g_lists.presets.size());
					return;
				}
				spdlog::info("[race-presets] RaceMenu's own slider rests on [{}]: the row follows", a_index);
				Panel::Bridge::Send("race-preset-native", { { "i", a_index } });
			});
		}

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

	void LogPlayerFields(const char* a_tag) { LogFields(a_tag, Npc(), true); }

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
