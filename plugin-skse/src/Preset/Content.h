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
//       tints          painted layers only - alpha of the ARGB color not
//                      0 - labelled index + type (when present) + mask;
//                      color is the value. The file keeps a slot for every
//                      layer the race offers, painted or not, and the
//                      unpainted ones change mask between saves of the
//                      same face. Being painted is part of the identity:
//                      painting and wiping a layer are a gain and a loss,
//                      and a new mask on a painted layer is both; a new
//                      color is an edit. The cost: a layer painted and
//                      then set to alpha 0 on purpose counts as absent.
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
		std::size_t              chosen{ 0 };   // gains the panel itself wrote - see Compare
		std::vector<std::string> chosenLabels;
	};

	struct Diff
	{
		std::vector<SectionDiff> sections;  // only sections with a gain, a loss or a change

		// True when no entry is gained or lost. Changed values alone leave
		// it true: changing a value is editing, not a leak. Chosen entries
		// leave it true for the same reason: the player made them here.
		bool NothingGainedOrLost() const;

		// [{ section, gained, lost, changed, gainedLabels, lostLabels,
		//    chosen, chosenLabels }]
		nlohmann::json ToJson() const;
	};

	// What a file with a_after would gain and lose against a_before, section
	// by section. Either may be a non-object (unreadable file); it then
	// counts as having no entries.
	//
	// a_written is what the panel itself wrote in this menu session, in the
	// same .jslot shape (today only "bodyMorphs", from Edit/Body.h). An entry
	// a_after has and a_before does not is a GAIN - unless a_written has the
	// same entry with the same value (same tolerance as every number here),
	// and then it is CHOSEN: the player picked it in the panel, and the
	// preview says so instead of calling it a surprise. Provenance of the
	// WRITE, not an exemption of the section: a gain a_written does not
	// cover is still a gain, even in the section and the save where the
	// player chose a body. Only gains are reclassified; a loss is always a
	// loss. Default: nothing written, every gain is a gain.
	Diff Compare(const nlohmann::json& a_before, const nlohmann::json& a_after,
		const nlohmann::json& a_written = nlohmann::json());
}
