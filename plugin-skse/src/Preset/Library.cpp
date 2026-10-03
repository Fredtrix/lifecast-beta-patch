// Library.cpp
// Lifecast

#include "Preset/Library.h"

#include "Edit/Session.h"
#include "Panel/Bridge.h"
#include "Preset/FileName.h"
#include "Preset/Jslot.h"

#include <atomic>
#include <fstream>
#include <sstream>
#include <thread>

namespace Lifecast::Preset::Library
{
	namespace
	{
		using json = nlohmann::json;
		namespace fs = std::filesystem;

		struct Entry
		{
			std::string name;  // file name without the extension, UTF-8
			fs::path    path;
			std::int64_t time{ 0 };
			std::uintmax_t size{ 0 };
		};

		// A newer list makes the check of an older one pointless; its result
		// is dropped instead of marking rows that may no longer be there.
		std::atomic<std::uint32_t> g_generation{ 0 };

		bool IsPreset(const fs::path& a_path)
		{
			auto ext = a_path.extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return ext == kExtension;
		}

		std::string Utf8(const fs::path& a_path)
		{
			const auto u8 = a_path.u8string();
			return std::string(u8.begin(), u8.end());
		}

		std::vector<Entry> Walk(std::string& a_error)
		{
			std::vector<Entry> out;
			std::error_code ec;
			const auto folder = Folder();
			if (!fs::exists(folder, ec)) {
				a_error = "folder does not exist";
				return out;
			}
			for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
				std::error_code fec;
				if (!it->is_regular_file(fec) || !IsPreset(it->path())) {
					continue;
				}
				Entry e;
				e.path = it->path();
				e.name = Utf8(it->path().stem());
				if (FileName::Same(e.name, kPreviewName)) {
					continue;
				}
				e.size = it->file_size(fec);
				const auto written = it->last_write_time(fec);
				if (!fec) {
					const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(written);
					e.time = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
				}
				out.push_back(std::move(e));
			}
			if (ec) {
				a_error = ec.message();
			}
			return out;
		}

		std::string Check(const fs::path& a_path)
		{
			try {
				std::ifstream in(a_path, std::ios::binary);
				if (!in) {
					return "cannot be opened";
				}
				std::ostringstream text;
				text << in.rdbuf();
				(void)Jslot::Preset::Parse(Jslot::Json::parse(text.str()));
				return {};
			} catch (const std::exception& e) {
				return e.what();
			}
		}

		// Runs on its own thread: reads and parses every file, then hands
		// the result back to the game thread, which is where Bridge::Send
		// belongs.
		void CheckAll(std::vector<Entry> a_entries, std::uint32_t a_generation)
		{
			const auto start = std::chrono::steady_clock::now();
			json bad = json::array();
			for (const auto& e : a_entries) {
				if (g_generation.load() != a_generation) {
					return;
				}
				if (auto why = Check(e.path); !why.empty()) {
					bad.push_back({ { "name", e.name }, { "why", std::move(why) } });
				}
			}
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - start).count();

			spdlog::info("[presets] checked {} files in {} ms: {} unreadable", a_entries.size(), ms, bad.size());
			for (const auto& b : bad) {
				spdlog::info("[presets]   unreadable: {} - {}", b["name"].get<std::string>(), b["why"].get<std::string>());
			}

			json data = { { "checked", a_entries.size() }, { "ms", ms }, { "bad", std::move(bad) } };
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([data = std::move(data), a_generation]() {
					if (g_generation.load() == a_generation) {
						Panel::Bridge::Send("presets-checked", data);
					}
				});
			}
		}

		void OnPresetsGet(const json&) { SendList(); }

		bool g_installed = false;
	}

	void SendList()
	{
		const auto start = std::chrono::steady_clock::now();
		std::string error;
		auto entries = Walk(error);
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - start).count();

		std::error_code ec;
		const auto absolute = fs::absolute(Folder(), ec);
		spdlog::info("[presets] listed {} presets in {} ms from {}{}", entries.size(), ms, Utf8(absolute),
			error.empty() ? "" : " (" + error + ")");

		json items = json::array();
		for (const auto& e : entries) {
			items.push_back({ { "name", e.name }, { "time", e.time }, { "size", e.size } });
		}
		Panel::Bridge::Send("presets", { { "folder", kFolder }, { "ms", ms }, { "items", std::move(items) } });

		const auto generation = ++g_generation;
		std::thread(CheckAll, std::move(entries), generation).detach();
	}

	std::filesystem::path Folder()
	{
		return std::filesystem::path(kFolder);
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Panel::Bridge::On("presets-get", &OnPresetsGet);
		Edit::Session::OnPanelShown(&SendList);
		g_installed = true;
		spdlog::info("[presets] installed - folder {}", kFolder);
	}
}
