// Load.cpp
// Atelier

#include "Preset/Load.h"

#include "Core/RaceMenuSync.h"
#include "Core/SkeeLink.h"
#include "Edit/Session.h"
#include "Panel/Bridge.h"
#include "Preset/Guard.h"
#include "Preset/Library.h"

#include <map>
#include <thread>

namespace Atelier::Preset::Load
{
	namespace
	{
		using json = nlohmann::json;
		using Clock = std::chrono::steady_clock;
		namespace fs = std::filesystem;

		constexpr auto kClass = "CharGen";
		constexpr auto kFunction = "LoadCharacterPresetEx";
		constexpr auto kTimeout = std::chrono::seconds(5);

		// One load at a time. Game thread only; the VM callback and the
		// timeout hop back to it.
		struct Pending
		{
			std::uint32_t     id{ 0 };
			std::string       name;
			std::string       bytes;  // the file as it was when the load was asked
			Clock::time_point sentAt{};
		};

		// A load given up on at the timeout. The native may still return and
		// apply the preset after that, so it is kept until it does.
		struct Abandoned
		{
			std::string       name;
			Clock::time_point sentAt{};
		};

		std::optional<Pending>                    g_pending;
		std::map<std::uint32_t, Abandoned>        g_abandoned;
		std::uint32_t                             g_nextId = 0;
		std::uint32_t                             g_loads = 0;  // returned true, this game session

		void Reply(const std::string& a_name, bool a_ok, const std::string& a_why, bool a_fallback,
			long long a_ms = 0, bool a_late = false)
		{
			Panel::Bridge::Send("preset-loaded", {
													 { "name", a_name },
													 { "ok", a_ok },
													 { "why", a_why },
													 { "fallback", a_fallback },
													 { "ms", a_ms },
													 { "late", a_late },
												 },
				true);
		}

		// The native returned for a load already given up on at the timeout.
		// No provenance is ever written here: a late load never authorizes an
		// overwrite. If no later load has started, this answer describes the
		// character, so it is re-read and the page is told. If a later load
		// has started, the screen belongs to that one: re-reading, the tint
		// request and a "loaded" to the page would all describe the wrong
		// preset, so they are skipped - but provenance is still dropped,
		// because the native that ran late may have applied over the later
		// load, and a dropped provenance can only refuse, never allow.
		void Late(std::uint32_t a_id, int a_result)
		{
			const auto it = g_abandoned.find(a_id);
			if (it == g_abandoned.end()) {
				spdlog::info("[preset-load] #{} returned after the load was already settled - ignored", a_id);
				return;
			}
			Abandoned a = std::move(it->second);
			g_abandoned.erase(it);

			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - a.sentAt).count();
			Guard::Forget("a load returned after its timeout");
			if (a_id != g_nextId) {
				spdlog::warn("[preset-load] #{} returned {} LATE and SUPERSEDED, after {} ms (timeout {} ms) - "
							 "load #{} started since; page, redraw and tints left to it",
					a_id, a_result == 1 ? "true" : "false", ms,
					std::chrono::duration_cast<std::chrono::milliseconds>(kTimeout).count(), g_nextId);
				return;
			}
			spdlog::warn("[preset-load] #{} returned {} LATE, after {} ms (timeout {} ms)", a_id,
				a_result == 1 ? "true" : "false", ms,
				std::chrono::duration_cast<std::chrono::milliseconds>(kTimeout).count());
			if (a_result == 1) {
				++g_loads;
				spdlog::info("[preset-load] #{} counted as load {} of this game session", a_id, g_loads);
				Core::RaceMenuSync::PresetLoaded();
			}
			Edit::Session::Replaced(a_result == 1 ? "preset loaded late" : "preset load failed late");
			Reply(a.name, a_result == 1,
				a_result == 1 ? std::string() : std::string("RaceMenu could not load this preset"), a_result != 1,
				ms, true);
		}

		void Finish(std::uint32_t a_id, int a_result, const std::string& a_how)
		{
			// a_result: 1 returned true, 0 returned false, -1 did not return
			if (!g_pending || g_pending->id != a_id) {
				if (a_result >= 0) {
					Late(a_id, a_result);
				}
				return;
			}
			Pending p = std::move(*g_pending);
			g_pending.reset();

			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - p.sentAt).count();
			if (a_how == "timed out") {
				g_abandoned[a_id] = Abandoned{ p.name, p.sentAt };
			}

			if (a_result == 1) {
				++g_loads;
				spdlog::info("[preset-load] #{} {} true after {} ms - load {} of this game session", a_id, a_how,
					ms, g_loads);
				Core::RaceMenuSync::PresetLoaded();
				Guard::Remember(p.name, std::move(p.bytes));
				Edit::Session::Replaced("preset loaded");
				Reply(p.name, true, {}, false, ms);
				return;
			}

