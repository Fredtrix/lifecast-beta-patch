// Content.cpp
// Atelier

#include "Preset/Content.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace Atelier::Preset::Content
{
	namespace
	{
		using json = nlohmann::json;

		constexpr std::size_t kLabels = 3;       // labels kept per side and section
		constexpr double      kTolerance = 1e-4;  // relative, on numbers

		struct Section
		{
			const char* name;
			const char* parent;  // nullptr: top level
			const char* key;
		};

		// The overwrite guard's six, in the order the page lists them.
		constexpr std::array<Section, 6> kSections{ {
			{ "sculpt", "morphs", "sculpt" },
			{ "custom", "morphs", "custom" },
			{ "bodyMorphs", nullptr, "bodyMorphs" },
			{ "transforms", nullptr, "transforms" },
			{ "overrides", nullptr, "overrides" },
			{ "skinOverrides", nullptr, "skinOverrides" },
		} };

		const json* Find(const json& a_root, const Section& a_section)
		{
			if (!a_root.is_object()) {
				return nullptr;
			}
			const json* base = &a_root;
			if (a_section.parent) {
				const auto it = a_root.find(a_section.parent);
				if (it == a_root.end() || !it->is_object()) {
					return nullptr;
				}
				base = &*it;
			}
			const auto it = base->find(a_section.key);
			return it == base->end() ? nullptr : &*it;
		}

		bool IsValueField(const std::string& a_key) { return a_key == "data" || a_key == "value"; }

		bool IsListOfObjects(const json& a_value)
		{
			return a_value.is_array() && !a_value.empty() &&
			       std::all_of(a_value.begin(), a_value.end(), [](const json& e) { return e.is_object(); });
		}

		std::string Scalar(const json& a_value)
		{
			return a_value.is_string() ? a_value.get<std::string>() : a_value.dump();
		}

		// identity label -> the values found under it (a list: an identity
		// can repeat, and the comparison is of multisets)
		using Entries = std::map<std::string, std::vector<const json*>>;

		void Flatten(const json& a_list, const std::string& a_prefix, Entries& a_out)
		{
			if (!a_list.is_array()) {
				return;
			}
			static const json kNoValue = nullptr;
			for (const auto& element : a_list) {
				if (!element.is_object()) {
					a_out[a_prefix + Scalar(element)].push_back(&kNoValue);
					continue;
				}
				// items() of an object iterates in key order: the label does
				// not depend on how the file ordered the fields.
				std::string  id;
				const json*  value = nullptr;
				std::vector<std::pair<std::string, const json*>> nested;
				for (const auto& [k, v] : element.items()) {
					if (IsValueField(k)) {
						value = &v;
					} else if (IsListOfObjects(v)) {
						nested.emplace_back(k, &v);
					} else if (!v.is_array() && !v.is_object()) {
						id += (id.empty() ? "" : ", ") + k + " " + Scalar(v);
					}
				}
				const auto label = a_prefix + id;
				if (value) {
					a_out[label].push_back(value);
				}
				for (const auto& [k, v] : nested) {
					Flatten(*v, label + " > ", a_out);
				}
				if (!value && nested.empty()) {
					a_out[label].push_back(&kNoValue);
				}
			}
		}

		bool Same(const json& a, const json& b)
		{
			if (a.is_number() && b.is_number()) {
				const double x = a.get<double>();
				const double y = b.get<double>();
				return std::fabs(x - y) <= kTolerance * (std::max)({ 1.0, std::fabs(x), std::fabs(y) });
			}
			if (a.is_array() && b.is_array()) {
				if (a.size() != b.size()) {
					return false;
				}
				for (std::size_t i = 0; i < a.size(); ++i) {
					if (!Same(a[i], b[i])) {
						return false;
					}
				}
				return true;
			}
			if (a.is_object() && b.is_object()) {
				if (a.size() != b.size()) {
					return false;
				}
				for (const auto& [k, v] : a.items()) {
					const auto it = b.find(k);
					if (it == b.end() || !Same(v, *it)) {
						return false;
					}
				}
				return true;
			}
			return a == b;
		}

		void Note(std::vector<std::string>& a_labels, const std::string& a_label)
		{
			if (a_labels.size() < kLabels) {
				a_labels.push_back(a_label);
			}
		}

		// Pairs the values of one identity. Values equal on both sides pair
		// first, so a set that only changed order reads as unchanged; what is
		// left pairs as changed, and the surplus of either side is gained or
		// lost.
		void Match(const std::string& a_label, std::vector<const json*> a_before, std::vector<const json*> a_after,
			SectionDiff& a_diff)
		{
			for (auto it = a_after.begin(); it != a_after.end();) {
				const auto hit = std::find_if(a_before.begin(), a_before.end(),
					[&](const json* b) { return Same(*b, **it); });
				if (hit != a_before.end()) {
					a_before.erase(hit);
					it = a_after.erase(it);
				} else {
					++it;
				}
			}
			const auto paired = (std::min)(a_before.size(), a_after.size());
			a_diff.changed += paired;
			for (std::size_t i = paired; i < a_after.size(); ++i) {
				++a_diff.gained;
				Note(a_diff.gainedLabels, a_label);
			}
			for (std::size_t i = paired; i < a_before.size(); ++i) {
				++a_diff.lost;
				Note(a_diff.lostLabels, a_label);
			}
		}
	}

	bool Diff::NothingGainedOrLost() const
	{
		return std::all_of(sections.begin(), sections.end(),
			[](const SectionDiff& s) { return s.gained == 0 && s.lost == 0; });
	}

	json Diff::ToJson() const
	{
		json out = json::array();
		for (const auto& s : sections) {
			out.push_back({
				{ "section", s.section },
				{ "gained", s.gained },
				{ "lost", s.lost },
				{ "changed", s.changed },
				{ "gainedLabels", s.gainedLabels },
				{ "lostLabels", s.lostLabels },
			});
		}
		return out;
	}

	Diff Compare(const json& a_before, const json& a_after)
	{
		Diff diff;
		for (const auto& section : kSections) {
			Entries before;
			Entries after;
			if (const auto* v = Find(a_before, section)) {
				Flatten(*v, {}, before);
			}
			if (const auto* v = Find(a_after, section)) {
				Flatten(*v, {}, after);
			}

			SectionDiff d;
			d.section = section.name;
			for (auto& [label, values] : after) {
				const auto it = before.find(label);
				Match(label, it == before.end() ? std::vector<const json*>{} : std::move(it->second), values, d);
				if (it != before.end()) {
					before.erase(it);
				}
			}
			for (auto& [label, values] : before) {
				Match(label, std::move(values), {}, d);
			}
			if (d.gained || d.lost || d.changed) {
				diff.sections.push_back(std::move(d));
			}
		}
		return diff;
	}
}
