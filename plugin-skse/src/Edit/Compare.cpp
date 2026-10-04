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
		// her leaves the old reference standing in its cell, culled, with its
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
			return mine != 0 && CellOf(actor) == mine;
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
		void Dress(const char* a_why)
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

			Park("just born");
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
					g_bornInMenu ? " - and she was born with the menu already OPEN, which boots 78 and 79 measured as "
								   "not drawing"
								 : "");
				return false;
			}
			root->SetAppCulled(false);
			spdlog::info("[compare] show: SetAppCulled(false) on {:08X} - GetAppCulled reads back {} | 3D {} | at {}",
				actor->GetFormID(), root->GetAppCulled(), actor->Is3DLoaded() ? "LOADED" : "not loaded",
				P(actor->GetPosition()));
			return true;
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
				Dress("the comparison was turned on");
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
					Park("the body finally loaded");
				}
			}

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
			if (!TwinHere()) {
				// The measured birth has not happened in this room. Try the
				// other one anyway - born with the menu already open - because
				// the alternative is a first opening with no comparison at all,
				// and the log then says which of the two cases this is.
				spdlog::warn("[compare] menu opened with no second figure in this room - one is made NOW, with the "
							 "menu already OPEN, which boots 78 and 79 measured as not drawing");
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
