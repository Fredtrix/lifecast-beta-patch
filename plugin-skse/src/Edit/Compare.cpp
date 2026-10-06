// Compare.cpp
// Lifecast
//
// THE WORD "twin" DOES NOT APPEAR IN A STRING LITERAL HERE, and that is not
// style. A release build of this plugin is checked by scanning the DLL for
// the byte string that only the throwaway test code ever logs, and that
// string is "twin"; a production log line carrying the word would make the
// check report test code that is not linked in. The comments are free - they
// do not ship - and the word the player-facing side uses is "the second
// figure" anyway.

#include "Edit/Compare.h"

#include "Core/RaceMenuSync.h"
#include "Core/SkeeLink.h"
#include "Edit/Session.h"
#include "Panel/Bridge.h"
#include "Panel/Stage.h"
#include "Preset/Library.h"

namespace Lifecast::Edit::Compare
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		// ----- what is kept, and for how long ------------------------------------
		//
		// One twin at a time. She is never deleted - SetDelete poisons the next
		// duplicate (V099, and it is what took boots 76 and 77) - so replacing
		// her leaves the old reference standing in its cell, disabled (E19: it
		// is disabled while the editor is closed, not only culled), with its
		// AI off. That is a declared leak, bounded by how many rooms the player
		// opens the editor in during one game session, and it is the price of
		// not calling the one function that is measured to crash.

		RE::TESNPC*         g_base = nullptr;  // the duplicate she is made of
		RE::ObjectRefHandle g_twin{};          // her reference
		RE::FormID          g_cellId = 0;      // the cell she was born in
		bool                g_bornInMenu = false;
		Clock::time_point   g_bornAt{};

		bool        g_on = false;  // the comparison, this opening
		std::string g_why;         // why the last request refused, for the page
		bool        g_arm = false;  // make her on the next tick with the menu closed
		bool        g_cull = false;  // cull her as soon as she has a body to cull
		bool        g_bornBodyless = false;  // g_cull was set when she had no 3D at all

		// E19: she is GONE while the editor is closed, and these are the state of
		// that. Culled is not gone - a culled actor is still in the world with
		// her collision, her name and her dialogue (boot 107).
		constexpr auto    kSettle = std::chrono::milliseconds(1500);  // how long her body stands before it may be disabled
		bool              g_hidden = false;     // Disable() was called on her and she has not been enabled since
		bool              g_hidePending = false;  // the editor closed; she goes as soon as her body has settled
		Clock::time_point g_settleUntil{};        // no Disable() before this, so none lands on a 3D still in flight
		bool              g_watch = false;        // log where she is a few seconds after she came back
		Clock::time_point g_watchAt{};
		RE::NiPoint3      g_watchFrom{};

		// E21.2 - the player's body morphs as they were when she was born, held
		// until her body is loaded and SKEE can apply them to it.
		std::vector<Core::SkeeLink::MorphValue> g_bodySnapshot;
		bool                                    g_bodyPending = false;

		bool g_installed = false;

		// ----- small helpers ------------------------------------------------------

		RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

		// Two views of the same reference, and the difference is not
		// cosmetic: Actor HIDES TESObjectREFR::SetPosition with a two-argument
		// override of its own (Actor.h:384, const NiPoint3&, bool). The probe
		// measured the one-argument TESObjectREFR path, so the placement calls
		// go through TwinRef and stay on the call that was measured. TwinActor
		// is for what only an Actor has - the AI switch and the equip manager.
		RE::TESObjectREFR* TwinRef()
		{
			auto ptr = g_twin.get();
			return ptr ? ptr.get() : nullptr;
		}

		RE::Actor* Twin()
		{
			auto* ref = TwinRef();
			return ref ? ref->As<RE::Actor>() : nullptr;
		}

		std::string P(const RE::NiPoint3& a_p)
		{
			return fmt::format("({:.1f}, {:.1f}, {:.1f})", a_p.x, a_p.y, a_p.z);
		}

		RE::FormID CellOf(RE::TESObjectREFR* a_ref)
		{
			auto* cell = a_ref ? a_ref->GetParentCell() : nullptr;
			return cell ? cell->GetFormID() : 0;
		}

		// True when there is a twin AND she is in the room the player is in.
		// Both halves matter: a handle can outlive its reference, and a
		// reference in another cell is one this module will not move (boot 92
		// measured that she does not draw again after a cell change).
		bool TwinHere()
		{
			auto* actor = Twin();
			if (!actor) {
				return false;
			}
			const auto mine = CellOf(Player());
			// A disabled reference may not report a parent cell, and she never
			// leaves the one she was born in (Place refuses to), so the cell she
			// was born in is the answer while she is away.
			const auto hers = g_hidden ? g_cellId : CellOf(actor);
			return mine != 0 && hers == mine;
		}

		const char* LevelName(RE::PROCESS_TYPE a_level)
		{
			switch (a_level) {
			case RE::PROCESS_TYPE::kNone:
				return "kNone";
			case RE::PROCESS_TYPE::kHigh:
				return "kHigh";
			case RE::PROCESS_TYPE::kMiddleHigh:
				return "kMiddleHigh";
			case RE::PROCESS_TYPE::kMiddleLow:
				return "kMiddleLow";
			case RE::PROCESS_TYPE::kLow:
				return "kLow";
			default:
				return "?";
			}
		}

		// Everything the E19 lines read back about one actor, in one place so
		// the figure and the player are read by the SAME fields. Each part is
		// a plain getter; nothing here writes.
		std::string Read(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return "absent";
			}
			auto* root = a_actor->Get3D(false);
			return fmt::format("level {} | AI {} | disabled {} | activation blocked {} | 3D {}{} | at {}",
				LevelName(a_actor->GetProcessLevel()), a_actor->IsAIEnabled() ? "ON" : "off",
				a_actor->IsDisabled() ? "YES" : "no", a_actor->IsActivationBlocked() ? "YES" : "no",
				a_actor->Is3DLoaded() ? "loaded" : "NOT loaded", root ? (root->GetAppCulled() ? ", culled" : ", not culled") : "",
				P(a_actor->GetPosition()));
		}

		void Push(const char* a_why)
		{
			auto* actor = Twin();
			spdlog::info("[compare] {}: on = {}{}{} | she is {}", a_why, g_on, g_why.empty() ? "" : " - why: ",
				g_why.empty() ? "" : g_why.c_str(),
				actor ? fmt::format("{:08X} at {} in cell {:08X}, 3D {}", actor->GetFormID(),
						   P(actor->GetPosition()), CellOf(actor), actor->Is3DLoaded() ? "loaded" : "NOT loaded")
					  : std::string("absent"));

			nlohmann::json out{ { "on", g_on } };
			if (!g_why.empty()) {
				out["why"] = g_why;
			}
			Panel::Bridge::Send("compare", out);
		}

		// ----- the clothes (E3b.2) ------------------------------------------------

		struct Worn
		{
			RE::TESBoundObject* object = nullptr;
			RE::ExtraDataList*  extraList = nullptr;
		};

		// Every stack GetWorn() finds true, one entry each - the same walk
		// Edit/Undress does, and for the same reason: an object can have more
		// than one stack and only the worn one is ever touched.
		std::vector<Worn> CollectWorn(RE::TESObjectREFR* a_ref)
		{
			std::vector<Worn> found;
			if (!a_ref) {
				return found;
			}
			for (auto& [object, pair] : a_ref->GetInventory()) {
				auto* entryData = pair.second.get();
				if (!entryData || !entryData->extraLists) {
					continue;
				}
				for (auto* xList : *entryData->extraLists) {
					if (xList && xList->GetWorn()) {
						found.push_back({ object, xList });
					}
				}
			}
			return found;
		}

		// E20.1 - what is worn, by NAME and SLOT. A count says how many, never
		// which: "worn 4 -> 4" cannot tell a shield on the copy that the player
		// also wears from one she should not have. Slots are written as the
		// 30-based numbers a modder reads (body 32, shield 39); a weapon has no
		// armour slot and reads "hand". Same walk as CollectWorn, so the list is
		// exactly the set that was counted.
		std::string DescribeWorn(RE::TESObjectREFR* a_ref)
		{
			std::string out;
			for (const auto& w : CollectWorn(a_ref)) {
				if (!w.object) {
					continue;
				}
				std::string slots;
				if (auto* armo = w.object->As<RE::TESObjectARMO>()) {
					const std::uint32_t mask = armo->GetSlotMask().underlying();
					for (std::uint32_t bit = 0; bit < 32; ++bit) {
						if (mask & (1u << bit)) {
							slots += (slots.empty() ? "" : ",") + std::to_string(30 + bit);
						}
					}
					if (slots.empty()) {
						slots = "none";
					}
				} else {
					slots = w.object->IsWeapon() ? "hand" : "n/a";
				}
				out += fmt::format("[{:08X} '{}' slot {}] ", w.object->GetFormID(), w.object->GetName(), slots);
			}
			return out.empty() ? std::string("nothing") : out;
		}

		// E21.1 - is the object equipped in EITHER hand or slot, by the entry and
		// not by its extra lists. CollectWorn above asks each extra list
		// GetWorn(), and ExtraDataList exposes no left-hand variant
		// (ExtraDataList.h:210), so a weapon or shield carried in the left hand
		// can be missing from it. InventoryEntryData::IsWorn(bool a_left) is the
		// header's own two-sided question (InventoryEntryData.h:52-53).
		bool EntryEquipped(const RE::InventoryEntryData* a_entry)
		{
			return a_entry && (a_entry->IsWorn(false) || a_entry->IsWorn(true));
		}

