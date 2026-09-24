// Library.h
// Atelier
//
// The presets on disk: RaceMenu's preset folder, listed for the panel.
//
// The folder is Data\SKSE\Plugins\CharGen\Presets\, the one RaceMenu itself
// saves to and loads from, so the list shows the same presets RaceMenu
// shows. Only *.jslot is listed; RaceMenu's older binary .slot format is not.
//
// The list is cheap on purpose: a directory walk, no file opened. Search and
// sort happen in the page, over the list it already has - nothing crosses
// the bridge per key typed. Whether each file actually reads as a preset is
// checked afterwards, off the game thread, and arrives as a second message:
// a bad file is marked in the list, it never costs the list.
//
// Messages (see Panel/Bridge.h for the envelope; the DLL also sends the list
// by itself when the panel comes on screen and after a save):
//
//   page -> dll  "presets-get"      {}
//   dll -> page  "presets"          { "folder": text, "ms": n,
//                                     "items": [ { "name", "time", "size" } ] }
//                                     time = last write, Unix seconds
//   dll -> page  "presets-checked"  { "checked": n, "ms": n,
//                                     "bad": [ { "name", "why" } ] }

#pragma once

#include <filesystem>

namespace Atelier::Preset::Library
{
	// Relative to the game's working directory, which is the game folder.
	// Under Mod Organizer the virtual file system merges every mod's copy
	// of this folder, and new files land in its overwrite.
	inline constexpr auto kFolder = "Data\\SKSE\\Plugins\\CharGen\\Presets";
	inline constexpr auto kExtension = ".jslot";

	std::filesystem::path Folder();

	// Walks the folder and sends "presets", then starts the check that
	// sends "presets-checked". Game thread. Also runs every time the panel
	// comes on screen: RaceMenu may have saved a preset meanwhile.
	void SendList();

	// Registers the bridge handlers. Call once, after kDataLoaded.
	void Install();
}
