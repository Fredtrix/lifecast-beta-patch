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

		// The load goes through RaceMenu's own Load button: its preset editor's
		// onLoadFile, invoked on the UI thread with the event its file dialog
		// would send. Inside it the load is a synchronous call into SKEE, so it
		// waits for nothing - the Papyrus queue, which a heavy script order
		// keeps busy for tens of seconds while a new game starts, is not on the
		// way. What RaceMenu does after its own load (sliders reloaded, hair
		// colour, tint layers) it does here too, because it is its own code.
		// Player only: the editor always loads onto the player.
		constexpr auto kEditor = "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.presetEditor";
		constexpr auto kFolder = "Data\\SKSE\\Plugins\\CharGen\\Presets";

		// The way it went before, and still goes when the editor cannot be
		// reached: CharGen's Papyrus native, which waits for its turn in the
		// Papyrus queue.
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
				// RaceMenu's editor sends this one itself after its own load;
				// only the Papyrus path has to ask for it.
				if (a_how == "returned") {
					Core::RaceMenuSync::PresetLoaded();
				}
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

		// One answer, from either path, handed to the game thread.
		void Answer(std::uint32_t a_id, bool a_value, const char* a_how)
		{
#ifdef LIFECAST_TUNING
			if (const int sim = g_simDelayMs.exchange(0); sim > 0) {
				spdlog::warn("[preset-load] #{} SIMULATED delay of {} ms before the answer is handled "
							 "(development build)",
					a_id, sim);
				std::thread([a_id, a_value, a_how, sim]() {
					std::this_thread::sleep_for(std::chrono::milliseconds(sim));
					if (auto* tasks = SKSE::GetTaskInterface()) {
						tasks->AddTask([a_id, a_value, a_how]() { Finish(a_id, a_value ? 1 : 0, a_how); });
					}
				}).detach();
				return;
			}
#endif
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([a_id, a_value, a_how]() { Finish(a_id, a_value ? 1 : 0, a_how); });
			}
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
				Answer(_id, value, "returned");
			}

			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::uint32_t _id;
		};

		// The editor tells how its load went only through the event it
		// dispatches right after it, "loadedPreset" { success }. This handler is
		// that event's listener for the length of one Invoke.
		class Heard : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				heard = true;
				RE::GFxValue success;
				if (a_params.argCount > 0 && a_params.args[0].IsObject() &&
					a_params.args[0].GetMember("success", &success) && success.IsBool()) {
					ok = success.GetBool();
				}
			}

			bool heard{ false };
			bool ok{ false };
		};

		// UI thread. False: the editor is not there, nothing was asked of it.
		bool LoadThroughTheEditor(std::uint32_t a_id, const std::string& a_name)
		{
			auto*        ui = RE::UI::GetSingleton();
			auto         menu = ui ? ui->GetMenu<RE::RaceSexMenu>() : nullptr;
			auto*        movie = menu ? menu->uiMovie.get() : nullptr;
			RE::GFxValue editor;
			if (!movie || !movie->GetVariable(&editor, kEditor) || !editor.IsObject() ||
				!editor.HasMember("onLoadFile") || !editor.HasMember("addEventListener")) {
				return false;
			}

			auto         heard = RE::make_gptr<Heard>();
			RE::GFxValue fn;
			RE::GFxValue scope;
			RE::GFxValue type;
			RE::GFxValue member;
			movie->CreateFunction(&fn, heard.get());
			movie->CreateObject(&scope);
			scope.SetMember("heard", fn);
			movie->CreateString(&type, "loadedPreset");
			movie->CreateString(&member, "heard");
			const std::array<RE::GFxValue, 3> listener{ type, scope, member };

			const std::string file = a_name + ".jslot";
			RE::GFxValue      folder;
			RE::GFxValue      input;
			RE::GFxValue      ev;
			movie->CreateString(&folder, kFolder);
			movie->CreateString(&input, file.c_str());
			movie->CreateObject(&ev);
			ev.SetMember("directoryPath", folder);
			ev.SetMember("input", input);

			editor.Invoke("addEventListener", listener);
			const auto start = Clock::now();
			const bool invoked = editor.Invoke("onLoadFile", nullptr, &ev, 1);
			const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
			editor.Invoke("removeEventListener", listener);

			spdlog::info("[preset-load] #{} RaceMenu's preset editor onLoadFile(\"{}\") invoked {} in {} us - "
						 "loadedPreset {}, success {}",
				a_id, file, invoked, us, heard->heard ? "heard" : "NOT heard", heard->ok);
			Answer(a_id, heard->heard && heard->ok,
				heard->heard ? "loaded by RaceMenu's editor" : "RaceMenu's editor did not answer");
			return true;
		}

		void LoadThroughPapyrus(std::uint32_t a_id, const std::string& a_name, bool a_skinCleared);

		// What the page is told while an answer is late, whichever path the load
		// took. Waiting() does nothing once the load has been answered, which on
		// the editor's path is within the same frame.
		void WatchForALateAnswer(std::uint32_t a_id)
		{
			std::thread([a_id]() {
				std::this_thread::sleep_for(kApplying);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([a_id]() { Waiting(a_id, false); });
				}
				std::this_thread::sleep_for(kSilent - kApplying);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([a_id]() { Waiting(a_id, true); });
				}
			}).detach();
		}

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
			spdlog::info("[preset-load] #{} \"{}\" asked - file {} bytes, sections {}, skin overrides cleared before {}",
				id, name, g_pending->bytes.size(), sections.dump(), skinCleared);

			WatchForALateAnswer(id);
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) {
				LoadThroughPapyrus(id, name, skinCleared);
				return;
			}
			tasks->AddUITask([id, name, skinCleared]() {
				if (LoadThroughTheEditor(id, name)) {
					return;
				}
				spdlog::warn("[preset-load] #{} RaceMenu's preset editor cannot be reached - through CharGen's "
							 "Papyrus native instead",
					id);
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([id, name, skinCleared]() { LoadThroughPapyrus(id, name, skinCleared); });
				}
			});
		}

		// Game thread.
		void LoadThroughPapyrus(std::uint32_t a_id, const std::string& a_name, bool a_skinCleared)
		{
			if (!g_pending || g_pending->id != a_id) {
				return;
			}
			const auto id = a_id;
			auto*      vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto*      player = RE::PlayerCharacter::GetSingleton();
			auto*      hairColor = Core::RaceMenuSync::HairColorForm();
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new Done(id));
			auto* args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(player), std::string(a_name),
				static_cast<RE::BGSColorForm*>(hairColor), std::int32_t(kFlags));
			if (!vm || !player || !vm->DispatchStaticCall(kClass, kFunction, args, callback)) {
				spdlog::warn("[preset-load] #{} DispatchStaticCall({}.{}) refused{}", id, kClass, kFunction,
					a_skinCleared ? " - the skin overrides were already cleared" : "");
				if (a_skinCleared) {
					Edit::Session::Replaced("skin overrides cleared, then the load was refused");
				}
				Finish(id, -1, "dispatch refused");
				return;
			}
			spdlog::info("[preset-load] #{} {}.{}(player, \"{}\", hair colour {}, flags {:#x}) dispatched", id, kClass,
				kFunction, a_name, hairColor ? "RaceMenu's" : "NONE", static_cast<std::uint32_t>(kFlags));
		}

		bool g_installed = false;
	}

	bool Busy() { return g_pending.has_value(); }

	bool Replaceable() { return g_pending.has_value() && g_pending->silent; }

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
		spdlog::info("[preset-load] installed - delegates to RaceMenu's preset editor; {}.{} (flags {:#x}) when it "
					 "cannot be reached",
			kClass, kFunction, static_cast<std::uint32_t>(kFlags));
	}
}