#ifdef LIFECAST_TUNING
		// E21.1 - EVERY stack an actor carries, by name, FormID, count and what
		// the game says about its equipped state; plus what her process holds in
		// each hand. Development build only: the lines are long and a release
		// build needs none of them. An empty name is resolved by form type and
		// editor ID, which is how the 24 nameless stacks of V128 get an answer.
		void DumpInventory(RE::Actor* a_actor, const char* a_who, const char* a_when)
		{
			if (!a_actor) {
				return;
			}
			auto        inv = a_actor->GetInventory();
			std::size_t n = 0;
			spdlog::info("[compare] inventory ({}, {}): {} stacks | weapon state {} | right hand {} | left hand {}", a_when,
				a_who, inv.size(), static_cast<std::uint32_t>(a_actor->GetWeaponState()),
				a_actor->GetEquippedObject(false) ? a_actor->GetEquippedObject(false)->GetName() : "-",
				a_actor->GetEquippedObject(true) ? a_actor->GetEquippedObject(true)->GetName() : "-");
			for (auto& [object, pair] : inv) {
				if (!object) {
					continue;
				}
				auto*             entry = pair.second.get();
				const char*       name = object->GetName();
				const char*       edid = object->GetFormEditorID();
				const std::string type(RE::FormTypeToString(object->GetFormType()));
				std::size_t       xWorn = 0;
				if (entry && entry->extraLists) {
					for (auto* xList : *entry->extraLists) {
						if (xList && xList->GetWorn()) {
							++xWorn;
						}
					}
				}
				spdlog::info("[compare] inventory ({}, {}) #{:02} {:08X} '{}' type {} edid '{}' count {} | IsWorn() {} "
							 "IsWorn(false) {} IsWorn(true) {} | extra lists worn {}",
					a_when, a_who, ++n, object->GetFormID(), name ? name : "", type, edid ? edid : "", pair.first,
					entry && entry->IsWorn(), entry && entry->IsWorn(false), entry && entry->IsWorn(true), xWorn);
			}
			// E21.1 - where the drawn weapons ARE in the scene graph: the
			// attachment nodes the engine hangs a sheathed weapon or a shield on,
			// and what each one holds, so the object in the frame can be named by
			// node and not only by inventory. Read-only.
			if (auto* root = a_actor->Get3D(false)) {
				for (const char* nodeName :
					{ "WeaponBack", "WeaponBow", "WeaponDagger", "WeaponSword", "WeaponAxe", "WeaponMace", "SHIELD", "QUIVER" }) {
					auto* node = root->GetObjectByName(RE::BSFixedString(nodeName));
					auto* asNode = node ? node->AsNode() : nullptr;
					std::string kids;
					std::size_t count = 0;
					if (asNode) {
						auto& children = asNode->GetChildren();
						for (auto* child = children.begin(); child != children.end(); ++child) {
							if (*child) {
								++count;
								const char* childName = (*child)->name.c_str();
								kids += fmt::format("'{}'{} ", childName ? childName : "", (*child)->GetAppCulled() ? "(culled)" : "");
							}
						}
					}
					spdlog::info("[compare] inventory ({}, {}) node {}: {} | children {} {}", a_when, a_who, nodeName,
						node ? (asNode ? "present" : "present, not a node") : "absent", count, kids);
				}
			}
			if (auto* process = a_actor->GetActorRuntimeData().currentProcess) {
				for (const auto& held : process->equippedForms) {
					spdlog::info("[compare] inventory ({}, {}) process.equippedForms: {:08X} '{}' in slot {:08X}", a_when,
						a_who, held.object ? held.object->GetFormID() : 0, held.object ? held.object->GetName() : "",
						held.slot ? held.slot->GetFormID() : 0);
				}
			} else {
				spdlog::info("[compare] inventory ({}, {}): no currentProcess - equippedForms not readable", a_when, a_who);
			}
		}

		// E21.5 - the T-pose is READ, never touched. Whether the graph manager
		// exists, what two locomotion floats say (a standing actor reads Speed
		// 0), and the idle the process last played. One line per actor, once.
		// A variable the graph does not have returns false and is logged so.
		void DumpGraph(RE::Actor* a_actor, const char* a_who)
		{
			if (!a_actor) {
				return;
			}
			RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
			const bool                                       hasManager = a_actor->GetAnimationGraphManager(manager);
			float                                            speed = 0.0f;
			float                                            direction = 0.0f;
			const bool                                       gotSpeed = a_actor->GetGraphVariableFloat(RE::BSFixedString("Speed"), speed);
			const bool gotDirection = a_actor->GetGraphVariableFloat(RE::BSFixedString("Direction"), direction);
			auto*      process = a_actor->GetActorRuntimeData().currentProcess;
			auto*      middle = process ? process->middleHigh : nullptr;
			auto*      idle = middle ? middle->lastIdlePlayed : nullptr;
			spdlog::info("[compare] graph ({}): manager {} | Speed {} ({}) | Direction {} ({}) | lastIdlePlayed {:08X} '{}' | "
						 "process {} middleHigh {}",
				a_who, hasManager && manager ? "present" : "ABSENT", speed, gotSpeed ? "read" : "NOT read", direction,
				gotDirection ? "read" : "NOT read", idle ? idle->GetFormID() : 0, idle ? idle->GetFormEditorID() : "-",
				process ? "present" : "absent", middle ? "present" : "absent");

			// E24.1 - the same read, deeper (all fields from headers read 2026-10-05): is the graph LINKED and ACTIVE,
			// what level does the process hold, which of the flags that drive the graph update are set. Nothing is written.
			const auto  level = a_actor->GetProcessLevel();
			auto*       high = process ? process->high : nullptr;
			auto*       sequencerIdle = high ? high->currentProcessIdle : nullptr;
			const auto& flags = a_actor->GetActorRuntimeData().boolFlags;
			std::size_t graphs = 0;
			int         linked = -1;
			int         active = -1;
			bool        behavior = false;
			if (hasManager && manager) {
				graphs = manager->graphs.size();
				if (graphs > 0 && manager->graphs[0]) {
					if (auto* bg = manager->graphs[0]->behaviorGraph) {
						behavior = true;
						linked = bg->isLinked ? 1 : 0;
						active = bg->isActive ? 1 : 0;
					}
				}
			}
			spdlog::info("[compare] graph+ ({}): processLevel {} | InHigh {} InMiddleHigh {} | graphs {} | behaviorGraph {} "
						 "isLinked {} isActive {} | currentProcessIdle {:08X} | flags ShouldAnimGraphUpdate {} "
						 "ForceOneAnimgraphUpdate {} ForceAnimGraphUpdate {} | AnimationDriven {}",
				a_who, static_cast<int>(level), process && process->InHighProcess(), process && process->InMiddleHighProcess(),
				graphs, behavior ? "present" : "absent/not read", linked, active, sequencerIdle ? sequencerIdle->GetFormID() : 0,
				flags.all(RE::Actor::BOOL_FLAGS::kShouldAnimGraphUpdate), flags.all(RE::Actor::BOOL_FLAGS::kForceOneAnimgraphUpdate),
				flags.all(RE::Actor::BOOL_FLAGS::kForceAnimGraphUpdate), a_actor->IsAnimationDriven());

			// E24.1 - graph variables, by name, tried as bool, int and float; a name the graph does not hold reads
			// false in all three. The names are data (strings of the vanilla behavior graph), not API; a name that
			// reads on one figure and not on the other, or differs, is the finding. Nothing is written.
			static const char* const kNames[] = { "bAnimationDriven", "bIsSynced", "iSyncIdleLocomotion", "iState",
				"IsBeastRace", "IsSneaking", "iLeftHandEquipped", "iRightHandEquipped", "iRightHandType", "iLeftHandType",
				"SpeedSampled", "TurnDelta", "Pitch", "bAllowRotation", "bHeadTrackingOn", "IsAttackReady", "IsFirstPerson",
				"iIsInSneak", "bNoStagger", "bInJumpState" };
			std::string line;
			for (const char* name : kNames) {
				const RE::BSFixedString key(name);
				bool                    b = false;
				std::int32_t            i = 0;
				float                   f = 0.0f;
				if (a_actor->GetGraphVariableBool(key, b)) {
					line += fmt::format(" {}=bool:{}", name, b);
				} else if (a_actor->GetGraphVariableInt(key, i)) {
					line += fmt::format(" {}=int:{}", name, i);
				} else if (a_actor->GetGraphVariableFloat(key, f)) {
					line += fmt::format(" {}=float:{}", name, f);
				}
			}
			spdlog::info("[compare] graph vars ({}), only the names that READ:{}", a_who, line.empty() ? " none" : line);
		}
