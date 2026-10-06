// Tints.cpp
// Lifecast

#include "Edit/Tints.h"

#include "Core/RaceMenuSync.h"
#include "Core/TintRefresh.h"
#include "Edit/Overlays.h"
#include "Edit/Session.h"
#include "Core/ListSig.h"
#include "Panel/Bridge.h"

namespace Lifecast::Edit::Tints
{
	namespace
	{
		using Type = RE::TintMask::Type;
		using Asset = RE::TESRace::FaceRelatedData::TintAsset;

		struct Layer
		{
			const char* key;
			const char* label;
			Type        type;
			bool        pick;
		};

		// Page order: the tints RaceMenu offers, once each. The freckle and
		// tattoo masks (type 0, no mask type in the race record) are left out:
		// RaceMenu has no control for them either.
		constexpr Layer kLayers[] = {
			{ "skin", "Skin Tone", Type::kSkinTone, false },
			{ "lips", "Lips", Type::kLips, false },
			{ "cheeks", "Cheeks", Type::kCheeks, false },
			{ "cheeks-lower", "Lower Cheeks", Type::kLowerCheeks, false },
			{ "eye-socket-upper", "Upper Eye Socket", Type::kUpperEyeSocket, false },
			{ "eye-socket-lower", "Lower Eye Socket", Type::kLowerEyeSocket, false },
			{ "eyeliner", "Eyeliner", Type::kEyeliner, false },
			{ "laugh-lines", "Laugh Lines", Type::kFrownLines, false },
			{ "nose", "Nose", Type::kNose, false },
			{ "forehead", "Forehead", Type::kForehead, false },
			{ "chin", "Chin", Type::kChin, false },
			{ "neck", "Neck", Type::kNeck, false },
			{ "paint", "War Paint", Type::kWarPaint, true },
			{ "dirt", "Dirt", Type::kDirt, true },
		};

		std::uint32_t g_sets = 0;

		std::vector<RE::TintMask*> MasksOf(Type a_type)
		{
			std::vector<RE::TintMask*> out;
			auto*                      player = RE::PlayerCharacter::GetSingleton();
			auto*                      list = player ? player->GetTintList() : nullptr;
			if (!list) {
				return out;
			}
			for (auto* mask : *list) {
				if (mask && mask->type.get() == a_type) {
					out.push_back(mask);
				}
			}
			return out;
		}

		const char* TextureOf(const RE::TintMask* a_mask)
		{
			return a_mask && a_mask->texture ? a_mask->texture->textureName.c_str() : "";
		}

		// The race's tint asset for this mask's texture, or null. Matched by
		// texture path, not by position.
		const Asset* AssetOf(const RE::TintMask* a_mask)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* npc = player ? player->GetActorBase() : nullptr;
			auto* race = npc ? npc->GetRace() : nullptr;
			if (!race || !a_mask) {
				return nullptr;
			}
			const auto sex = static_cast<std::uint32_t>(npc->GetSex());
			auto*      rf = sex < RE::SEXES::kTotal ? race->faceRelatedData[sex] : nullptr;
			if (!rf || !rf->tintMasks) {
				return nullptr;
			}
			const char* want = TextureOf(a_mask);
			for (const auto* asset : *rf->tintMasks) {
				if (asset && _stricmp(asset->texture.file.textureName.c_str(), want) == 0) {
					return asset;
				}
			}
			return nullptr;
		}

		std::string Hex(const RE::Color& a_c)
		{
			return fmt::format("#{:02X}{:02X}{:02X}", a_c.red, a_c.green, a_c.blue);
		}

		bool ParseHex(const std::string& a_s, RE::Color& a_out)
		{
			if (a_s.size() != 7 || a_s[0] != '#') {
				return false;
			}
			for (std::size_t i = 1; i < 7; ++i) {
				if (!std::isxdigit(static_cast<unsigned char>(a_s[i]))) {
					return false;
				}
			}
			const auto v = std::stoul(a_s.substr(1), nullptr, 16);
			a_out.red = static_cast<std::uint8_t>((v >> 16) & 0xFF);
			a_out.green = static_cast<std::uint8_t>((v >> 8) & 0xFF);
			a_out.blue = static_cast<std::uint8_t>(v & 0xFF);
			return true;
		}

