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
// How an entry is matched, and the fields that change without anything
// having changed:
//
//   - an entry is identified by its fields other than its value ("data" or
//     "value"); a nested list of objects adds a level to the identity. A
//     skin override is (slotMask, firstPerson) > (key, index, type), a body
//     morph is (name) > (key), an extended slider is (name).
//   - values compare as values, with a tolerance: the file writes 0 and 0.0
//     for the same number.
//   - lists compare as SETS of entries, not in order.
//   - three sections have labels of their own, because the generic one
//     would carry a field that changes on every save:
//       head parts     formIdentifier + type. Never formId: it carries the
//                      load order of the moment the file was written.
//       tints          index + type (when present); color AND mask are the
//                      value, so either one changing is a change, not a
//                      gain and a loss. The mask at an index follows the
//                      race's tint list in the load order of the save:
//                      two saves of the same character in two load orders
//                      put different masks at the same index, invisible
//                      ones included.
//       face textures  index + texture, matched regardless of case and
//                      slash direction; nothing is a value.
//
// Nine sections are compared: the six the overwrite guard watches, and the
// head parts, tints and face textures. The guard keeps its six on purpose -
// it decides to refuse, this only describes - but a file that loses its
// hair or its tint layers must say so, and those three are where a load of
// content the game does not have loses the most.
//
// "changed" never warns, and one case of it looks like a hole: a skin
// override replaced at the SAME index is a change, not a gain and a loss.
// That is right because the dangerous form of it - the file's skin having
// come from content this session never loaded - is refused before: the
// overwrite guard lets a file with skin overrides be written over only when
// this session loaded that file whole. Read the guard before "fixing" it
// here.
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