#endif

		// E19.1 - ask the game to rebuild what an actor WEARS on her body.
		//
		// Boot 94 measured the data and not the body: she read 4 worn, the
		// player's four, and the frame 4.5 s later still showed her own helm and
		// hide. The candidate is Update3DModel, and what it needs is two things,
		// not one:
		//
		//   a process   Actor::Update3DModel does nothing, silently, without a
		//               currentProcess (Actor.cpp:1402-1408). She HAS one: the
		//               process level read kHigh seven times in boot 92 with her
		//               AI off, and this logs it again on THIS figure, which is
		//               not the probe's.
		//   a pending   AIProcess::Update3DModel acts on the flags that
		//   flag        Set3DUpdateFlag wrote into middleHigh->update3DModel, and
		//               a bare call has measured as a no-op on the PLAYER: 0.07 ms
		//               and nothing redrawn. The same call with the flag raised
		//               redraws. So the flag is raised here, kModel and kSkin -
		//               the pair the player's weight already uses (Appearance.cpp)
		//               - and the process is asked directly because the process
		//               is what is being read.
		//
		// What is read back is the pending-flag byte before the raise, after it
		// and after the call. The header places it at offset 311 right after an
		// unnamed byte, and nobody has checked that offset in a running game, so
		// the log says "unverified" beside it: if the byte never moves, that is
		// the first suspect, not the call.
		void Refresh3D(RE::Actor* a_actor, const char* a_who)
		{
			auto* process = a_actor ? a_actor->GetActorRuntimeData().currentProcess : nullptr;
			if (!process) {
				spdlog::warn("[compare] refresh ({}): NO currentProcess - Update3DModel would do nothing and is not called",
					a_who);
				return;
			}
			auto* middle = process->middleHigh;
			if (!middle) {
				spdlog::warn("[compare] refresh ({}): process present but no middleHigh - Set3DUpdateFlag is a silent "
							 "no-op without it, so nothing is asked",
					a_who);
				return;
			}

			auto*      rootBefore = a_actor->Get3D(false);
			const bool culledBefore = rootBefore && rootBefore->GetAppCulled();
			const auto flagsBefore = static_cast<unsigned>(middle->update3DModel.underlying());
			const auto wornBefore = CollectWorn(a_actor).size();
			process->Set3DUpdateFlag(static_cast<RE::RESET_3D_FLAGS>(
				static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kModel) | static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kSkin)));
			const auto flagsRaised = static_cast<unsigned>(middle->update3DModel.underlying());

			const auto t0 = Clock::now();
			process->Update3DModel(a_actor);
			const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

			const auto flagsAfter = static_cast<unsigned>(middle->update3DModel.underlying());
			auto*      rootAfter = a_actor->Get3D(false);
			spdlog::info("[compare] refresh ({}): level {} | pending-flag byte (offset 311, unverified) {:02X} -> raised {:02X} "
						 "-> after the call {:02X} | Update3DModel took {:.3f} ms | root {} | culled {} -> {} | worn {} -> {}",
				a_who, LevelName(a_actor->GetProcessLevel()), flagsBefore, flagsRaised, flagsAfter, ms,
				rootBefore == rootAfter ? "the same node" : (rootAfter ? "a NEW node" : "GONE"), culledBefore,
				rootAfter ? rootAfter->GetAppCulled() : false, wornBefore, CollectWorn(a_actor).size());
		}

		// ----- what is DRAWN on her, by the biped (E22.1) --------------------------
		//
		// The inventory says what she carries. What is attached to her 3D is
		// BipedAnim::objects[slot] (RE/B/BipedAnim.h:47): `item` is the form the
		// slot was built from and `partClone` is the node actually hanging on
		// her. A slot that has a partClone is drawn. Read for the five slots the
		// author saw on her back: shield, sword, dagger, bow, quiver.
		struct BipedSlot
		{
			RE::BIPED_OBJECT slot;
			const char*      name;
		};
		constexpr BipedSlot kBipedSlots[] = {
			{ RE::BIPED_OBJECT::kShield, "shield" },
			{ RE::BIPED_OBJECT::kOneHandSword, "one-hand sword" },
			{ RE::BIPED_OBJECT::kOneHandDagger, "one-hand dagger" },
			{ RE::BIPED_OBJECT::kBow, "bow" },
			{ RE::BIPED_OBJECT::kQuiver, "quiver" },
		};

		bool BipedDrawn(RE::Actor* a_actor, RE::BIPED_OBJECT a_slot)
		{
			auto* biped = a_actor ? a_actor->GetBiped().get() : nullptr;
			return biped && biped->objects[a_slot].partClone != nullptr;
		}

		void DumpBiped(RE::Actor* a_actor, const char* a_who, const char* a_when)
		{
			auto* biped = a_actor ? a_actor->GetBiped().get() : nullptr;
			if (!biped) {
				spdlog::info("[compare] biped ({}, {}): NO biped data (GetBiped() is null)", a_who, a_when);
				return;
			}
			for (const auto& s : kBipedSlots) {
				const auto& o = biped->objects[s.slot];
				spdlog::info("[compare] biped ({}, {}): {} - item {:08X} '{}', partClone {}", a_who, a_when, s.name,
					o.item ? o.item->GetFormID() : 0u, o.item ? o.item->GetName() : "-",
					o.partClone ? "PRESENT (drawn)" : "null");
			}
		}

		// E22.1 - the conserto: a slot drawn on her and not on the player is
		// taken off with Actor::RemoveWeapon (RE/A/Actor.h:348).
		void RemoveStrayDrawn(RE::Actor* a_actor, RE::Actor* a_player, const char* a_why)
		{
			DumpBiped(a_actor, "the second figure", "before RemoveWeapon");
			DumpBiped(a_player, "the player", "before RemoveWeapon");
			std::size_t removed = 0;
			for (const auto& s : kBipedSlots) {
				if (BipedDrawn(a_actor, s.slot) && !BipedDrawn(a_player, s.slot)) {
					spdlog::info("[compare] biped ({}): {} is drawn on her and not on the player - RemoveWeapon", a_why, s.name);
					a_actor->RemoveWeapon(s.slot);
					++removed;
				}
			}
			spdlog::info("[compare] biped ({}): RemoveWeapon called on {} slot(s)", a_why, removed);
			DumpBiped(a_actor, "the second figure", "after RemoveWeapon");
		}

		// E22.2 - hair and skin colour on the figure, with her 3D loaded
		// (RE/A/Actor.h:690 and :693). What is logged is that the calls were
		// made; whether the colour moved is the frame's to say.
		void RefreshLook(RE::Actor* a_actor, const char* a_why)
		{
			if (!a_actor || !a_actor->Is3DLoaded() || !a_actor->Get3D(false)) {
				spdlog::info("[compare] look ({}): her 3D is not loaded - UpdateHairColor/UpdateSkinColor not called", a_why);
				return;
			}
			spdlog::info("[compare] look ({}): UpdateHairColor + UpdateSkinColor on {:08X} ...", a_why, a_actor->GetFormID());
			a_actor->UpdateHairColor();
			a_actor->UpdateSkinColor();
			spdlog::info("[compare] look ({}): returned", a_why);
		}

		// D106: clothes are CONTEXT, so the twin mirrors them. A dressed
		// "before" next to an undressed "after" compares the clothes.
		//
		// It is explicit work and not a flag, because P3 measured that
		// CreateDuplicateForm does not carry what the player is WEARING: at
		// birth the twin had five worn items from her base's defaultOutfit and
		// the player had four from mods, with not one FormID in common and not
		// even the same FormID range. So her own outfit comes off and copies of
		// the player's go on.
		//
		// The player's ExtraDataList is NOT handed over with the object. It is
		// the list that carries tempering, enchantment and charge, and it
		// belongs to the player's stack; pointing a second container at it to
		// make a visual match is not a trade this module makes. The twin wears
		// a plain copy, and a tempered item reads as untempered on her.
		void Dress(const char* a_why, bool a_control = false)
		{
			auto* player = Player();
			auto* actor = Twin();
			auto* equip = RE::ActorEquipManager::GetSingleton();
			if (!player || !actor || !equip) {
				spdlog::warn("[compare] dress ({}): no player, no second figure or no equip manager - nothing done", a_why);
				return;
			}

			// The four trailing arguments are the measurement of boot 93, not a
			// style choice. Their defaults in
			// ActorEquipManager.h:19 and :22 are
			// a_queueEquip = true, a_forceEquip = false, a_playSounds = true,
			// a_applyNow = false - the change is QUEUED and not applied. Boot 93
			// called the three-argument form on this figure and measured the
			// result: her inventory grew by the four pieces (20 stacks -> 24) and
			// what she WORE did not move at all - 5 worn before, 5 worn after,
			// and the capture shows her in her own helm and hide while the player
			// stands beside her in black. Her AI is off (EnableAI(false), which is
			// what keeps her from walking away), so nothing ever drains that
			// queue. The header documents none of these four names, so this is a
			// hypothesis with a number attached and not a known fix: the readback
			// below and the frame are what decide it.
			//   queueEquip false - do not queue it, there is no one to drain it
			//   forceEquip true  - she has no AI to consent
			//   playSounds false - she is scenery, and scenery is silent
			//   applyNow   true  - the whole point
			constexpr bool kNoQueue = false;
			constexpr bool kForce = true;
			constexpr bool kSilent = false;
			constexpr bool kNow = true;

			const auto hers = CollectWorn(actor);
			spdlog::info("[compare] worn by name ({}) - the second figure BEFORE the dress: {}", a_why, DescribeWorn(actor));
#ifdef LIFECAST_TUNING
			DumpInventory(actor, "the second figure", "before the dress");
			DumpInventory(player, "the player", "before the dress");
#endif
			for (const auto& w : hers) {
				if (w.object) {
					equip->UnequipObject(actor, w.object, w.extraList, 1, nullptr, kNoQueue, kForce, kSilent, kNow);
				}
			}

			// What she already carries, read BEFORE the adds. A piece she was
			// handed on an earlier switch-on is still in her bag, and adding it
			// again is what grew her stacks 20 -> 24 in boot 93. The check costs
			// one inventory walk and lets this run on every switch-on, which is
			// what D106 asks for: equal to the player AT THE INSTANT OF
			// REPOINTING, not at the instant she was born.
			const auto  carried = actor->GetInventory();
			const auto  theirs = CollectWorn(player);
			std::size_t asked = 0;
			std::size_t added = 0;
			for (const auto& w : theirs) {
				if (!w.object) {
					continue;
				}
				if (carried.find(w.object) == carried.end()) {
					actor->AddObjectToContainer(w.object, nullptr, 1, nullptr);
					++added;
				}
				equip->EquipObject(actor, w.object, nullptr, 1, nullptr, kNoQueue, kForce, kSilent, kNow);
				++asked;
			}

			// E21.1 - whatever she holds equipped that the PLAYER does not. The
			// two loops above only touch what GetWorn() found, so an item in a
			// hand the extra lists do not describe would survive them. Asked by
			// the entry, in both hands, and compared by object against the
			// player's own equipped set. Before and after are logged: this is
			// the only conserto E21 names, and a count that does not move is a
			// result, not a defect of the caller.
			{
				std::set<RE::TESBoundObject*> playerHolds;
				for (auto& [object, pair] : player->GetInventory()) {
					if (EntryEquipped(pair.second.get())) {
						playerHolds.insert(object);
					}
				}
				std::vector<std::pair<RE::TESBoundObject*, RE::ExtraDataList*>> strays;
				for (auto& [object, pair] : actor->GetInventory()) {
					auto* entryData = pair.second.get();
					if (!object || !EntryEquipped(entryData) || playerHolds.count(object)) {
						continue;
					}
					RE::ExtraDataList* xList = nullptr;
					if (entryData->extraLists) {
						for (auto* candidate : *entryData->extraLists) {
							if (candidate && candidate->GetWorn()) {
								xList = candidate;
								break;
							}
						}
					}
					strays.emplace_back(object, xList);
				}
				spdlog::info("[compare] strays ({}): equipped on her and not on the player: {} (player holds {} equipped)",
					a_why, strays.size(), playerHolds.size());
				for (const auto& [object, xList] : strays) {
					spdlog::info("[compare] strays ({}): unequipping {:08X} '{}'", a_why, object->GetFormID(),
						object->GetName());
					equip->UnequipObject(actor, object, xList, 1, nullptr, kNoQueue, kForce, kSilent, kNow);
				}
				if (!strays.empty()) {
					std::size_t still = 0;
					for (auto& [object, pair] : actor->GetInventory()) {
						if (object && EntryEquipped(pair.second.get()) && !playerHolds.count(object)) {
							++still;
						}
					}
					spdlog::info("[compare] strays ({}): after the unequip {} of them still read equipped", a_why, still);
				}
			}

			// The readback, not the intention: what she is actually wearing
			// after all of that, against what the player wears. The stack count
			// stays in the line because it is how the accumulation above was
			// noticed in the first place - if it climbs again, the `added`
			// number beside it says whether these adds are the cause.
			auto        inv = actor->GetInventory();
			std::size_t nowWorn = 0;
			for (auto& [object, pair] : inv) {
				auto* entryData = pair.second.get();
				if (!entryData || !entryData->extraLists) {
					continue;
				}
				for (auto* xList : *entryData->extraLists) {
					if (xList && xList->GetWorn()) {
						++nowWorn;
					}
				}
			}
			spdlog::info("[compare] dress ({}): took {} off her, asked for {} of the player's {} ({} of them new to "
						 "her bag) - she now reads {} worn, {} inventory stacks",
				a_why, hers.size(), asked, theirs.size(), added, nowWorn, inv.size());

			// E20.1: the two lists side by side, same pass, same walk.
			spdlog::info("[compare] worn by name ({}) - the player: {}", a_why, DescribeWorn(player));
			spdlog::info("[compare] worn by name ({}) - the second figure: {}", a_why, DescribeWorn(actor));
#ifdef LIFECAST_TUNING
			DumpInventory(actor, "the second figure", "after the dress");
			DumpInventory(player, "the player", "after the dress");
			DumpGraph(actor, "the second figure");
			DumpGraph(player, "the player");
#endif

			// E19.1: the data is right, now the body. The player is the control,
			// read by the same fields in the same pass - she has a process
			// nobody doubts, so a refresh that reads differently on the figure
			// says where the figure differs.
			Refresh3D(actor, "the second figure");
			if (a_control) {
				Refresh3D(player, "the player, control");
			}

			// E22.1 and E22.2, after the refresh: what her 3D now shows.
			RemoveStrayDrawn(actor, player, a_why);
			RefreshLook(actor, a_why);
		}

		// ----- the face (E21.3) ---------------------------------------------------
		//
		// Written on the duplicate BEFORE PlaceObjectAtMe, so her first draw
		// already has the player's face. Every field is logged before and after.
		// A pointer member the duplicate shares with the player's base is NOT
		// written (it would write the player); it is logged as shared. Members
		// not confirmed in RE/T/TESNPC.h are not written and are listed in the
		// MEDIDO as not covered: the player's live tint masks, which live outside
		// the NPC, and the engine's own copy semantics, which no header states.

		const char* FormLabel(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return "none";
			}
			const char* name = a_form->GetName();
			return (name && *name) ? name : "(no name)";
		}

		std::uint32_t FormIdOf(const RE::TESForm* a_form)
		{
			return a_form ? a_form->GetFormID() : 0u;
		}

		void CopyFace(RE::TESNPC* a_from, RE::TESNPC* a_to)
		{
			// pointers first: shared or distinct decides what may be written
			spdlog::info("[face] pointers player/copy - headRelatedData {}/{}, faceData {}/{}, headParts {}/{}, tintLayers {}/{}",
				static_cast<const void*>(a_from->headRelatedData), static_cast<const void*>(a_to->headRelatedData),
				static_cast<const void*>(a_from->faceData), static_cast<const void*>(a_to->faceData),
				static_cast<const void*>(a_from->headParts), static_cast<const void*>(a_to->headParts),
				static_cast<const void*>(a_from->tintLayers), static_cast<const void*>(a_to->tintLayers));

			// hair colour and face texture
			if (!a_from->headRelatedData || !a_to->headRelatedData) {
				spdlog::info("[face] headRelatedData: NOT written (player {}, copy {})",
					static_cast<const void*>(a_from->headRelatedData), static_cast<const void*>(a_to->headRelatedData));
			} else if (a_from->headRelatedData == a_to->headRelatedData) {
				spdlog::info("[face] headRelatedData: SHARED with the player's base - not written (hair colour '{}' ({:08X}), face texture '{}')",
					FormLabel(a_from->headRelatedData->hairColor), FormIdOf(a_from->headRelatedData->hairColor),
					FormLabel(a_from->headRelatedData->faceDetails));
			} else {
				// The id beside the name, as the head part lines below do: a name alone
				// cannot say WHICH form this is, and 'HairColor' is both a vanilla colour
				// and the name RaceMenu gives its one mutable one.
				spdlog::info("[face] hairColor: copy '{}' ({:08X}) -> player's '{}' ({:08X})",
					FormLabel(a_to->headRelatedData->hairColor), FormIdOf(a_to->headRelatedData->hairColor),
					FormLabel(a_from->headRelatedData->hairColor), FormIdOf(a_from->headRelatedData->hairColor));
				a_to->SetHairColor(a_from->headRelatedData->hairColor);
				spdlog::info("[face] hairColor: copy now reads '{}' ({:08X})", FormLabel(a_to->headRelatedData->hairColor),
					FormIdOf(a_to->headRelatedData->hairColor));
				spdlog::info("[face] faceDetails: copy '{}' -> player's '{}'", FormLabel(a_to->headRelatedData->faceDetails),
					FormLabel(a_from->headRelatedData->faceDetails));
				a_to->SetFaceTexture(a_from->headRelatedData->faceDetails);
				spdlog::info("[face] faceDetails: copy now reads '{}'", FormLabel(a_to->headRelatedData->faceDetails));
			}

			// head parts, one per player part
			if (!a_from->headParts || !a_to->headParts) {
				spdlog::info("[face] headParts: NOT written (player {}, copy {})",
					static_cast<const void*>(a_from->headParts), static_cast<const void*>(a_to->headParts));
			} else if (a_from->headParts == a_to->headParts) {
				spdlog::info("[face] headParts: SHARED with the player's base - not written ({} parts)", a_from->numHeadParts);
			} else {
				spdlog::info("[face] headParts: player {} parts, copy {} parts", a_from->numHeadParts, a_to->numHeadParts);
				for (std::int8_t i = 0; i < a_from->numHeadParts; ++i) {
					auto* part = a_from->headParts[i];
					if (!part) {
						continue;
					}
					const auto type = part->type.get();
					auto*      had = a_to->GetCurrentHeadPartByType(type);
					// A type can hold several parts (misc does), and ChangeHeadPart
					// REPLACES the first of its type: boot 113 turned the mouth into
					// a collision part that way. A part the copy already lists is
					// left alone.
					bool listed = false;
					for (std::int8_t j = 0; j < a_to->numHeadParts; ++j) {
						listed = listed || a_to->headParts[j] == part;
					}
					if (listed) {
						spdlog::info("[face] headPart type {}: '{}' ({:08X}) already on the copy - not written",
							static_cast<std::uint32_t>(type), FormLabel(part), part->GetFormID());
						continue;
					}
					spdlog::info("[face] headPart type {}: copy '{}' ({:08X}) -> player's '{}' ({:08X})",
						static_cast<std::uint32_t>(type), FormLabel(had), had ? had->GetFormID() : 0u,
						FormLabel(part), part->GetFormID());
					if (had != part) {
						a_to->ChangeHeadPart(part);
					}
					auto* now = a_to->GetCurrentHeadPartByType(type);
					spdlog::info("[face] headPart type {}: copy now reads '{}' ({:08X})",
						static_cast<std::uint32_t>(type), FormLabel(now), now ? now->GetFormID() : 0u);
				}
			}

			// face type and sculpt values
			if (!a_from->faceData || !a_to->faceData) {
				spdlog::info("[face] faceData: NOT written (player {}, copy {})",
					static_cast<const void*>(a_from->faceData), static_cast<const void*>(a_to->faceData));
			} else if (a_from->faceData == a_to->faceData) {
				spdlog::info("[face] faceData: SHARED with the player's base - not written");
			} else {
				std::size_t partsDiffer = 0;
				for (std::size_t i = 0; i < RE::TESNPC::FaceData::Parts::kTotal; ++i) {
					spdlog::info("[face] faceData.parts[{}]: copy {} -> player's {}", i, a_to->faceData->parts[i],
						a_from->faceData->parts[i]);
					if (a_to->faceData->parts[i] != a_from->faceData->parts[i]) {
						++partsDiffer;
					}
					a_to->faceData->parts[i] = a_from->faceData->parts[i];
				}
				std::size_t morphsDiffer = 0;
				for (std::size_t i = 0; i < RE::TESNPC::FaceData::Morphs::kTotal; ++i) {
					if (a_to->faceData->morphs[i] != a_from->faceData->morphs[i]) {
						++morphsDiffer;
						spdlog::info("[face] faceData.morphs[{}]: copy {:.4f} -> player's {:.4f}", i, a_to->faceData->morphs[i],
							a_from->faceData->morphs[i]);
					}
					a_to->faceData->morphs[i] = a_from->faceData->morphs[i];
				}
				std::size_t partsAfter = 0, morphsAfter = 0;
				for (std::size_t i = 0; i < RE::TESNPC::FaceData::Parts::kTotal; ++i) {
					partsAfter += a_to->faceData->parts[i] == a_from->faceData->parts[i];
				}
				for (std::size_t i = 0; i < RE::TESNPC::FaceData::Morphs::kTotal; ++i) {
					morphsAfter += a_to->faceData->morphs[i] == a_from->faceData->morphs[i];
				}
				spdlog::info("[face] faceData: {} of {} parts and {} of {} morphs differed; after the write {} and {} equal the player's",
					partsDiffer, static_cast<int>(RE::TESNPC::FaceData::Parts::kTotal), morphsDiffer,
					static_cast<int>(RE::TESNPC::FaceData::Morphs::kTotal), partsAfter, morphsAfter);
			}

			// skin tint colour, held by value
			spdlog::info("[face] bodyTintColor: copy ({},{},{},{}) -> player's ({},{},{},{})", a_to->bodyTintColor.red,
				a_to->bodyTintColor.green, a_to->bodyTintColor.blue, a_to->bodyTintColor.alpha, a_from->bodyTintColor.red,
				a_from->bodyTintColor.green, a_from->bodyTintColor.blue, a_from->bodyTintColor.alpha);
			a_to->bodyTintColor = a_from->bodyTintColor;

			// tint layers
			if (!a_from->tintLayers || !a_to->tintLayers) {
				spdlog::info("[face] tintLayers: NOT written (player {}, copy {})",
					static_cast<const void*>(a_from->tintLayers), static_cast<const void*>(a_to->tintLayers));
			} else if (a_from->tintLayers == a_to->tintLayers) {
				spdlog::info("[face] tintLayers: SHARED with the player's base - not written ({} layers)", a_from->tintLayers->size());
			} else if (a_from->tintLayers->size() != a_to->tintLayers->size()) {
				spdlog::info("[face] tintLayers: NOT written - sizes differ (player {}, copy {})", a_from->tintLayers->size(),
					a_to->tintLayers->size());
			} else {
				for (std::uint32_t i = 0; i < a_from->tintLayers->size(); ++i) {
					auto* src = (*a_from->tintLayers)[i];
					auto* dst = (*a_to->tintLayers)[i];
					if (!src || !dst || src == dst) {
						spdlog::info("[face] tintLayers[{}]: NOT written (player {}, copy {})", i, static_cast<const void*>(src),
							static_cast<const void*>(dst));
						continue;
					}
					spdlog::info("[face] tintLayers[{}]: index {} color ({},{},{},{}) preset {} interp {} -> index {} color ({},{},{},{}) preset {} interp {}",
						i, dst->tintIndex, dst->tintColor.red, dst->tintColor.green, dst->tintColor.blue, dst->tintColor.alpha,
						dst->preset, dst->interpolationValue, src->tintIndex, src->tintColor.red, src->tintColor.green,
						src->tintColor.blue, src->tintColor.alpha, src->preset, src->interpolationValue);
					dst->tintColor = src->tintColor;
					dst->tintIndex = src->tintIndex;
					dst->preset = src->preset;
					dst->interpolationValue = src->interpolationValue;
				}
			}

			// weight and height, by value
			spdlog::info("[face] weight: copy {:.2f} -> player's {:.2f}; height: copy {:.4f} -> player's {:.4f}", a_to->weight,
				a_from->weight, a_to->height, a_from->height);
			a_to->weight = a_from->weight;
			a_to->height = a_from->height;
			spdlog::info("[face] not covered: the player's live tint masks (outside the NPC), originalRace, faceNPC, outfits, "
						 "the engine's own copy semantics");
		}

		// ----- the body (E21.2) ---------------------------------------------------
		//
		// The player's body lives in morphs held by SKEE per REFERENCE, not in
		// her base record, so CreateDuplicateForm does not carry it and the
		// figure is born with none. Written here under the same name and key the
		// player's were read with. Edit/BodyMorphs.h warns that a third party's
		// key sums when it is written from outside; that is about the PLAYER,
		// who already holds a value under it. Whether it reaches a reference
		// that holds none is what the counts below measure.

		std::size_t MorphMatches(const std::vector<Core::SkeeLink::MorphValue>& a_a,
			const std::vector<Core::SkeeLink::MorphValue>& a_b)
		{
			std::size_t same = 0;
			for (const auto& x : a_a) {
				for (const auto& y : a_b) {
					if (x.name == y.name && x.key == y.key && std::abs(x.value - y.value) < 0.0005f) {
						++same;
						break;
					}
				}
			}
			return same;
		}

		// Reads the player's table and writes it onto her reference. Needs only the
		// reference, not the body; applying waits for the body (ApplyBody).
		void CopyBody(RE::Actor* a_figure)
		{
			auto* player = Player();
			if (!player || !a_figure) {
				return;
			}
			std::vector<Core::SkeeLink::MorphValue> mine;
			std::vector<Core::SkeeLink::MorphValue> hers;
			std::vector<Core::SkeeLink::MorphValue> after;
			std::size_t                             written = 0;
			const bool                              readMine = Core::SkeeLink::ReadBodyMorphs(player, mine);
			const bool                              readHers = Core::SkeeLink::ReadBodyMorphs(a_figure, hers);
			const bool wrote = readMine && !mine.empty() && Core::SkeeLink::WriteBodyMorphs(a_figure, mine, written);
			const bool readAfter = Core::SkeeLink::ReadBodyMorphs(a_figure, after);
			spdlog::info("[compare] body: the player holds {} morph values (read {}), the second figure held {} before "
						 "(read {}), {} SetMorph calls (wrote {}), she reads {} after (read {}) of which {} equal the "
						 "player's by name, key and value",
				mine.size(), readMine, hers.size(), readHers, written, wrote, after.size(), readAfter,
				MorphMatches(mine, after));
			g_bodySnapshot = std::move(mine);
			g_bodyPending = wrote;
		}

