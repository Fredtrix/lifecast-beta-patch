// FaceSliders.cpp
// Lifecast

#include "Edit/FaceSliders.h"

#include "Edit/Session.h"
#include "Panel/Bridge.h"
#include "Panel/Panel.h"
#include "Panel/RaceMenuFilm.h"

namespace Lifecast::Edit::FaceSliders
{
	namespace
	{
		using FaceData = RE::TESNPC::FaceData;

		static_assert(kUsableMorphs < FaceData::Morphs::kTotal,
			"the usable morphs stop before the VampireMorph entry");

		// Whether the last "face" sent to the page carried real values. While
		// it did not, every tick checks whether they have arrived.
		bool g_sentReady = false;

		std::uint32_t g_sets = 0;

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

		void OnFaceGet(const nlohmann::json&)
		{
			PushFace("the page asked");
		}

		void OnFaceSet(const nlohmann::json& a_data)
		{
			if (!Session::MenuOpen()) {
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

			++g_sets;
			// RaceMenu's own slider callback reshapes the head in place; the
			// value is not written here first, so the menu sees the change.
			// Without the callback, the value is written and the actor redrawn.
			const float before = face->morphs[index];
			Panel::RaceMenuFilm::Apply(Panel::RaceMenuFilm::Slider::kMorph, index, value, final,
				[index, value, before](bool a_delivered, bool a_final) {
					auto* face = PlayerFace();
					if (!face) {
						return;
					}
					if (!a_delivered) {
						face->morphs[index] = value;
						Session::RequestRedraw(a_final);
					}
					if (a_final) {
						spdlog::info("[face] morph[{}] = {:.3f} (drag ended, {}); face data {:.3f} -> {:.3f}", index,
							value, a_delivered ? "through RaceMenu" : "written, full redraw", before,
							face->morphs[index]);
						// What the page shows is what the structure holds, read
						// back - not what it asked for.
						PushFace("after a final write");
					}
				});
		}

		void OnShown() { PushFace("the panel came on screen"); }

		// The values were placeholders when last sent; send again as soon as
		// the engine has filled them in.
		void OnTick()
		{
			if (!g_sentReady && Panel::IsVisible() && Session::MenuOpen() && Ready(PlayerFace())) {
				PushFace("the face data became ready");
			}
		}

		void OnOpened()
		{
			g_sets = 0;
			g_sentReady = false;
		}

		void OnClosed() { spdlog::info("[face] RaceSex Menu closed: {} values written", g_sets); }

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}

		Session::OnPanelShown(&OnShown);
		Session::OnTick(&OnTick);
		Session::OnMenuOpened(&OnOpened);
		Session::OnMenuClosed(&OnClosed);

		Panel::Bridge::On("face-get", &OnFaceGet);
		Panel::Bridge::On("face-set", &OnFaceSet);

		g_installed = true;
		spdlog::info("[face] installed - {} face morph sliders", kUsableMorphs);
	}
}
