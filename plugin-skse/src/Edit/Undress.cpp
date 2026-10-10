// Undress.cpp
// Lifecast

#include "Edit/Undress.h"

#include "Edit/Compare.h"
#include "Edit/Session.h"
#include "Panel/Bridge.h"

namespace Lifecast::Edit::Undress
{
	namespace
	{
		bool g_on = false;

		// Every worn (object, exact worn extraList) pair, as found the moment
		// undress ran. The extraList is only a key to look for at re-dress,
		// never a pointer to use: it may be freed by then - see Undress.h.
		struct Worn
		{
			RE::TESBoundObject* object = nullptr;
			RE::ExtraDataList*  extraList = nullptr;
		};
		std::vector<Worn> g_worn;

		// Every ExtraDataList* actually worn right now, one entry per stack
		// that GetWorn() finds true - an object can have more than one stack
		// and only the worn one is ever touched (commonlib-scout, 2026-09-29).
		std::vector<Worn> CollectWorn(RE::Actor* a_actor)
		{
			std::vector<Worn> found;
			for (auto& [object, pair] : a_actor->GetInventory()) {
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

		void PushUndress(const char* a_why)
		{
			spdlog::info("[undress] {}: on = {}", a_why, g_on);
			Panel::Bridge::Send("undress", { { "on", g_on } });
		}

		void DoUndress()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* equipManager = RE::ActorEquipManager::GetSingleton();
			if (!player || !equipManager) {
				return;
			}
			g_worn = CollectWorn(player);
			for (auto& worn : g_worn) {
				equipManager->UnequipObject(player, worn.object, worn.extraList);
			}
			spdlog::info("[undress] unequipped {} worn item(s)", g_worn.size());
		}

		void DoDress()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* equipManager = RE::ActorEquipManager::GetSingleton();
			if (!player || !equipManager) {
				g_worn.clear();
				return;
			}
			// The pointer kept at undress is never handed back as it is: a list
			// that held nothing but the worn flag is freed by the engine the
			// moment the item comes off, and EquipObject would then read freed
			// memory - see Undress.h. It is looked for among the object's lists
			// NOW, by address only (never dereferenced): found, the item goes
			// back on that same instance, temper and enchantment included; not
			// found, it goes back with no list and the engine picks the stack.
			const auto  wornNow = CollectWorn(player);
			auto        inventory = player->GetInventory();
			std::size_t sameList = 0;
			std::size_t engineList = 0;
			std::size_t gone = 0;
			for (auto& kept : g_worn) {
				const auto it = inventory.find(kept.object);
				if (it == inventory.end() || it->second.first <= 0) {
					spdlog::info("[undress] {:08X} is no longer in the inventory - not re-equipped",
						kept.object ? kept.object->GetFormID() : 0);
					++gone;
					continue;
				}
				RE::ExtraDataList* list = nullptr;
				if (auto* entry = it->second.second.get(); entry && entry->extraLists) {
					for (auto* xList : *entry->extraLists) {
						if (xList == kept.extraList) {
							list = xList;
							break;
						}
					}
				}
				const bool alreadyWorn = std::any_of(wornNow.begin(), wornNow.end(), [&](const Worn& w) {
					return w.object == kept.object && (!list || w.extraList == list);
				});
				if (alreadyWorn) {
					continue;
				}
				equipManager->EquipObject(player, kept.object, list);
				++(list ? sameList : engineList);
			}
			spdlog::info("[undress] re-equipped {} item(s): {} on the list they came off, {} on a list the engine "
						 "picks; {} no longer in the inventory",
				sameList + engineList, sameList, engineList, gone);
			g_worn.clear();
		}

		void OnUndressGet(const nlohmann::json&) { PushUndress("the page asked"); }

		void OnUndressSet(const nlohmann::json& a_data)
		{
			if (!Session::MenuOpen()) {
				spdlog::warn("[undress] undress-set with the RaceSex Menu closed - ignored");
				return;
			}
			if (!a_data.is_object() || !a_data.contains("on") || !a_data["on"].is_boolean()) {
				spdlog::warn("[undress] undress-set without a boolean \"on\" - ignored");
				return;
			}
			const bool wantOn = a_data["on"].get<bool>();
			if (wantOn == g_on) {
				PushUndress("undress-set, already there");
				return;
			}
			if (wantOn) {
				DoUndress();
			} else {
				DoDress();
			}
			g_on = wantOn;
			// The second figure wears what the player wears. Clothes are
			// CONTEXT and not what is under comparison (D106): a dressed
			// "before" beside an undressed "after" compares the clothes. Does
			// nothing when the comparison is off, which is the common case.
			Compare::MirrorWorn();
			Session::RequestRedraw(true);
			PushUndress("after undress-set");
		}

		void OnOpened()
		{
			g_on = false;
			g_worn.clear();
			// Pushed, not just reset: the page cannot be trusted to ask again
			// on its own (it did not, across a Finish/reopen, and that is the
			// defect the author found - EMENDA de 2026-09-29). The label is
			// fixed to what this call says, never to what the character is
			// actually wearing - by design, so no dressed/undressed detection
			// is needed (see Undress.h).
			PushUndress("menu opened");
		}

		void OnClosed()
		{
			if (g_on) {
				DoDress();
				spdlog::info("[undress] the RaceSex Menu closed while undressed - dressed back");
			}
			g_on = false;
		}

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Session::OnMenuOpened(&OnOpened);
		Session::OnMenuClosed(&OnClosed);
		Panel::Bridge::On("undress-get", &OnUndressGet);
		Panel::Bridge::On("undress-set", &OnUndressSet);
		g_installed = true;
		spdlog::info("[undress] installed");
	}
}