#ifdef LIFECAST_TUNING
		// E22.3 - the RaceMenu's own route: SavePreset on the player, LoadPreset on
		// the figure (kPresetApplyAll). Two gates. (1) The Preset interface must
		// answer; it answered null once, and then this logs and stops. (2) The
		// header warns that LoadPreset may write the actor's TESNPC, so it is
		// called ONLY when the figure's base is provably not the player's: pointer
		// and FormID of both are logged first, and equal means no call.
		void PresetRoute(const char* a_why)
		{
			auto* player = Player();
			auto* actor = Twin();
			if (!player || !actor || !actor->Get3D(false)) {
				spdlog::info("[preset] ({}): no player, or her 3D is not there - not called", a_why);
				return;
			}
			if (!Core::SkeeLink::PresetReady()) {
				spdlog::info("[preset] ({}): the Preset interface is NOT reachable - the item stops here", a_why);
				return;
			}
			auto*      mine = player->GetActorBase();
			auto*      hers = actor->GetActorBase();
			const bool distinct = mine && hers && mine != hers && mine->GetFormID() != hers->GetFormID();
			spdlog::info("[preset] ({}): player base {} {:08X}, her base {} {:08X} - {}", a_why, static_cast<const void*>(mine),
				mine ? mine->GetFormID() : 0u, static_cast<const void*>(hers), hers ? hers->GetFormID() : 0u,
				distinct ? "distinct" : "NOT distinct, LoadPreset is NOT called");
			if (!distinct) {
				return;
			}
			const char* file = "SKSE\\Plugins\\CharGen\\Exported\\lifecast-e22.jslot";
			const char* tint = "Textures\\CharGen\\Exported\\lifecast-e22.dds";
			const bool  saved = Core::SkeeLink::SavePreset(player, file, tint);
			spdlog::info("[preset] ({}): SavePreset on the player returned {}", a_why, saved);
			if (!saved) {
				return;
			}
			const bool loaded = Core::SkeeLink::LoadPreset(actor, file, tint);
			spdlog::info("[preset] ({}): LoadPreset on the figure returned {}", a_why, loaded);
		}
