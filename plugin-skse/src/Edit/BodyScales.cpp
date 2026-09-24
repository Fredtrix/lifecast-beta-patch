// BodyScales.cpp
// Atelier

#include "Edit/BodyScales.h"

#include "Core/SkeeLink.h"
#include "Edit/Session.h"
#include "Panel/Bridge.h"

namespace Atelier::Edit::BodyScales
{
	namespace
	{
		// The key RaceMenu's body sliders write under. Sharing it is the
		// point: see the header.
		constexpr const char* kKey = "RSMPlugin";

		enum class Who
		{
			kEveryone,
			kFemale,
		};

		struct Node
		{
			const char* key;
			const char* label;
			const char* node;
			float       min;
			float       max;
			Who         who;
			bool        always;  // offered even without checking the node
		};

		// Nodes, ranges and conditions as RaceMenu offers them.
		constexpr Node kNodes[] = {
			{ "height", "Height", "NPC", 0.25F, 2.00F, Who::kEveryone, true },
			{ "head", "Head", "NPC Head [Head]", 0.01F, 3.00F, Who::kEveryone, false },
			{ "breast-l", "Left Breast", "NPC L Breast", 0.1F, 3.0F, Who::kFemale, false },
			{ "breast-r", "Right Breast", "NPC R Breast", 0.1F, 3.0F, Who::kFemale, false },
			{ "breast-curve-l", "Left Breast Curve", "NPC L Breast01", 0.1F, 3.0F, Who::kFemale, false },
			{ "breast-curve-r", "Right Breast Curve", "NPC R Breast01", 0.1F, 3.0F, Who::kFemale, false },
			{ "glute-l", "Left Glute", "NPC L Butt", 0.1F, 3.0F, Who::kFemale, false },
			{ "glute-r", "Right Glute", "NPC R Butt", 0.1F, 3.0F, Who::kFemale, false },
			{ "biceps-l", "Left Biceps", "NPC L UpperarmTwist1 [LUt1]", 0.1F, 2.0F, Who::kEveryone, true },
			{ "biceps-r", "Right Biceps", "NPC R UpperarmTwist1 [RUt1]", 0.1F, 2.0F, Who::kEveryone, true },
			{ "biceps2-l", "Left Biceps 2", "NPC L UpperarmTwist2 [LUt2]", 0.1F, 2.0F, Who::kEveryone, true },
			{ "biceps2-r", "Right Biceps 2", "NPC R UpperarmTwist2 [RUt2]", 0.1F, 2.0F, Who::kEveryone, true },
			{ "quiver", "Quiver", "QUIVER", 0.1F, 3.0F, Who::kEveryone, false },
			{ "bow", "Bow", "WeaponBow", 0.1F, 3.0F, Who::kEveryone, false },
			{ "axe", "Axe", "WeaponAxe", 0.1F, 3.0F, Who::kEveryone, false },
			{ "sword", "Sword", "WeaponSword", 0.1F, 3.0F, Who::kEveryone, false },
			{ "mace", "Mace", "WeaponMace", 0.1F, 3.0F, Who::kEveryone, false },
			{ "shield", "Shield", "SHIELD", 0.1F, 3.0F, Who::kEveryone, false },
			{ "weapon-back", "Weapon Back", "WeaponBack", 0.1F, 3.0F, Who::kEveryone, false },
			{ "weapon", "Weapon", "WEAPON", 0.1F, 3.0F, Who::kEveryone, false },
		};

		std::uint32_t g_sets = 0;

		Skee::INiTransformInterface* NiTransform()
		{
			static Skee::INiTransformInterface* s_iface = nullptr;
			static bool                         s_asked = false;
			if (!s_asked) {
				s_asked = true;
				auto* map = Core::SkeeLink::GetInterfaceMap();
				auto* raw = map ? map->QueryInterface("NiTransform") : nullptr;
				s_iface = static_cast<Skee::INiTransformInterface*>(raw);
				spdlog::info("[scales] SKEE NiTransform interface: {}",
					s_iface ? "found" : "not found - body scales disabled");
			}
			return s_iface;
		}

		// SKEE takes its own forward-declared TESObjectREFR. TESObjectREFR is
		// Actor's first base, at offset 0 on every runtime, so the upcast is
		// a no-op on the pointer and only the type changes; no member of the
		// object is read here.
		Skee::TESObjectREFR* SkeeRef(RE::PlayerCharacter* a_player)
		{
			return reinterpret_cast<Skee::TESObjectREFR*>(static_cast<RE::TESObjectREFR*>(a_player));
		}

