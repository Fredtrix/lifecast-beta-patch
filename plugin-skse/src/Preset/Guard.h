// Guard.h
// Atelier
//
// Whether saving over an existing preset would destroy something.
//
// Saving is RaceMenu writing the character as it is NOW over the file. What
// the file has and the live character does not is lost; what the live
// character carries from before (body morphs, transforms, sculpt of another
// preset) goes into the file. Neither is visible until the preset is loaded
// again.
//
// So before an overwrite the existing file is read (read, never rewritten)
// and the overwrite is allowed only when one of these holds:
//
//   provenance  - the last whole-preset load of this RaceSex Menu session was
//                 that same file, and its content has not changed since.
//                 Compared byte for byte: a false "unchanged" would let the
//                 overwrite through, so identity is the content itself, not
//                 its size or date. Provenance ends when the menu closes, or
//                 when another load starts - the live character can change in
//                 ways this module cannot see.
//   nothing     - the file has none of the sections the live character is not
//     at risk     guaranteed to hold (extended sliders, sculpt, body morphs,
//                 transforms, overrides, skin overrides).
//
// Otherwise the overwrite is refused, and the reason lists what would be lost,
// section by section, so the page can show it.

#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

namespace Atelier::Preset::Guard
{
	struct Verdict
	{
		bool           allow{ false };
		std::string    how;       // why it is allowed, or why it is refused
		nlohmann::json loss;      // refused: [{ "section", "count" }]
		nlohmann::json sections;  // what the existing file carries, for the log
	};

	// Reads the file at a_path and decides. a_name is the preset name as the
	// page sent it.
	Verdict CheckOverwrite(const std::string& a_name, const std::filesystem::path& a_path);

	// Non-empty sections of a preset file and their sizes, as
	// { "section": count }. Empty object when the text does not parse.
	nlohmann::json Sections(const std::string& a_text);

	// A whole-preset load of a_name succeeded, and a_bytes is the file it
	// read. Replaces any earlier provenance.
	void Remember(const std::string& a_name, std::string a_bytes);

	// Provenance ends; a_why goes to the log.
	void Forget(const char* a_why);

	// Reads a whole file; false when it cannot be opened.
	bool ReadAll(const std::filesystem::path& a_path, std::string& a_out);

	// Registers the menu-closed hook. Call once, after kDataLoaded.
	void Install();
}