		nlohmann::json Swatches(const RE::TintMask* a_mask)
		{
			auto        out = nlohmann::json::array();
			const auto* asset = AssetOf(a_mask);
			if (asset) {
				for (const auto* c : asset->presets.colors) {
					if (c) {
						out.push_back(Hex(c->color));
					}
				}
			}
			return out;
		}

		// A texture's name for a pop-up: the file, without the folder and the
		// extension - what the page's own Catalog row does with a path it cannot
		// find in the catalog.
		std::string ItemName(const char* a_path)
		{
			std::string name = a_path ? a_path : "";
			const auto  slash = name.find_last_of("\\/");
			if (slash != std::string::npos) {
				name.erase(0, slash + 1);
			}
			const auto dot = name.find_last_of('.');
			if (dot != std::string::npos && _stricmp(name.c_str() + dot, ".dds") == 0) {
				name.erase(dot);
			}
			return Core::ListSig::Sanitize(name);
		}

		// The first mask of a picked type that is showing, or -1.
		int ShownIndex(const std::vector<RE::TintMask*>& a_masks)
		{
			for (std::size_t i = 0; i < a_masks.size(); ++i) {
				if (a_masks[i]->alpha > 0.0F) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		void PushTints(const char* a_why)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->GetTintList()) {
				spdlog::info("[tints] {}: no player tint list yet - sending not ready", a_why);
				Panel::Bridge::Send("tints", { { "ready", false }, { "layers", nlohmann::json::array() } });
				return;
			}

			auto layers = nlohmann::json::array();
			for (const auto& l : kLayers) {
				const auto masks = MasksOf(l.type);
				if (masks.empty()) {
					continue;
				}
				const int  shown = l.pick ? ShownIndex(masks) : 0;
				const auto* mask = masks[shown >= 0 ? shown : 0];
				nlohmann::json layer = { { "k", l.key }, { "label", l.label }, { "kind", l.pick ? "pick" : "plain" },
					{ "color", Hex(mask->color) }, { "alpha", shown >= 0 ? mask->alpha : 1.0F },
					{ "swatches", Swatches(mask) } };
				if (l.pick) {
					layer["count"] = masks.size();
					layer["index"] = shown;
					// The whole list, for the pop-up: one name per mask, in the order
					// the index points into.
					auto        items = nlohmann::json::array();
					std::string canonical;
					for (const auto* m : masks) {
						const auto name = ItemName(TextureOf(m));
						canonical += name + "\n";
						items.push_back(nlohmann::json::array({ name }));
					}
					auto stamp = Core::ListSig::Stamp(std::string("tint:") + l.key, canonical, masks.size());
					stamp["items"] = std::move(items);
					layer["list"] = std::move(stamp);
				}
				if (l.type == Type::kWarPaint) {
					layer["texture"] = shown >= 0 ? TextureOf(mask) : "";
					auto        catalog = nlohmann::json::array();
					std::string canonical;
					std::size_t n = 0;
					for (const auto& p : Overlays::WarPaintCatalog()) {
						const auto name = Core::ListSig::Sanitize(p.name);
						const auto texture = Core::ListSig::Sanitize(p.texture);
						canonical += name + "\t" + texture + "\n";
						catalog.push_back({ { "name", name }, { "texture", texture } });
						++n;
					}
					layer["catalog"] = std::move(catalog);
					layer["catalogSig"] = Core::ListSig::Stamp("tint:paint-catalog", canonical, n);
				}
				layers.push_back(std::move(layer));
			}
			spdlog::info("[tints] {}: sending {} layers from {} masks", a_why, layers.size(),
				player->GetTintList()->size());
			Panel::Bridge::Send("tints", { { "ready", true }, { "layers", std::move(layers) } });
		}

