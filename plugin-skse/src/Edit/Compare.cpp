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

#include "Edit/Session.h"
#include "Panel/Bridge.h"
#include "Panel/Stage.h"

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

			// E19.1: the data is right, now the body. The player is the control,
			// read by the same fields in the same pass - she has a process
			// nobody doubts, so a refresh that reads differently on the figure
			// says where the figure differs.
			Refresh3D(actor, "the second figure");
			if (a_control) {
				Refresh3D(player, "the player, control");
			}
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
				Dress("the comparison was turned on", true);
				if (!Show()) {
					Push("compare-set refused");
					return;
				}
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
					spdlog::info("[compare] the body arrived {} ms after she was placed - that gap is how long a copy "
								 "of the player stood visible in the world",
						flash);
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