		bool IsFemale(RE::PlayerCharacter* a_player)
		{
			auto* npc = a_player->GetActorBase();
			return npc && npc->GetSex() == RE::SEX::kFemale;
		}

		bool Offered(RE::PlayerCharacter* a_player, const Node& a_node, bool a_female)
		{
			if (a_node.who == Who::kFemale && !a_female) {
				return false;
			}
			if (a_node.always) {
				return true;
			}
			auto* root = a_player->Get3D(false);
			return root && root->GetObjectByName(RE::BSFixedString(a_node.node)) != nullptr;
		}

		const Node* Find(const std::string& a_key)
		{
			for (const auto& n : kNodes) {
				if (a_key == n.key) {
					return &n;
				}
			}
			return nullptr;
		}

		// Collects the scales SKEE holds for one node under our key, by the
		// visitor - a second way in, next to Has/Get.
		class ScaleVisitor : public Skee::INiTransformInterface::NodeVisitor
		{
		public:
			ScaleVisitor(const char* a_node) :
				_node(a_node) {}

			bool VisitPosition(const char*, const char*, Skee::INiTransformInterface::Position&) override
			{
				return false;
			}
			bool VisitRotation(const char*, const char*, Skee::INiTransformInterface::Rotation&) override
			{
				return false;
			}
			bool VisitScale(const char* a_node, const char* a_key, float a_scale) override
			{
				++seen;
				if (a_node && a_key && std::string_view(a_node) == _node && std::string_view(a_key) == kKey) {
					found = true;
					value = a_scale;
				}
				return false;
			}
			bool VisitScaleMode(const char*, const char*, Skee::skee_u32) override { return false; }

			std::uint32_t seen = 0;
			bool          found = false;
			float         value = 1.0F;

		private:
			std::string_view _node;
		};

		// What this session wrote, per node. A game session read every written
		// scale back as 1.000 through Has/Get while the body visibly
		// changed; until the read is sorted out, the page is not told 1.00
		// for a scale it just set.
		std::map<std::string, float> g_written;

		bool Plausible(float a_v) { return std::isfinite(a_v) && a_v > 0.0F; }

		float Read(Skee::INiTransformInterface* a_iface, RE::PlayerCharacter* a_player, const Node& a_node,
			bool a_female, std::string* a_how = nullptr)
		{
			auto*      ref = SkeeRef(a_player);
			const bool has = a_iface->HasNodeTransformScale(ref, false, a_female, a_node.node, kKey);
			const float got = a_iface->GetNodeTransformScale(ref, false, a_female, a_node.node, kKey);
			ScaleVisitor visitor(a_node.node);
			a_iface->VisitNodes(ref, false, a_female, visitor);
			const auto written = g_written.find(a_node.key);

			// What this session wrote comes first: a game session had Has true and
			// Get 1.000 on every one of 32 reads, the written value included,
			// so Get cannot be told apart from "not scaled". It is only used
			// for a scale this session has not touched.
			float       v = 1.0F;
			const char* from = "none";
			if (written != g_written.end()) {
				v = written->second;
				from = "written";
			} else if (visitor.found && Plausible(visitor.value)) {
				v = visitor.value;
				from = "visit";
			} else if (has && Plausible(got)) {
				v = got;
				from = "get";
			}
			if (a_how) {
				*a_how = fmt::format("has {} get {:.3f}, visit {}/{} scales {:.3f}, written {}, used {}", has, got,
					visitor.found, visitor.seen, visitor.value,
					written != g_written.end() ? fmt::format("{:.3f}", written->second) : std::string("-"), from);
			}
			return v;
		}

		// The same read through NiOverride's Papyrus function, logged next
		// to the C++ one. If Papyrus gives the written value back and the
		// interface does not, the fault is in how this plugin calls it.
		class PapyrusRead : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			PapyrusRead(std::string a_key, float a_written) :
				_key(std::move(a_key)), _written(a_written) {}

			void operator()(RE::BSScript::Variable a_result) override
			{
				if (a_result.IsFloat()) {
					spdlog::info("[scales] {}: NiOverride.GetNodeTransformScale (Papyrus) = {:.3f}, written {:.3f}",
						_key, a_result.GetFloat(), _written);
				} else {
					spdlog::info("[scales] {}: NiOverride.GetNodeTransformScale (Papyrus) returned no float", _key);
				}
			}

			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::string _key;
			float       _written;
		};

