// Load.cpp
// Lifecast

#include "Preset/Load.h"

#include "Core/RaceMenuSync.h"
#include "Core/SkeeLink.h"
#include "Edit/Session.h"
#include "Panel/Bridge.h"
#include "Preset/Guard.h"
#include "Preset/Library.h"

#include <atomic>
#include <thread>

namespace Lifecast::Preset::Load
{
	namespace
	{
		using json = nlohmann::json;
		using Clock = std::chrono::steady_clock;
		namespace fs = std::filesystem;

		constexpr auto kClass = "CharGen";
		constexpr auto kFunction = "LoadCharacterPresetEx";
		// The call is never given up on: the native may return at any time and apply the
		// preset, and a page that said "failed" would then be lying. These two only
		// change what the page is told while it waits.
		constexpr auto kApplying = std::chrono::seconds(5);
		constexpr auto kSilent = std::chrono::seconds(120);

		// One load at a time. Game thread only; the VM callback and the
		// timeout hop back to it.
		struct Pending
		{
			std::uint32_t     id{ 0 };
			std::string       name;
			std::string       bytes;  // the file as it was when the load was asked
			Clock::time_point sentAt{};
			bool              silent{ false };  // the long warning went out: a new load may replace this one
		};

		std::optional<Pending>                    g_pending;
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

		// The call has been out for a while and has not returned. It is still
		// alive, so this is not a failure and the busy flag stays: the page is
		// told it is applying, and later that RaceMenu has not answered yet.
		void Waiting(std::uint32_t a_id, bool a_silent)
		{
			if (!g_pending || g_pending->id != a_id) {
				return;
			}
			g_pending->silent = a_silent;
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_pending->sentAt).count();
			spdlog::info("[preset-load] #{} still running after {} ms - the page is told \"{}\", not \"failed\"", a_id, ms,
				a_silent ? "no answer yet" : "applying");
			Panel::Bridge::Send("preset-loading", {
													   { "name", g_pending->name },
													   { "ms", ms },
													   { "silent", a_silent },
												   },
				true);
		}

		void Finish(std::uint32_t a_id, int a_result, const std::string& a_how)
		{
			// a_result: 1 returned true, 0 returned false, -1 never ran (dispatch refused)
			if (!g_pending || g_pending->id != a_id) {
				spdlog::info("[preset-load] #{} answered after the load was already settled - ignored", a_id);
				return;
			}
			Pending p = std::move(*g_pending);
			g_pending.reset();

			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - p.sentAt).count();
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
			Reply(p.name, false, a_result == 0 ? "RaceMenu could not load this preset" : "the load was refused", true, ms);
		}

#ifdef LIFECAST_TUNING
		// Development build only: the next load answers this many ms late, as if the
		// Papyrus queue had been stalled. The seam is the return, so everything after
		// it is the production path. "e32-sim-delay" { "ms": n } arms it once.
		std::atomic<int> g_simDelayMs{ 0 };
#endif

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
				const auto id = _id;
#ifdef LIFECAST_TUNING
				if (const int sim = g_simDelayMs.exchange(0); sim > 0) {
					spdlog::warn("[preset-load] #{} SIMULATED delay of {} ms before the answer is handled "
								 "(development build)",
						id, sim);
					std::thread([id, value, sim]() {
						std::this_thread::sleep_for(std::chrono::milliseconds(sim));
						if (auto* tasks = SKSE::GetTaskInterface()) {
							tasks->AddTask([id, value]() { Finish(id, value ? 1 : 0, "returned"); });
						}
					}).detach();
					return;
				}
#endif
				if (auto* tasks = SKSE::GetTaskInterface()) {
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
				if (!g_pending->silent) {
					spdlog::info("[preset-load] refused \"{}\": #{} is still running ({} ms)", name, g_pending->id,
						std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_pending->sentAt).count());
					Reply(name, false, "a load is still running", false);
					return;
				}
				// The old call has been out for kSilent without an answer and the page was told so. A new load
				// replaces it as the one being waited for; the old call is not cancelled (it cannot be), and if it
				// ever returns, Finish() finds another id and only logs it - the page and the state stay as the
				// new load leaves them.
				spdlog::info("[preset-load] #{} had no answer after {} s - a new load is accepted, its late answer "
							 "will only be logged",
					g_pending->id, std::chrono::duration_cast<std::chrono::seconds>(kSilent).count());
				g_pending.reset();
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
				std::this_thread::sleep_for(kApplying);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([id]() { Waiting(id, false); });
				}
				std::this_thread::sleep_for(kSilent - kApplying);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([id]() { Waiting(id, true); });
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
#ifdef LIFECAST_TUNING
		Panel::Bridge::On("e32-sim-delay", [](const json& a_data) {
			g_simDelayMs = a_data.value("ms", 0);
			spdlog::warn("[preset-load] next load will answer {} ms late (development build)", g_simDelayMs.load());
		});
#endif
		g_installed = true;
		spdlog::info("[preset-load] installed - delegates to {}.{}, flags {:#x}", kClass, kFunction, static_cast<std::uint32_t>(kFlags));
	}
}