		// The skin tone mask colours the head only; the body takes its colour
		// from the NPC's body tint (QNAM), which Actor::UpdateSkinColor pushes
		// to the body's shaders. Without this the neck shows a seam.
		void FollowSkin(const RE::TintMask* a_mask, bool a_final)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* npc = player ? player->GetActorBase() : nullptr;
			if (!npc || !a_mask) {
				return;
			}
			const auto before = npc->bodyTintColor;
			npc->bodyTintColor.red = a_mask->color.red;
			npc->bodyTintColor.green = a_mask->color.green;
			npc->bodyTintColor.blue = a_mask->color.blue;
			player->UpdateSkinColor();
			if (a_final) {
				spdlog::info("[tints] body tint {} -> {} (follows the skin tone)", Hex(before),
					Hex(npc->bodyTintColor));
			}
		}

		// Changes the texture a mask shows: the field SKSE's
		// Game.SetTintMaskTexturePath writes for RaceMenu's catalog. The log
		// reads the field back, and says whether the mask's texture is the
		// race's own record - writing it would then rename the race's asset,
		// which every actor of that race and sex shares (measured, not assumed:
		// the header cannot say which it is).
		void SetTexture(const std::string& a_key, int a_index, RE::TintMask* a_mask, const std::string& a_path)
		{
			if (!a_mask->texture) {
				spdlog::warn("[tints] {}: mask {} has no texture record - texture not set", a_key, a_index);
				return;
			}
			const std::string before = TextureOf(a_mask);
			const auto*       asset = AssetOf(a_mask);
			const bool        shared = asset && &asset->texture.file == a_mask->texture;
			a_mask->texture->textureName = a_path;
			spdlog::info("[tints] {} texture, mask {}: \"{}\" -> \"{}\" (read back \"{}\"; race asset {}{})", a_key,
				a_index, before, a_path, TextureOf(a_mask), asset ? "found" : "not found",
				asset ? (shared ? ", SAME record as the mask" : ", a different record from the mask") : "");
			if (asset) {
				spdlog::info("[tints] {} race asset texture now \"{}\"", a_key, asset->texture.file.textureName.c_str());
			}
		}

		const Layer* Find(const std::string& a_key)
		{
			for (const auto& l : kLayers) {
				if (a_key == l.key) {
					return &l;
				}
			}
			return nullptr;
		}

		void OnTintsGet(const nlohmann::json&)
		{
			PushTints("the page asked");
		}

		void OnTintSet(const nlohmann::json& a_data)
		{
			if (!Session::MenuOpen()) {
				spdlog::warn("[tints] tint-set with the RaceSex Menu closed - ignored");
				return;
			}
			if (!a_data.is_object() || !a_data.contains("k") || !a_data["k"].is_string()) {
				spdlog::warn("[tints] tint-set without a string \"k\" - ignored");
				return;
			}
			const auto   key = a_data["k"].get<std::string>();
			const Layer* layer = Find(key);
			if (!layer) {
				spdlog::warn("[tints] tint-set for unknown layer \"{}\" - ignored", key);
				return;
			}
			const auto masks = MasksOf(layer->type);
			if (masks.empty()) {
				spdlog::warn("[tints] tint-set {}: the player has no mask of this type - ignored, resending", key);
				PushTints("layer missing");
				return;
			}

			// Read and check everything before writing anything.
			RE::Color color;
			const bool hasColor = a_data.contains("color");
			if (hasColor && (!a_data["color"].is_string() || !ParseHex(a_data["color"].get<std::string>(), color))) {
				spdlog::warn("[tints] tint-set {}: \"color\" is not #RRGGBB - ignored", key);
				return;
			}
			float      alpha = 0.0F;
			const bool hasAlpha = a_data.contains("alpha");
			if (hasAlpha) {
				if (!a_data["alpha"].is_number() || !std::isfinite(a_data["alpha"].get<float>())) {
					spdlog::warn("[tints] tint-set {}: \"alpha\" is not a finite number - ignored", key);
					return;
				}
				alpha = std::clamp(a_data["alpha"].get<float>(), 0.0F, 1.0F);
			}
			int        index = 0;
			const bool hasIndex = a_data.contains("index");
			if (hasIndex) {
				if (!layer->pick || !a_data["index"].is_number_integer()) {
					spdlog::warn("[tints] tint-set {}: \"index\" only for a picked layer, as an integer - ignored", key);
					return;
				}
				index = a_data["index"].get<int>();
				if (index < -1 || index >= static_cast<int>(masks.size())) {
					spdlog::warn("[tints] tint-set {}: index {} is outside [-1, {}) - ignored", key, index,
						masks.size());
					return;
				}
			}
			// A texture from the war paint catalog. Only a path the catalog
			// carries is taken: the page never sends a path of its own.
			std::string texture;
			const bool  hasTexture = a_data.contains("texture");
			if (hasTexture) {
				if (layer->type != Type::kWarPaint || !a_data["texture"].is_string()) {
					spdlog::warn("[tints] tint-set {}: \"texture\" only for war paint, as a string - ignored", key);
					return;
				}
				texture = a_data["texture"].get<std::string>();
				bool known = false;
				for (const auto& p : Overlays::WarPaintCatalog()) {
					if (_stricmp(p.texture.c_str(), texture.c_str()) == 0) {
						known = true;
						break;
					}
				}
				if (!known) {
					spdlog::warn("[tints] tint-set {}: texture \"{}\" is not in the war paint catalog - ignored", key,
						texture);
					return;
				}
				if (hasIndex && index < 0) {
					spdlog::warn("[tints] tint-set {}: a texture with index -1 (none) - ignored", key);
					return;
				}
			}
			if (!hasColor && !hasAlpha && !hasIndex && !hasTexture) {
				spdlog::warn("[tints] tint-set {}: nothing to set - ignored", key);
				return;
			}

			// The mask being edited: for a picked layer, the one picked now
			// or the one showing.
			int target = 0;
			if (layer->pick) {
				const int shown = ShownIndex(masks);
				target = hasIndex ? index : shown;
				if (target < 0 && hasTexture) {
					// Nothing showing: the first mask takes the paint, at full
					// intensity, the way a pick from "None" would show it.
					target = 0;
					masks[0]->alpha = 1.0F;
				}
				if (target < 0 && !hasIndex) {
					spdlog::warn("[tints] tint-set {}: nothing is picked - pick one first; ignored", key);
					return;
				}
				if (target < 0) {
					// "none": clear the type.
					for (auto* m : masks) {
						m->alpha = 0.0F;
					}
				} else if (hasIndex && target != shown) {
					// Moving the pick carries the colour and intensity along.
					// Only the mask it moves from is cleared: RaceMenu can
					// show several paint layers at once, and the others are
					// not this control's to remove.
					auto* from = shown >= 0 ? masks[shown] : nullptr;
					float carried = 1.0F;
					if (from) {
						masks[target]->color = from->color;
						carried = from->alpha;
						from->alpha = 0.0F;
					}
					masks[target]->alpha = carried;
				}
			}
			if (target >= 0) {
				auto* mask = masks[target];
				if (hasColor) {
					mask->color.red = color.red;
					mask->color.green = color.green;
					mask->color.blue = color.blue;
				}
				if (hasAlpha) {
					mask->alpha = alpha;
				}
				if (hasTexture) {
					SetTexture(key, target, mask, texture);
				}
			}
			++g_sets;

			const bool final = a_data.contains("final") && a_data["final"].is_boolean() && a_data["final"].get<bool>();
			if (layer->type == Type::kSkinTone && hasColor && target >= 0) {
				FollowSkin(masks[target], final);
			}
			// Shown the way RaceMenu shows a mask colour change, without
			// DoReset3D - see Core/TintRefresh.h.
			Core::TintRefresh::Request(final);
			if (final) {
				if (target >= 0) {
					spdlog::info("[tints] {} = mask {} ({}) {} alpha {:.2f} (drag ended)", key, target,
						TextureOf(masks[target]), Hex(masks[target]->color), masks[target]->alpha);
				} else {
					spdlog::info("[tints] {} = none (drag ended)", key);
				}
				// RaceMenu keeps its own copy of the masks; without this its
				// tint controls show the old values and a reload restores them.
				Core::RaceMenuSync::TintsChanged();
				if (hasTexture) {
					// The texture is in that copy too, and RaceMenu's script
					// writes it back on every load: it has to hold the new
					// path, or a reload brings the old paint back.
					Core::RaceMenuSync::PresetLoaded();
				}
				PushTints("after a final write");
			}
		}

		void OnShown() { PushTints("the panel came on screen"); }

		void OnOpened() { g_sets = 0; }

		void OnClosed()
		{
			spdlog::info("[tints] RaceSex Menu closed: {} values written", g_sets);
			Core::TintRefresh::LogAndReset();
		}

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Session::OnPanelShown(&OnShown);
		Session::OnMenuOpened(&OnOpened);
		Session::OnMenuClosed(&OnClosed);
		Panel::Bridge::On("tints-get", &OnTintsGet);
		Panel::Bridge::On("tint-set", &OnTintSet);
		g_installed = true;
		spdlog::info("[tints] installed");
	}

	void Resend(const char* a_why)
	{
		if (Session::MenuOpen()) {
			PushTints(a_why);
		}
	}
}
