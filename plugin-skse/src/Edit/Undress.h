// Undress.h
// Lifecast - toggle to see the body while editing, Fase 6.6 E5.
//
// SO DESEQUIPAR (fixed by the author, "Despido ate vestir de novo"): every
// worn item this feature takes off is unequipped in place and never leaves
// the actor's inventory - never through a container, never recreated. The
// same InventoryEntryData/ExtraDataList instance goes back on, so
// enchantment and charge survive by construction, not by anything this code
// restores. There is no co-save: a forced close of the game while undressed
// leaves the character undressed, not robbed, and that is the accepted
// worst case (Docs/PARA-PLANEJAMENTO.md, E5).
//
// Captured on undress: for every worn TESBoundObject, the exact
// ExtraDataList* that RE::ExtraDataList::GetWorn() finds true among that
// object's InventoryEntryData::extraLists (an object can have more than one
// stack; only the worn one is ever touched). Re-equip on dress looks the
// kept ExtraDataList* up among the object's lists at that moment, by
// address only, and hands it back only if it is still there. A list that
// carried nothing but the worn flag is freed by the engine as the item comes
// off - every time, measured - and handing that pointer to EquipObject read
// freed memory and crashed the game on some cycles. Such an item goes back
// with no list (the engine picks its stack); a list with data on it (temper,
// enchantment) survives the unequip and goes back as the same instance.
//
// Mirrors Light's close-time symmetry: whatever this feature changed is put
// back before the menu closes, same OnMenuClosed hook. A forced close
// bypasses that hook - which is the one case this feature does not cover,
// by design - and cannot leave a save with the character undressed, which is
// why "what if the game is force-closed undressed" was cancelled as a test
// (EMENDA de 2026-09-29, _Steward\Projetos\Lifecast\Fase-6.6-o-controle-de-personagem.md):
// there is no path that reaches that state to begin with.
//
// The button label is FIXED, not a state readout (author's decision, same
// EMENDA, after finding it in the E5 round): opening the RaceSex Menu always
// pushes "on: false" to the page, whatever the character is actually
// wearing - closing while undressed already re-dresses (above), so a label
// that instead reported real state would drift the moment it did. Fixed +
// pushed-on-open is what makes "always says Undress on open" true instead of
// just usually true: there is deliberately no dressed/undressed detection
// here to keep in sync.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "undress-get"  {}
//   dll -> page  "undress"      { "on": bool }
//   page -> dll  "undress-set"  { "on": bool }

#pragma once

namespace Lifecast::Edit::Undress
{
	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();
}
