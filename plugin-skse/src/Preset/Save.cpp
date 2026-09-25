// Save.cpp
// Atelier

#include "Preset/Save.h"

#include "Panel/Bridge.h"
#include "Preset/FileName.h"
#include "Preset/Guard.h"
#include "Preset/Jslot.h"
#include "Preset/Library.h"

#include <fstream>
#include <sstream>
#include <thread>

namespace Atelier::Preset::Save
{
	namespace
	{
		using json = nlohmann::json;
		using Clock = std::chrono::steady_clock;
		namespace fs = std::filesystem;

		// RaceMenu's native, and how long it gets to answer before the save
		// counts as not available.
		constexpr auto kClass = "CharGen";
		constexpr auto kFunction = "SaveCharacterPreset";
		constexpr auto kTimeout = std::chrono::seconds(5);

		struct FileState
		{
			bool           exists{ false };
			std::uintmax_t size{ 0 };
			fs::file_time_type written{};
		};

		// One save at a time. Every field below is touched on the game
		// thread only; the VM callback and the timeout hop back to it.
		struct Pending
		{
			std::uint32_t id{ 0 };
			std::string   name;
			fs::path      path;
			FileState     before;
			json          sectionsBefore;  // of the file overwritten; empty object for a new one
			Clock::time_point sentAt{};
		};

		std::optional<Pending> g_pending;
		std::uint32_t          g_nextId = 0;
		std::size_t            g_saves = 0;

		FileState Stat(const fs::path& a_path)
		{
			FileState s;
			std::error_code ec;
			s.exists = fs::is_regular_file(a_path, ec);
			if (s.exists) {
				s.size = fs::file_size(a_path, ec);
				s.written = fs::last_write_time(a_path, ec);
			}
			return s;
		}

		void Reply(const std::string& a_name, bool a_ok, const std::string& a_why, bool a_fallback,
			std::uintmax_t a_size = 0, long long a_ms = 0, json a_loss = json())
		{
			json data = {
				{ "name", a_name },
				{ "ok", a_ok },
				{ "why", a_why },
				{ "fallback", a_fallback },
				{ "size", a_size },
				{ "ms", a_ms },
			};
			if (!a_loss.is_null()) {
				data["loss"] = std::move(a_loss);
			}
			Panel::Bridge::Send("preset-saved", data, true);
		}

		// Off the game thread, after a save that landed: does the file RaceMenu
		// wrote read with the copied parser, and which sections does it carry?
		// Log only - it answers whether our side holds the full file.
		void Inspect(fs::path a_path, std::string a_name, json a_before)
		{
			// An overwrite: which sections the file had and has now. The
			// guard allowed it because nothing it counts could be lost; this
			// is where that is seen against the file RaceMenu actually wrote.
			if (!a_before.empty()) {
				std::string text;
				const auto after = Guard::ReadAll(a_path, text) ? Guard::Sections(text) : json::object();
				json lost = json::array();
				json gained = json::array();
				json changed = json::array();
				for (const auto& [k, v] : a_before.items()) {
					if (!after.contains(k)) {
						lost.push_back(k);
					} else if (after[k] != v) {
						changed.push_back(k + " " + v.dump() + "->" + after[k].dump());
					}
				}
				for (const auto& [k, v] : after.items()) {
					if (!a_before.contains(k)) {
						gained.push_back(k);
					}
				}
				spdlog::info("[preset-save] {} overwritten: sections lost {}, gained {}, size changed {}", a_name,
					lost.dump(), gained.dump(), changed.dump());
			}
			try {
				std::ifstream in(a_path, std::ios::binary);
				std::ostringstream text;
				text << in.rdbuf();
				const auto root = Jslot::Json::parse(text.str());
				std::string keys;
				for (const auto& [k, v] : root.items()) {
					keys += (keys.empty() ? "" : ",") + k;
				}
				const auto preset = Jslot::Preset::Parse(root);
				const bool same = preset.Serialize() == root;
				const auto& m = root.contains("morphs") ? root["morphs"] : Jslot::Json();
				spdlog::info(
					"[preset-save] {} reads: sections [{}], custom {}, sculpt {}, round trip {}",
					a_name, keys,
					m.contains("custom") && m["custom"].is_array() ? std::to_string(m["custom"].size()) : "absent",
					m.contains("sculpt") && !m["sculpt"].is_null() ? "present" : "absent",
					same ? "identical" : "DIFFERENT");
			} catch (const std::exception& e) {
				spdlog::warn("[preset-save] {} does NOT read with our parser: {}", a_name, e.what());
			}
		}

