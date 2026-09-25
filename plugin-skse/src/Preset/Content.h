// Content.h
// Atelier
//
// What one preset file carries that another does not, compared by CONTENT.
//
// Counting the entries of a section on each side is not enough: a section
// both files have can still gain or lose entries inside it (two skin
// overrides against one, for instance), and a count per section reads that
// as "nothing changed". So each section is taken apart into entries, and the
// entries are matched one by one.
//
// How an entry is matched, and the three fields that change without anything
// having changed:
//
//   - an entry is identified by its fields other than its value ("data" or
//     "value"); a nested list of objects adds a level to the identity. A
//     skin override is (slotMask, firstPerson) > (key, index, type), a body
//     morph is (name) > (key), an extended slider is (name).
//   - values compare as values, with a tolerance: the file writes 0 and 0.0
//     for the same number.
//   - lists compare as SETS of entries, not in order.
//   - head parts and plugins are matched by formIdentifier and name, never
//     by formId or the plugin index: those carry the load order of the
//     moment the file was written, and change between two saves of the same
//     character. Neither section is compared here today; the rule is written
//     down for the day one is.
//
// Only the sections the live character is not guaranteed to hold are
// compared - the same six the overwrite guard watches. The rest (head parts,
// tints, face textures, the 19 face morphs) is what the panel shows and
// edits, and a difference there is the player's edit, not a leak.
//
// Pure logic, no engine includes.

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Atelier::Preset::Content
{
	struct SectionDiff
	{
		std::string              section;  // "skinOverrides", "custom", "sculpt"...
		std::size_t              gained{ 0 };
		std::size_t              lost{ 0 };
		std::size_t              changed{ 0 };
		std::vector<std::string> gainedLabels;  // the first few, for the page and the log
		std::vector<std::string> lostLabels;
	};

	struct Diff
	{
		std::vector<SectionDiff> sections;  // only sections with a gain, a loss or a change

		// True when no entry is gained or lost. Changed values alone leave
		// it true: changing a value is editing, not a leak.
		bool NothingGainedOrLost() const;

		// [{ section, gained, lost, changed, gainedLabels, lostLabels }]
		nlohmann::json ToJson() const;
	};

	// What a file with a_after would gain and lose against a_before, section
	// by section. Either may be a non-object (unreadable file); it then
	// counts as having no entries.
	Diff Compare(const nlohmann::json& a_before, const nlohmann::json& a_after);
}