#endif

#ifdef LIFECAST_TUNING
		// E23 - the face by the RaceMenu's own Papyrus natives, the route the Preset
		// panel already uses (Preset\Save.cpp, Preset\Load.cpp): CharGen.
		// SaveCharacterPreset(player, file), and when it answers,
		// CharGen.LoadCharacterPresetEx(figure, file, hairColor, -1). The sculpt
		// and the extended face sliders live in SKEE keyed by the TESNPC pointer
		// and are not reachable any other way (the "Preset" C++ interface answers
		// null). LoadCharacterPresetEx writes the figure's TESNPC, so it is called
		// ONLY when that base is provably not the player's. The file is the panel's
		// own preview slot (Library::kPreviewName), so no preset appears in the list.
		void FaceLoaded(const char* a_why);

		class FaceLoadDone : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			void operator()(RE::BSScript::Variable a_result) override
			{
				const bool value = a_result.IsBool() && a_result.GetBool();
				spdlog::info("[face-preset] LoadCharacterPresetEx returned (bool {}, value {})", a_result.IsBool(), value);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([]() { FaceLoaded("the preset load returned"); });
				}
			}
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
		};

		void FaceLoaded(const char* a_why)
		{
			RefreshLook(Twin(), a_why);
		}

		void FaceLoadStart()
		{
			auto* player = Player();
			auto* actor = Twin();
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!player || !actor || !vm || !actor->Get3D(false)) {
				spdlog::info("[face-preset] no player, figure 3D or VM - the load is not dispatched");
				return;
			}
			auto*      mine = player->GetActorBase();
			auto*      hers = actor->GetActorBase();
			const bool distinct = mine && hers && mine != hers && mine->GetFormID() != hers->GetFormID();
			spdlog::info("[face-preset] player base {} {:08X}, her base {} {:08X} - {}", static_cast<const void*>(mine),
				mine ? mine->GetFormID() : 0u, static_cast<const void*>(hers), hers ? hers->GetFormID() : 0u,
				distinct ? "distinct" : "NOT distinct, the load is NOT dispatched");
			if (!distinct) {
				return;
			}
			auto*                                                    hair = Core::RaceMenuSync::HairColorForm();
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new FaceLoadDone());
			auto* args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(actor), std::string(Preset::Library::kPreviewName),
				static_cast<RE::BGSColorForm*>(hair), std::int32_t(-1));
			const bool sent = vm->DispatchStaticCall("CharGen", "LoadCharacterPresetEx", args, callback);
			spdlog::info("[face-preset] CharGen.LoadCharacterPresetEx(figure {:08X}, \"{}\", hair {}, -1) dispatched: {}",
				actor->GetFormID(), Preset::Library::kPreviewName, hair ? "RaceMenu's" : "NONE", sent);
		}

		class FaceSaveDone : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			void operator()(RE::BSScript::Variable) override
			{
				spdlog::info("[face-preset] SaveCharacterPreset returned");
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([]() { FaceLoadStart(); });
				}
			}
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
		};

		void FaceByPreset(const char* a_why)
		{
			auto* player = Player();
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!player || !vm) {
				spdlog::info("[face-preset] ({}): no player or VM", a_why);
				return;
			}
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new FaceSaveDone());
			auto* args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(player), std::string(Preset::Library::kPreviewName));
			const bool sent = vm->DispatchStaticCall("CharGen", "SaveCharacterPreset", args, callback);
			spdlog::info("[face-preset] ({}): CharGen.SaveCharacterPreset(player, \"{}\") dispatched: {}", a_why,
				Preset::Library::kPreviewName, sent);
		}