			spdlog::info("[preset-load] #{} {} after {} ms", a_id, a_result == 0 ? "returned FALSE" : a_how.c_str(),
				ms);
			// A call that ran may have applied part of the file before
			// failing: the live character is no longer what the page shows.
			// A refused dispatch never ran, and changed nothing.
			if (a_how != "dispatch refused") {
				Edit::Session::Replaced("preset load failed");
			}
			Reply(p.name, false,
				a_result == 0 ? "RaceMenu could not load this preset" : "RaceMenu did not answer the load", true,
				ms);
		}

		class Done : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit Done(std::uint32_t a_id) :
				_id(a_id)
			{}

			void operator()(RE::BSScript::Variable a_result) override
			{
				const bool isBool = a_result.IsBool();
				const bool value = isBool && a_result.GetBool();
				spdlog::info("[preset-load] #{} {}.{} returned (bool {}, value {})", _id, kClass, kFunction, isBool,
					value);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					const auto id = _id;
					tasks->AddTask([id, value]() { Finish(id, value ? 1 : 0, "returned"); });
				}
			}

			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::uint32_t _id;
		};

		void OnPresetLoad(const json& a_data)
		{
			const auto name = a_data.contains("name") && a_data["name"].is_string() ?
			                      a_data["name"].get<std::string>() :
			                      std::string();
			if (name.empty()) {
				Reply(name, false, "no preset named", false);
				return;
			}
			if (g_pending) {
				Reply(name, false, "a load is still running", false);
				return;
			}

			// The name comes from the list, which is the folder's own file
			// names; it is checked against the disk, not against the naming
			// rule for new presets.
			const auto u8 = std::u8string(name.begin(), name.end());
			const auto path = Library::Folder() / fs::path(u8 + std::u8string(u8".jslot"));
			std::string bytes;
			if (!Guard::ReadAll(path, bytes)) {
				spdlog::info("[preset-load] refused \"{}\": the file cannot be opened", name);
				Reply(name, false, "the preset file cannot be opened", false);
				Library::SendList();
				return;
			}

			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!vm || !player) {
				spdlog::warn("[preset-load] no {} - not loaded", vm ? "player" : "Papyrus VM");
				Reply(name, false, "the game is not ready", true);
				return;
			}

			// Another load is about to change the live character: whatever
			// file it came from before, it is not that file any more.
			Guard::Forget("another load started");

			const auto id = ++g_nextId;
			const auto sections = Guard::Sections(bytes);
			g_pending = Pending{ id, name, std::move(bytes), Clock::now() };

			// Loading a preset does not clear the skin overrides already on
			// the character: a preset without them, or with an empty list,
			// leaves them, and one with another index adds to them. So they
			// are cleared first, and "load A" leaves the character with A's
			// skin and nothing else.
			const bool skinCleared = Core::SkeeLink::ClearSkinOverrides(player);

			auto* hairColor = Core::RaceMenuSync::HairColorForm();
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new Done(id));
			auto* args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(player), std::string(name),
				static_cast<RE::BGSColorForm*>(hairColor), std::int32_t(kFlags));
			if (!vm->DispatchStaticCall(kClass, kFunction, args, callback)) {
				spdlog::warn("[preset-load] #{} DispatchStaticCall({}.{}) refused{}", id, kClass, kFunction,
					skinCleared ? " - the skin overrides were already cleared" : "");
				if (skinCleared) {
					Edit::Session::Replaced("skin overrides cleared, then the load was refused");
				}
				Finish(id, -1, "dispatch refused");
				return;
			}
			spdlog::info("[preset-load] #{} {}.{}(player, \"{}\", hair colour {}, flags {:#x}) dispatched - file {} bytes, "
						 "sections {}, skin overrides cleared before {}",
				id, kClass, kFunction, name, hairColor ? "RaceMenu's" : "NONE", static_cast<std::uint32_t>(kFlags),
				g_pending->bytes.size(), sections.dump(), skinCleared);

			std::thread([id]() {
				std::this_thread::sleep_for(kTimeout);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([id]() { Finish(id, -1, "timed out"); });
				}
			}).detach();
		}

		bool g_installed = false;
	}

	bool Busy() { return g_pending.has_value(); }

	void Install()
	{
		if (g_installed) {
			return;
		}
		Panel::Bridge::On("preset-load", &OnPresetLoad);
		g_installed = true;
		spdlog::info("[preset-load] installed - delegates to {}.{}, flags {:#x}", kClass, kFunction, static_cast<std::uint32_t>(kFlags));
	}
}
