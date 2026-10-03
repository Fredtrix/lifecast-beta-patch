// Content.cpp
// Lifecast

#include "Preset/Content.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>

namespace Lifecast::Preset::Content
{
	namespace
	{
		using json = nlohmann::json;

		constexpr std::size_t kLabels = 3;       // labels kept per side and section
		constexpr double      kTolerance = 1e-4;  // relative, on numbers

		// How the entries of a section are labelled.
		enum class Identity
		{
			kFields,        // every scalar field but the value, nested lists adding a level
			kHeadPart,      // formIdentifier + type; formId left out
			kTint,          // painted layers only: index + type + texture; color is the value
			kFaceTexture,   // index + texture, no value
		};

		struct Section
		{
			const char* name;
			const char* parent;  // nullptr: top level
			const char* key;
			Identity    identity;
		};

		// The overwrite guard's six first, then the three the panel edits,
		// in the order the page lists them.
		constexpr std::array<Section, 9> kSections{ {
			{ "sculpt", "morphs", "sculpt", Identity::kFields },
			{ "custom", "morphs", "custom", Identity::kFields },
			{ "bodyMorphs", nullptr, "bodyMorphs", Identity::kFields },
			{ "transforms", nullptr, "transforms", Identity::kFields },
			{ "overrides", nullptr, "overrides", Identity::kFields },
			{ "skinOverrides", nullptr, "skinOverrides", Identity::kFields },
			{ "headParts", nullptr, "headParts", Identity::kHeadPart },
			{ "tintInfo", nullptr, "tintInfo", Identity::kTint },
			{ "faceTextures", nullptr, "faceTextures", Identity::kFaceTexture },
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

		// A texture path as a label: the same file written with the other
		// slash or another case is the same texture.
		std::string Path(const json& a_value)
		{
			auto text = a_value.is_string() ? a_value.get<std::string>() : std::string();
			for (auto& c : text) {
				c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return text;
		}

		std::string Field(const json& a_element, const char* a_key)
		{
			const auto it = a_element.find(a_key);
			return it == a_element.end() ? std::string() : Scalar(*it);
		}

		// A tint layer is painted when the alpha of its ARGB color is not 0.
		// A color that is missing or not a number counts as painted: the
		// comparison would rather report a layer than hide one.
		bool Visible(const json& a_layer)
		{
			const auto it = a_layer.find("color");
			if (it == a_layer.end() || !it->is_number_integer()) {
				return true;
			}
			const auto argb = static_cast<std::uint32_t>(it->get<std::int64_t>());
			return (argb >> 24) != 0;
		}

		// The three sections whose generic label would get identity wrong:
		// formId in head parts changes without anything having changed; tint
		// layers carry a slot for every layer, painted or not; and face
		// textures, which have no value, match paths regardless of case and
		// slash.
		void FlattenOwn(const json& a_list, Identity a_identity, Entries& a_out)
		{
			if (!a_list.is_array()) {
				return;
			}
			static const json kNoValue = nullptr;
			for (const auto& element : a_list) {
				if (!element.is_object()) {
					a_out[Scalar(element)].push_back(&kNoValue);
					continue;
				}
				switch (a_identity) {
				case Identity::kHeadPart:
					a_out["type " + Field(element, "type") + ", " + Field(element, "formIdentifier")].push_back(&kNoValue);
					break;
				case Identity::kTint:
					{
						// Only painted layers count. The file keeps a slot for
						// every layer the race offers, and one the player never
						// painted has alpha 0 and whatever mask the slot had -
						// counting those reports losses nobody can see.
						if (!Visible(element)) {
							break;
						}
						std::string label = "index " + Field(element, "index");
						if (element.contains("type")) {
							label += ", type " + Field(element, "type");
						}
						label += ", " + Path(element.contains("texture") ? element["texture"] : json());
						const auto color = element.find("color");
						a_out[label].push_back(color == element.end() ? &kNoValue : &*color);
						break;
					}
				case Identity::kFaceTexture:
					a_out["index " + Field(element, "index") + ", " +
						  Path(element.contains("texture") ? element["texture"] : json())]
						.push_back(&kNoValue);
					break;
				case Identity::kFields:
					break;
				}
			}
		}

		void Collect(const json& a_list, Identity a_identity, Entries& a_out)
		{
			if (a_identity == Identity::kFields) {
				Flatten(a_list, {}, a_out);
			} else {
				FlattenOwn(a_list, a_identity, a_out);
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
		//
		// a_written holds, for this label, the values the panel itself wrote
		// in this menu session (see Compare in Content.h). A surplus entry of
		// the after side that has one of them - by value, with the same
		// tolerance as everything else here - is counted as chosen instead of
		// gained, and that written value is used up.
		void Match(const std::string& a_label, std::vector<const json*> a_before, std::vector<const json*> a_after,
			SectionDiff& a_diff, std::vector<const json*> a_written = {})
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
				const auto mine = std::find_if(a_written.begin(), a_written.end(),
					[&](const json* w) { return Same(*w, *a_after[i]); });
				if (mine != a_written.end()) {
					a_written.erase(mine);
					++a_diff.chosen;
					Note(a_diff.chosenLabels, a_label);
					continue;
				}
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
				{ "chosen", s.chosen },
				{ "chosenLabels", s.chosenLabels },
				{ "lostLabels", s.lostLabels },
			});
		}
		return out;
	}

	Diff Compare(const json& a_before, const json& a_after, const json& a_written)
	{
		Diff diff;
		for (const auto& section : kSections) {
			Entries before;
			Entries after;
			Entries written;
			if (const auto* v = Find(a_before, section)) {
				Collect(*v, section.identity, before);
			}
			if (const auto* v = Find(a_after, section)) {
				Collect(*v, section.identity, after);
			}
			if (const auto* v = Find(a_written, section)) {
				Collect(*v, section.identity, written);
			}

			SectionDiff d;
			d.section = section.name;
			for (auto& [label, values] : after) {
				const auto it = before.find(label);
				const auto mine = written.find(label);
				Match(label, it == before.end() ? std::vector<const json*>{} : std::move(it->second), values, d,
					mine == written.end() ? std::vector<const json*>{} : mine->second);
				if (it != before.end()) {
					before.erase(it);
				}
			}
			for (auto& [label, values] : before) {
				Match(label, std::move(values), {}, d);
			}
			if (d.gained || d.lost || d.changed || d.chosen) {
				diff.sections.push_back(std::move(d));
			}
		}
		return diff;
	}
}