#endif

		// Once her body is loaded: ApplyBodyMorphs then UpdateModelWeight. The
		// readback of the call says it was made; only the frame says the body
		// changed (V127).
		void ApplyBody(const char* a_why)
		{
			if (!g_bodyPending) {
				return;
			}
			auto* actor = Twin();
			if (!actor || !actor->Get3D(false)) {
				return;  // not yet - still pending
			}
			g_bodyPending = false;
			spdlog::info("[compare] body ({}): ApplyBodyMorphs + UpdateModelWeight(immediate) on {:08X} ...", a_why,
				actor->GetFormID());
			// E23: the body's SHAPE also lives in node transforms (the panel's body
			// scales and any other mod's), held by SKEE per reference. They go over
			// first, under the same node and key, so the apply below sees them.
			if (auto* player = Player(); player && player->GetActorBase()) {
				const bool                           female = player->GetActorBase()->GetSex() == RE::SEX::kFemale;
				Core::SkeeLink::NodeCopyCounts read, wrote;
				const bool                           copied = Core::SkeeLink::CopyNodeTransforms(player, actor, female, read, wrote);
				spdlog::info("[compare] nodes ({}): player holds {} scales, {} positions, {} rotations, {} scale modes (female {}) - "
							 "copied {}; written {} / {} / {} / {}",
					a_why, read.scales, read.positions, read.rotations, read.modes, female, copied, wrote.scales, wrote.positions,
					wrote.rotations, wrote.modes);
			}
			const bool done = Core::SkeeLink::ApplyBodyMorphs(actor);
			spdlog::info("[compare] body ({}): returned, called {}", a_why, done);
		}

		// ----- born, parked, fetched, shown ---------------------------------------

		// Culls her root node, which is what makes the comparison cost nothing
		// while it is off: a culled node is not drawn and its subgraph is not
		// walked, so animation, skinning and SMP all stop there (D104).
		void Park(const char* a_why)
		{
			auto* actor = Twin();
			if (!actor) {
				g_cull = false;
				return;
			}
			auto* root = actor->Get3D(false);
			if (!root) {
				// Her body is not there yet - she was placed this frame. The
				// tick culls her on the first one that has a body, and the gap
				// between the two is logged, because that gap is exactly how
				// long a copy of the player is visible standing in the world.
				g_cull = true;
				g_bornBodyless = true;
				spdlog::info("[compare] park ({}): no 3D yet - she is culled on the first tick that has one", a_why);
				return;
			}
			root->SetAppCulled(true);
			g_cull = false;
			spdlog::info("[compare] park ({}): SetAppCulled(true) on {:08X} - GetAppCulled reads back {}", a_why,
				actor->GetFormID(), root->GetAppCulled());
		}

		bool Make(const char* a_when)
		{
			auto* player = Player();
			if (!player) {
				g_why = "there is no player yet";
				return false;
			}
			auto* cell = player->GetParentCell();
			if (!cell) {
				g_why = "the player is not in a room yet";
				return false;
			}
			if (!player->Is3DLoaded()) {
				g_why = "the player's own body is not loaded yet";
				return false;
			}
			auto* base = player->GetActorBase();
			if (!base) {
				g_why = "the player has no actor base";
				return false;
			}

			// faceNPC is deliberately NOT pointed at the player's base. The
			// plain duplicate came back with its OWN face data and its head
			// drew (boot 80); the one time the trick ran, the game went down
			// within a second (boot 76) and the cause was never attributed. The
			// cheapest correct move is not to call it.
			//
			// Every risky call is fenced by a log line on both sides, so a CTD
			// names the call that took it rather than the session.
			spdlog::info("[compare] {}: CreateDuplicateForm(true, nullptr) on the player's base {:08X} ...", a_when,
				base->GetFormID());
			auto* dupForm = base->CreateDuplicateForm(true, nullptr);
			spdlog::info("[compare] {}: CreateDuplicateForm returned {}", a_when, dupForm ? "a form" : "NULL");
			auto* dup = dupForm ? dupForm->As<RE::TESNPC>() : nullptr;
			if (!dup) {
				g_why = "the game would not copy the player's character";
				spdlog::warn("[compare] {}: no duplicate - the comparison is not available", a_when);
				return false;
			}

			// E22.1, prevention: her own outfit and bag are the source of the
			// shield, bow and dagger on her back. Assigned on the DUPLICATE only,
			// by plain assignment (no change flag, so nothing of it is save data).
			// numContainerObjects is zeroed without freeing the array: whether
			// CreateDuplicateForm gave her a copy of it or shares the player's is
			// not stated by any header, and a free would be the wrong call in the
			// second case. The pointers are logged so the log says which it was.
			spdlog::info("[face] defaultOutfit: copy {:08X} '{}' (player {:08X}) -> none; containerObjects player {} / copy {}, "
						 "copy had {} entries -> 0",
				dup->defaultOutfit ? dup->defaultOutfit->GetFormID() : 0u, FormLabel(dup->defaultOutfit),
				base->defaultOutfit ? base->defaultOutfit->GetFormID() : 0u,
				static_cast<const void*>(base->containerObjects), static_cast<const void*>(dup->containerObjects),
				dup->numContainerObjects);
			dup->defaultOutfit = nullptr;
			dup->numContainerObjects = 0;

			// E21.3: the face, on the duplicate, before she is born.
			CopyFace(base, dup);

			// forcePersist false: nothing of her is written to the save, which
			// is the whole of what Compare.h declares about surviving a load.
			spdlog::info("[compare] {}: PlaceObjectAtMe({:08X}, persist=false) ...", a_when, dup->GetFormID());
			auto placed = player->PlaceObjectAtMe(dup, false);
			spdlog::info("[compare] {}: PlaceObjectAtMe returned {}", a_when, placed ? "a reference" : "NULL");
			if (!placed) {
				g_why = "the game would not place the copy in this room";
				return false;
			}

			g_base = dup;
			g_twin = placed->CreateRefHandle();
			g_hidden = false;
			g_hidePending = false;
			g_cellId = CellOf(placed.get());
			g_bornInMenu = Session::MenuOpen();
			g_bornAt = Clock::now();
			g_why.clear();

			// She walked 368,3 units during a 26 s wait in boot 80 and made
			// four captures measure nothing. The AI goes off before anything
			// else is asked of her.
			if (auto* actor = placed->As<RE::Actor>()) {
				actor->EnableAI(false);
				spdlog::info("[compare] {}: EnableAI(false) - IsAIEnabled reads back {}", a_when, actor->IsAIEnabled());
			}

			// E21.2: the body, by reference, while her 3D is still arriving.
			CopyBody(placed->As<RE::Actor>());

			spdlog::info("[compare] {}: DONE - base {:08X}, reference {:08X} at {} in cell {:08X}, 3D {}, born with "
						 "the menu {}",
				a_when, dup->GetFormID(), placed->GetFormID(), P(placed->GetPosition()), g_cellId,
				placed->Is3DLoaded() ? "LOADED" : "not loaded", g_bornInMenu ? "OPEN" : "closed");

			// E19, gate 1: the process level of THIS figure, at the instant of
			// birth. It is read again once her body is there (the tick, and the
			// refresh), because a process can appear after the 3D does.
			spdlog::info("[compare] {}: process readback at birth - {}", a_when, Read(Twin()));

			Park("just born");

			// Born with the menu closed, she is not wanted until the next opening.
			// Born with it open (the first-opening path), she is wanted NOW and
			// goes when the editor closes.
			if (!Session::MenuOpen()) {
				g_hidePending = true;
				g_settleUntil = Clock::now() + kSettle;
			}
			return true;
		}

		// Puts her where the framing rule is going to look for her. Same cell,
		// always: the move between cells is the one boot 92 measured as not
		// coming back, and moving her WITHIN the cell with the menu open is the
		// one boot 80 measured as keeping her in the visible set, seven of
		// seven.
		bool Place()
		{
			auto* player = Player();
			auto* actor = Twin();
			if (!player || !actor) {
				g_why = "there is no second figure";
				return false;
			}
			if (!TwinHere()) {
				g_why = "the second figure was left in another room - close the editor and open it again here";
				spdlog::warn("[compare] place: she is in cell {:08X}, the player in {:08X} - she is NOT moved between "
							 "cells (boot 92: she does not draw again)",
					CellOf(actor), CellOf(player));
				return false;
			}
			RE::NiPoint3 spot;
			if (!Panel::Stage::PairSpot(spot)) {
				g_why = "the view has not been measured yet";
				spdlog::warn("[compare] place: Stage has no kept frame - nothing to be beside");
				return false;
			}

			auto* ref = TwinRef();
			spdlog::info("[compare] place: MoveTo(player), then {} - she was at {}", P(spot), P(ref->GetPosition()));
			ref->MoveTo(player);
			ref->SetPosition(spot);
			ref->SetAngle(player->GetAngle());
			ref->Update3DPosition(true);
			spdlog::info("[compare] place: she reads {} - {:.1f} from the player, 3D {}", P(ref->GetPosition()),
				player->GetPosition().GetDistance(ref->GetPosition()), ref->Is3DLoaded() ? "LOADED" : "not loaded");
			return true;
		}

		// Uncull, and read the flag back. Nothing here claims she is ON SCREEN:
		// boot 92 measured that no field read so far separates "drawn" from
		// "present and not culled" - the same three values came back with her
		// visible and with her invisible. This reports what was called and what
		// was read, and the eye does the rest.
		bool Show()
		{
			auto* actor = Twin();
			auto* root = actor ? actor->Get3D(false) : nullptr;
			if (!root) {
				g_why = "the second figure has no body loaded in this room";
				spdlog::warn("[compare] show: no 3D to uncull{}",
					g_bornInMenu ? " - she was born with the menu already OPEN, and a figure born that way has been "
								   "seen drawing in this same cell, so the cause is still open"
								 : "");
				return false;
			}
			root->SetAppCulled(false);
			spdlog::info("[compare] show: SetAppCulled(false) on {:08X} - GetAppCulled reads back {} | 3D {} | at {}",
				actor->GetFormID(), root->GetAppCulled(), actor->Is3DLoaded() ? "LOADED" : "not loaded",
				P(actor->GetPosition()));
			return true;
		}

		// ----- gone, and back (E19) -----------------------------------------------
		//
		// The author's requirement, verbatim: after she leaves the panel she must
		// vanish - not stay in the world, not be interactable, not walk away, not
		// exist to the player's eyes. Park() only culls her node, and boot 107
		// showed what that leaves: a culled actor is still standing there, with
		// her collision, her name over the crosshair and her dialogue.
		//
		// Two calls, and each is read back:
		//   Disable()               takes the reference out of the world.
		//   SetActivationBlocked()  belt and braces for the case Disable leaves
		//                           her targetable.
		// SetDelete is NOT among them, and it is the one that is measured to
		// crash (V099). Disable() alone has never been isolated: V099 tested it
		// together with SetDelete. The first Disable() therefore waits until her
		// body has had time to finish loading (kSettle, at the top of the file),
		// because the two crashes that nobody attributed both landed on a 3D
		// still in flight.

		void Vanish(const char* a_why)
		{
			g_hidePending = false;
			auto* actor = Twin();
			if (!actor || g_hidden) {
				return;
			}
			const auto from = actor->GetPosition();
			spdlog::info("[compare] vanish ({}): before - {}", a_why, Read(actor));
			if (auto* root = actor->Get3D(false)) {
				root->SetAppCulled(true);
			}
			actor->SetActivationBlocked(true);
			actor->Disable();
			g_hidden = true;
			spdlog::info("[compare] vanish ({}): SetActivationBlocked(true) and Disable() called - right after: {} | cell {:08X}, "
						 "born in {:08X}",
				a_why, Read(actor), CellOf(actor), g_cellId);
			g_watch = true;
			g_watchAt = Clock::now() + std::chrono::milliseconds(3000);
			g_watchFrom = from;
		}

		// Called every tick while a vanish is owed. It does nothing until she has
		// a body that has been there for kSettle.
		void TryVanish()
		{
			if (!g_hidePending || Clock::now() < g_settleUntil) {
				return;
			}
			auto* actor = Twin();
			if (!actor) {
				g_hidePending = false;
				return;
			}
			if (g_cull || !actor->Get3D(false) || !actor->Is3DLoaded()) {
				return;  // the body has not arrived; the tick that culls her sets the settle time again
			}
			Vanish("the editor is closed");
		}

		void Appear(const char* a_why)
		{
			g_hidePending = false;
			auto* actor = Twin();
			if (!actor) {
				g_hidden = false;
				return;
			}
			if (!g_hidden) {
				return;
			}
			const auto from = actor->GetPosition();
			spdlog::info("[compare] return ({}): before - {}", a_why, Read(actor));
			actor->SetActivationBlocked(false);
			actor->Enable(false);
			g_hidden = false;
			// The AI bit is asserted again rather than trusted: Disable and Enable
			// are the engine's own lifecycle and nothing here has measured that
			// they leave the bit alone. She must not walk.
			actor->EnableAI(false);
			// Her body comes back not culled. The tick culls it as soon as there is
			// one, exactly as it does for a figure that was just born.
			g_cull = true;
			g_bornBodyless = false;
			g_bornAt = Clock::now();
			spdlog::info("[compare] return ({}): Enable(false), SetActivationBlocked(false), EnableAI(false) called - right "
						 "after: {}",
				a_why, Read(actor));
			g_watch = true;
			g_watchAt = Clock::now() + std::chrono::milliseconds(3000);
			g_watchFrom = from;
		}

		// ----- the bridge ---------------------------------------------------------

		double Num(const nlohmann::json& a_d, const char* a_key, double a_fallback)
		{
			return a_d.is_object() && a_d.contains(a_key) && a_d[a_key].is_number()
					 ? a_d[a_key].get<double>()
					 : a_fallback;
		}

		void OnCompareGet(const nlohmann::json&) { Push("the page asked"); }