		void ReadByPapyrus(RE::PlayerCharacter* a_player, const Node& a_node, bool a_female, float a_written)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm) {
				return;
			}
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new PapyrusRead(a_node.key, a_written));
			auto* args = RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(a_player), false, bool(a_female),
				std::string(a_node.node), std::string(kKey));
			if (!vm->DispatchStaticCall("NiOverride", "GetNodeTransformScale", args, callback)) {
				spdlog::info("[scales] {}: DispatchStaticCall(NiOverride.GetNodeTransformScale) failed", a_node.key);
			}
		}

		void PushScales(const char* a_why)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* iface = NiTransform();
			if (!player || !iface || !player->Get3D(false)) {
				spdlog::info("[scales] {}: no player skeleton or no SKEE - sending not ready", a_why);
				Panel::Bridge::Send("scales", { { "ready", false }, { "nodes", nlohmann::json::array() } });
				return;
			}

			const bool female = IsFemale(player);
			auto       nodes = nlohmann::json::array();
			for (const auto& n : kNodes) {
				if (!Offered(player, n, female)) {
					continue;
				}
				nodes.push_back({ { "k", n.key }, { "label", n.label }, { "min", n.min }, { "max", n.max },
					{ "value", Read(iface, player, n, female) } });
			}
			spdlog::info("[scales] {}: sending {} of {} sliders (female {})", a_why, nodes.size(),
				std::size(kNodes), female);
			Panel::Bridge::Send("scales", { { "ready", true }, { "nodes", std::move(nodes) } });
		}

		void OnScalesGet(const nlohmann::json&)
		{
			PushScales("the page asked");
		}

		void OnScalesSet(const nlohmann::json& a_data)
		{
			if (!Session::MenuOpen()) {
				spdlog::warn("[scales] scales-set with the RaceSex Menu closed - ignored");
				return;
			}
			if (!a_data.is_object() || !a_data.contains("k") || !a_data["k"].is_string() || !a_data.contains("v") ||
				!a_data["v"].is_number()) {
				spdlog::warn("[scales] scales-set without a string \"k\" and a number \"v\" - ignored");
				return;
			}
			const auto  key = a_data["k"].get<std::string>();
			const auto* node = Find(key);
			if (!node) {
				spdlog::warn("[scales] scales-set for unknown slider \"{}\" - ignored", key);
				return;
			}
			const float v = a_data["v"].get<float>();
			if (!std::isfinite(v)) {
				spdlog::warn("[scales] scales-set {}: value is not finite - ignored", key);
				return;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* iface = NiTransform();
			if (!player || !iface) {
				spdlog::warn("[scales] scales-set {}: no player or no SKEE - ignored", key);
				return;
			}
			const bool female = IsFemale(player);
			if (!Offered(player, *node, female)) {
				// RaceMenu changed race or sex since the list was sent.
				spdlog::warn("[scales] scales-set {}: not offered for this player now - ignored, resending", key);
				PushScales("the slider is no longer offered");
				return;
			}

			const float scale = std::clamp(v, node->min, node->max);
			auto*       ref = SkeeRef(player);
			for (const bool firstPerson : { false, true }) {
				if (scale != 1.0F) {
					iface->AddNodeTransformScale(ref, firstPerson, female, node->node, kKey, scale);
				} else {
					iface->RemoveNodeTransformScale(ref, firstPerson, female, node->node, kKey);
				}
			}
			for (const bool firstPerson : { false, true }) {
				iface->UpdateNodeTransforms(ref, firstPerson, female, node->node);
			}
			++g_sets;
			if (scale != 1.0F) {
				g_written[node->key] = scale;
			} else {
				g_written.erase(node->key);
			}

			const bool final = a_data.contains("final") && a_data["final"].is_boolean() && a_data["final"].get<bool>();
			if (final) {
				std::string how;
				const float back = Read(iface, player, *node, female, &how);
				spdlog::info("[scales] {} ({}) = {:.2f}{} (drag ended); read back {:.3f} ({})", key, node->node,
					scale, scale == 1.0F ? ", key removed" : "", back, how);
				ReadByPapyrus(player, *node, female, scale);
			}
		}

		void OnShown() { PushScales("the panel came on screen"); }

		void OnOpened()
		{
			g_sets = 0;
			g_written.clear();
		}

		void OnClosed() { spdlog::info("[scales] RaceSex Menu closed: {} values written", g_sets); }

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
		Panel::Bridge::On("scales-get", &OnScalesGet);
		Panel::Bridge::On("scales-set", &OnScalesSet);
		g_installed = true;
		spdlog::info("[scales] installed");
	}
}
