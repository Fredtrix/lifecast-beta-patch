// Guard.cpp
// Atelier

#include "Preset/Guard.h"

#include "Edit/Session.h"
#include "Preset/FileName.h"

#include <array>
#include <fstream>
#include <optional>
#include <sstream>

namespace Atelier::Preset::Guard
{
	namespace
	{
		using json = nlohmann::json;

		struct Provenance
		{
			std::string name;
			std::string bytes;
		};

		// Game thread only.
		std::optional<Provenance> g_provenance;

		std::size_t Count(const json& a_value)
		{
			if (a_value.is_array() || a_value.is_object()) {
				return a_value.size();
			}
			return a_value.is_null() ? 0 : 1;
		}

		// The sections the live character is not guaranteed to hold, in the
		// order the page lists them.
		struct AtRisk
		{
			const char* section;
			const char* parent;  // nullptr: top level
			const char* key;
		};

		constexpr std::array<AtRisk, 6> kAtRisk{ {
			{ "sculpt", "morphs", "sculpt" },
			{ "custom", "morphs", "custom" },
			{ "bodyMorphs", nullptr, "bodyMorphs" },
			{ "transforms", nullptr, "transforms" },
			{ "overrides", nullptr, "overrides" },
			{ "skinOverrides", nullptr, "skinOverrides" },
		} };

		const json* Find(const json& a_root, const AtRisk& a_risk)
		{
			const json* base = &a_root;
			if (a_risk.parent) {
				const auto it = a_root.find(a_risk.parent);
				if (it == a_root.end() || !it->is_object()) {
					return nullptr;
				}
				base = &*it;
			}
			const auto it = base->find(a_risk.key);
			return it == base->end() ? nullptr : &*it;
		}

		void OnMenuClosed() { Forget("the RaceSex Menu closed"); }

		bool g_installed = false;
	}

	bool ReadAll(const std::filesystem::path& a_path, std::string& a_out)
	{
		std::ifstream in(a_path, std::ios::binary);
		if (!in) {
			return false;
		}
		std::ostringstream text;
		text << in.rdbuf();
		a_out = std::move(text).str();
		return true;
	}

	json Sections(const std::string& a_text)
	{
		json out = json::object();
		try {
			const auto root = json::parse(a_text);
			if (!root.is_object()) {
				return out;
			}
			for (const auto& [k, v] : root.items()) {
				if (k == "morphs" && v.is_object()) {
					for (const auto& [mk, mv] : v.items()) {
						if (Count(mv) > 0 && mk != "sculptDivisor") {
							out["morphs." + mk] = Count(mv);
						}
					}
				} else if (Count(v) > 0) {
					out[k] = Count(v);
				}
			}
		} catch (const std::exception&) {
			return json::object();
		}
		return out;
	}

	Verdict CheckOverwrite(const std::string& a_name, const std::filesystem::path& a_path)
	{
		Verdict v;
		std::string bytes;
		if (!ReadAll(a_path, bytes)) {
			v.how = "the existing preset could not be read";
			return v;
		}
		v.sections = Sections(bytes);

		if (g_provenance && FileName::Same(g_provenance->name, a_name)) {
			if (g_provenance->bytes == bytes) {
				v.allow = true;
				v.how = "loaded whole in this session, unchanged since";
				return v;
			}
			spdlog::info("[guard] {} was loaded in this session but changed on disk since", a_name);
		}

		json root;
		try {
			root = json::parse(bytes);
		} catch (const std::exception& e) {
			v.how = std::string("the existing preset does not read: ") + e.what();
			return v;
		}
		if (!root.is_object()) {
			v.how = "the existing preset is not a preset object";
			return v;
		}

		v.loss = json::array();
		for (const auto& risk : kAtRisk) {
			if (const auto* value = Find(root, risk); value && Count(*value) > 0) {
				v.loss.push_back({ { "section", risk.section }, { "count", Count(*value) } });
			}
		}
		if (v.loss.empty()) {
			v.allow = true;
			v.how = "the existing preset has nothing the live character may lack";
			return v;
		}
		v.how = "the existing preset has parts that were not loaded here";
		return v;
	}

	void Remember(const std::string& a_name, std::string a_bytes)
	{
		spdlog::info("[guard] provenance: {} ({} bytes)", a_name, a_bytes.size());
		g_provenance = Provenance{ a_name, std::move(a_bytes) };
	}

	void Forget(const char* a_why)
	{
		if (g_provenance) {
			spdlog::info("[guard] provenance of {} ended: {}", g_provenance->name, a_why);
			g_provenance.reset();
		}
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Edit::Session::OnMenuClosed(&OnMenuClosed);
		g_installed = true;
	}
}