		void Finish(std::uint32_t a_id, bool a_returned, const std::string& a_how)
		{
			if (!g_pending || g_pending->id != a_id) {
				spdlog::info("[preset-save] #{} {} after the save was already settled - ignored", a_id, a_how);
				return;
			}
			const Pending p = *g_pending;
			g_pending.reset();

			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - p.sentAt).count();
			const FileState after = Stat(p.path);
			const bool changed = !p.before.exists ||
			                     after.written != p.before.written || after.size != p.before.size;

			spdlog::info(
				"[preset-save] #{} {} after {} ms: file {} size {} (before: {} size {}), changed {}",
				a_id, a_how, ms, after.exists ? "exists" : "MISSING", after.size,
				p.before.exists ? "existed" : "absent", p.before.size, changed);

			if (a_returned && after.exists && after.size > 0 && changed) {
				++g_saves;
				Reply(p.name, true, {}, false, after.size, ms);
				Library::SendList();
				std::thread(Inspect, p.path, p.name, p.sectionsBefore).detach();
				return;
			}

			std::string why;
			if (!a_returned) {
				why = "RaceMenu did not answer the save";
			} else if (!after.exists) {
				why = "RaceMenu returned but no file was written";
			} else if (after.size == 0) {
				why = "RaceMenu wrote an empty file";
			} else {
				why = "RaceMenu returned but the existing file did not change";
			}
			Reply(p.name, false, why, true, after.size, ms);
		}

		class Done : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit Done(std::uint32_t a_id) :
				_id(a_id)
			{}

			void operator()(RE::BSScript::Variable a_result) override
			{
				spdlog::info("[preset-save] #{} {}.{} returned (result none: {})", _id, kClass, kFunction,
					a_result.IsNoneObject());
				if (auto* tasks = SKSE::GetTaskInterface()) {
					const auto id = _id;
					tasks->AddTask([id]() { Finish(id, true, "returned"); });
				}
			}

			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::uint32_t _id;
		};

		void OnPresetSave(const json& a_data)
		{
			const auto name = a_data.contains("name") && a_data["name"].is_string() ?
			                      a_data["name"].get<std::string>() :
			                      std::string();
			const bool overwrite = a_data.value("overwrite", false);

			if (const auto problem = FileName::Problem(name); !problem.empty()) {
				spdlog::info("[preset-save] refused \"{}\": {}", name, problem);
				Reply(name, false, problem, false);
				return;
			}
			if (g_pending) {
				Reply(name, false, "a save is still running", false);
				return;
			}

			const auto path = Library::Folder() / (name + Library::kExtension);
			const FileState before = Stat(path);
			if (before.exists && !overwrite) {
				Reply(name, false, "a preset with this name already exists", false);
				return;
			}
			json sectionsBefore = json::object();
			if (before.exists) {
				auto verdict = Guard::CheckOverwrite(name, path);
				spdlog::info("[preset-save] overwrite of {} {}: {} - file sections {}, at risk {}", name,
					verdict.allow ? "allowed" : "REFUSED", verdict.how, verdict.sections.dump(),
					verdict.loss.is_null() ? "[]" : verdict.loss.dump());
				if (!verdict.allow) {
					Reply(name, false, verdict.how, false, 0, 0,
						verdict.loss.is_null() ? json::array() : std::move(verdict.loss));
					return;
				}
				sectionsBefore = std::move(verdict.sections);
			}

			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!vm || !player) {
				spdlog::warn("[preset-save] no {} - not saved", vm ? "player" : "Papyrus VM");
				Reply(name, false, "the game is not ready", true);
				return;
			}

			const auto id = ++g_nextId;
			g_pending = Pending{ id, name, path, before, std::move(sectionsBefore), Clock::now() };

			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new Done(id));
			auto* args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(player), std::string(name));
			if (!vm->DispatchStaticCall(kClass, kFunction, args, callback)) {
				spdlog::warn("[preset-save] #{} DispatchStaticCall({}.{}) refused", id, kClass, kFunction);
				Finish(id, false, "dispatch refused");
				return;
			}
			spdlog::info("[preset-save] #{} {}.{}(player, \"{}\") dispatched, overwrite {}", id, kClass, kFunction,
				name, before.exists);

			std::thread([id]() {
				std::this_thread::sleep_for(kTimeout);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([id]() { Finish(id, false, "timed out"); });
				}
			}).detach();
		}

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Panel::Bridge::On("preset-save", &OnPresetSave);
		g_installed = true;
		spdlog::info("[preset-save] installed - delegates to {}.{}", kClass, kFunction);
	}
}
