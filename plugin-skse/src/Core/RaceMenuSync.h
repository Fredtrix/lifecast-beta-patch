// RaceMenuSync.h
// Atelier - telling RaceMenu's own script what the panel changed.
//
// RaceMenu keeps a copy of the hair colour and of the tint masks in its
// script, refreshes its sliders from that copy, and writes the copy back on
// load. A change made behind its back is shown by the game but not by
// RaceMenu, and does not survive a reload. RaceMenu listens for mod events
// that make it take a fresh copy; these functions send them.
//
// The events are RaceMenu's public ones (registered in racemenu.psc), sent
// through SKSE's mod callback source like SendModEvent does from Papyrus.

#pragma once

namespace Atelier::Core::RaceMenuSync
{
	// RaceMenu's own hair colour form, RaceMenuHairColor (000801:RaceMenu.esp).
	// Null when RaceMenu.esp is not loaded.
	RE::BGSColorForm* HairColorForm();

	// "RSM_HairColorChange": RaceMenu sets its hair colour form to a_color,
	// gives it to the player and keeps it as a custom colour.
	void HairColorChanged(const RE::Color& a_color);

	// "RSM_SliderChange" with "ChangeMaskColor": RaceMenu re-reads every tint
	// mask and refreshes its tint controls.
	void TintsChanged();

	// "RSM_SliderChange" with "ChangeHairColorPreset": RaceMenu re-reads the
	// hair colour and refreshes its controls.
	void HairPresetChanged();

	// "RSM_ShadersInvalidated": what RaceMenu's script sends after
	// Game.UpdateTintMaskColors(), so its native side re-applies what it
	// layers over the tint (overlays, paints).
	void ShadersInvalidated();

	// "RSM_RequestTintSave": RaceMenu takes a fresh copy of the hair colour
	// and the tint masks. RaceMenu's own CharGen.LoadPreset sends it after a
	// preset load, "some internals were probably modified".
	void PresetLoaded();

	// "RSM_ToggleLight" with numArg 1.0/0.0: RaceMenu's own chargen light
	// toggle, the same event its "L" hotkey sends.
	void ToggleLight(bool a_on);
}