#ifdef LIFECAST_TUNING
		// Development build only (E20.2): the dress and the refresh WITH the comparison
		// already on and her body on screen, so a frame can sit on each side of it.
		// E24.1: the graph read, on demand, for both figures. Read-only.
		void OnCompareGraph(const nlohmann::json&)
		{
			DumpGraph(Twin(), "the second figure (asked)");
			DumpGraph(RE::PlayerCharacter::GetSingleton(), "the player (asked)");
		}

		void OnCompareDress(const nlohmann::json&)
		{
			if (!g_on) {
				spdlog::warn("[compare] compare-dress while the comparison is off - ignored");
				return;
			}
			Dress("asked by the page (development build)", true);
		}
#endif

		void OnCompareSet(const nlohmann::json& a_data)
		{
			if (!Session::MenuOpen()) {
				spdlog::warn("[compare] compare-set with the RaceSex Menu closed - ignored");
				return;
			}
			if (!a_data.is_object() || !a_data.contains("on") || !a_data["on"].is_boolean()) {
				spdlog::warn("[compare] compare-set without a boolean \"on\" - ignored");
				return;
			}
			const bool wantOn = a_data["on"].get<bool>();

			// The character view's own edges, which only the page knows: the
			// panel covers the left of the screen, so the middle of the window
			// is not the middle of what the player can see. Missing, the rule
			// frames against the whole window and says so.
			double left = 0.0;
			double right = 1.0;
			if (a_data.contains("frame") && a_data["frame"].is_object()) {
				left = Num(a_data["frame"], "left", 0.0);
				right = Num(a_data["frame"], "right", 1.0);
			} else {
				spdlog::warn("[compare] compare-set with no \"frame\" - the rule will centre the pair on the whole "
							 "window, panel included");
			}

			g_why.clear();
			if (wantOn == g_on) {
				Push("compare-set, already there");
				return;
			}

			if (wantOn) {
				// Every failure below leaves the panel whole: g_why says what
				// happened, g_on stays false, and nothing else in the panel
				// depends on this module.
				if (!Place()) {
					Push("compare-set refused");
					return;
				}
				// Before the uncull, so she is never seen in her own default
				// outfit: identical at the instant of repointing (D106). Every
				// switch-on, not once per figure: "the instant of repointing" is
				// THIS instant, and boot 93 ran it once and skipped it on the
				// second switch-on of the same opening, which would leave her in
				// last switch-on's clothes if the player changed in between.
				bool dressHer = true;
#ifdef LIFECAST_TUNING
				// Development build only, like Panel/Stage's "stage-tune": a frame of
				// her BEFORE the dress (E20.2). A release build has no such field.
				if (a_data.contains("dress") && a_data["dress"].is_boolean()) {
					dressHer = a_data["dress"].get<bool>();
				}
				if (!dressHer) {
					spdlog::info("[compare] compare-set: dress SKIPPED on request (development build) - she is seen in "
								 "her own outfit until \"compare-dress\" arrives");
				}
#endif
				if (dressHer) {
					Dress("the comparison was turned on", true);
				}
				if (!Show()) {
					Push("compare-set refused");
					return;
				}
				// E21.2: boot 111 applied the morphs on the tick her body arrived,
				// while she was still culled, and she was not drawn afterwards. The
				// apply moves here, to the moment she is shown.
				ApplyBody("the comparison was turned on");
				RefreshLook(Twin(), "the comparison was turned on");
#ifdef LIFECAST_TUNING
				PresetRoute("the comparison was turned on");
				// FaceByPreset("the comparison was turned on");  // boot 119: the game died inside CharGen.LoadCharacterPresetEx on the copy (see Docs/VERIFY.md V134); boot 129: with the crash fix it survives but a modal from the FSMP config loader goes in front of the panel (V140)
				RefreshLook(Twin(), "after the preset route");
#endif
				Panel::Stage::FramePair(true, left, right);
				g_on = true;
			} else {
				Park("the comparison was turned off");
				Panel::Stage::FramePair(false, left, right);
				g_on = false;
			}
			Push("after compare-set");
		}

		// ----- the session --------------------------------------------------------

		// Where the twin is BORN. A figure born with the menu OPEN drew (confirmed
		// in a captured frame) in cell 00016A06 - the very cell where boots 78
		// and 79 had measured that one born inside the menu does not. Those two
		// boots measured a probe that no longer exists, with
		// code from four days before this module was written, so that
		// measurement does not transfer here. What decides whether she draws is
		// therefore OPEN: it is not the cell and it is not being born with the
		// menu open, and the path of the birth is the part nobody has isolated.
		//
		// So the birth is armed when the editor CLOSES, and it happens on the
		// next input tick, in the room the player is standing in, which is the
		// room the next opening will be in. The cost of the tick when it is not
		// armed is two bool reads.
		void OnTick()
		{
			if (g_cull) {
				auto* actor = Twin();
				if (actor && actor->Get3D(false)) {
					const auto flash =
						std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_bornAt).count();
					if (g_bornBodyless) {
						// Placed with no 3D: nothing of her existed to draw during the gap, so
						// it is how long she existed WITHOUT a body, not how long she showed.
						spdlog::info("[compare] the body arrived {} ms after she was placed - she had no 3D until then, "
									 "so nothing of her was drawn during the gap",
							flash);
					} else {
						// Brought back by Enable with a body already in the world: whether she
						// drew during the gap is NOT measured by this line.
						spdlog::info("[compare] the culling tick came {} ms after she was brought back - she had a body, so "
									 "this is the longest she can have stood visible (not measured)",
							flash);
					}
					spdlog::info("[compare] the body arrived - {}", Read(actor));
					Park("the body finally loaded");
					g_settleUntil = Clock::now() + kSettle;
				}
			}

			if (g_watch && Clock::now() >= g_watchAt) {
				g_watch = false;
				if (auto* actor = Twin()) {
					spdlog::info("[compare] watch, 3 s later: {} | moved {:.2f} units from where she was", Read(actor),
						actor->GetPosition().GetDistance(g_watchFrom));
				}
			}
			TryVanish();

			if (!g_arm || Session::MenuOpen()) {
				return;
			}

			auto* player = Player();
			if (!player || !player->Is3DLoaded() || CellOf(player) == 0) {
				return;  // not ready - still armed, try again next tick
			}
			g_arm = false;  // one attempt per arming, and it is not retried on failure

			// The figure already in this room stays, however she was born: being
			// born with the menu open is not what decides whether she draws.
			if (TwinHere()) {
				return;
			}
			Make("born in the world, with the menu closed");
		}

		void OnOpened()
		{
			g_on = false;
			g_why.clear();
			// She is back before anything else is asked of her, so the page's
			// first Compare click finds a body and not a figure still gone.
			if (TwinHere()) {
				Appear("the menu opened");
			}
			if (!TwinHere()) {
				// The measured birth has not happened in this room. Try the
				// other one anyway - born with the menu already open - because
				// the alternative is a first opening with no comparison at all,
				// and the log then says which of the two cases this is.
				spdlog::warn("[compare] menu opened with no second figure in this room - one is made NOW, with the "
							 "menu already OPEN - a birth path this module has never measured");
				Make("born with the menu already open");
			}
			// Pushed rather than only reset: the page does not reliably ask
			// again across a Finish and reopen, which is the defect the author
			// found in Undress.
			Push("menu opened");
		}

		void OnClosed()
		{
			if (g_on) {
				Park("the RaceSex Menu closed");
			}
			g_on = false;
			g_why.clear();
			g_arm = true;
			// E19: she goes when the editor does. TryVanish waits for her body.
			g_hidePending = true;
			g_settleUntil = Clock::now() + kSettle;
		}
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Session::OnMenuOpened(&OnOpened);
		Session::OnMenuClosed(&OnClosed);
		Session::OnTick(&OnTick);
		Panel::Bridge::On("compare-get", &OnCompareGet);
		Panel::Bridge::On("compare-set", &OnCompareSet);
#ifdef LIFECAST_TUNING
		Panel::Bridge::On("compare-dress", &OnCompareDress);
		Panel::Bridge::On("compare-graph", &OnCompareGraph);
#endif
		g_installed = true;
		spdlog::info("[compare] installed");
	}

	void MirrorWorn()
	{
		if (!g_on) {
			return;
		}
		Dress("the player's clothes changed under us");
	}
}
