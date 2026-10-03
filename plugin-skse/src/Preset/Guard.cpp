// Guard.cpp
// Lifecast

#include "Preset/Guard.h"

#include "Edit/Session.h"
#include "Preset/FileName.h"

#include <array>
#include <fstream>
#include <sstream>
#include <vector>

namespace Lifecast::Preset::Guard
{
	namespace
	{
		using json = nlohmann::json;

		struct Provenance
		{
			std::string name;
			std::string bytes;
			const char* how;  // "loaded" or "written"
		};

		// The files this menu session has seen whole, one entry per name as
		// FileName::Same compares them. A load empties it; a save adds or
		// replaces one entry. No cap: a dropped entry would refuse its
		// overwrite with nothing in the log to say why. Game thread only.
		std::vector<Provenance> g_provenance;

		Provenance* Find(const std::string& a_name)
		{
			for (auto& p : g_provenance) {
				if (FileName::Same(p.name, a_name)) {
					return &p;
				}
			}
			return nullptr;
		}

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

		if (const auto* seen = Find(a_name)) {
			if (seen->bytes == bytes) {
				v.allow = true;
				v.how = std::string(seen->how) + " whole in this session, unchanged since";
				return v;
			}
			spdlog::info("[guard] {} was {} in this session but changed on disk since", a_name, seen->how);
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
		g_provenance.clear();
		spdlog::info("[guard] provenance: {} ({} bytes)", a_name, a_bytes.size());
		g_provenance.push_back(Provenance{ a_name, std::move(a_bytes), "loaded" });
	}

	void Wrote(const std::string& a_name, std::string a_bytes)
	{
		const auto size = a_bytes.size();
		if (auto* seen = Find(a_name)) {
			*seen = Provenance{ a_name, std::move(a_bytes), "written" };
		} else {
			g_provenance.push_back(Provenance{ a_name, std::move(a_bytes), "written" });
		}
		spdlog::info("[guard] provenance: {} ({} bytes, written) - {} file(s) seen whole", a_name, size,
			g_provenance.size());
	}

	void Forget(const char* a_why)
	{
		for (const auto& p : g_provenance) {
			spdlog::info("[guard] provenance of {} ended: {}", p.name, a_why);
		}
		g_provenance.clear();
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
