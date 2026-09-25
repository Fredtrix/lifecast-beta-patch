// ObodyLink.cpp
// Atelier

#include "Core/ObodyLink.h"

#include "Core/ObodyApi.h"

namespace Atelier::Core::ObodyLink
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		namespace API = OBody::API;

		// Written by OBody, on whatever thread it answers from.
		API::IPluginInterface* g_interface = nullptr;

		// What the readiness events said last. Set from OBody's thread, read
		// from the game thread.
		std::atomic<bool> g_ready{ false };

		bool                  g_requested = false;
		Clock::time_point     g_requestedAt{};
		std::vector<Callback> g_onChanged;

		long long SinceRequest()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_requestedAt).count();
		}

		// Hops to the game thread: OBody's header does not say which thread
		// the readiness events arrive on, and every consumer of this module
		// talks to the page, which is game-thread work.
		void Notify()
		{
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) {
				spdlog::warn("[obody] no task interface - readiness change not passed on");
				return;
			}
			tasks->AddTask([]() {
				for (auto* fn : g_onChanged) {
					fn();
				}
			});
		}

		// Must live until the process ends: OBody keeps the pointer (the
		// header says so), so it is a static and never freed.
		class Listener : public API::IOBodyReadinessEventListener
		{
		public:
			void OBodyIsReady() override
			{
				g_ready = true;
				if (g_interface) {
					spdlog::info("[obody] OBodyIsReady, {} ms after the request - the body section can be offered "
								 "(plugin API version {})",
						SinceRequest(), static_cast<int>(g_interface->PluginAPIVersion()));
				} else {
					spdlog::warn("[obody] OBodyIsReady, {} ms after the request, but no interface was written", SinceRequest());
				}
				Notify();
			}

			void OBodyIsNoLongerReady() override
			{
				g_ready = false;
				spdlog::info("[obody] OBodyIsNoLongerReady, {} ms after the request - the body section is withdrawn",
					SinceRequest());
				Notify();
			}

			void OBodyIsBecomingReady() override
			{
				spdlog::info("[obody] OBodyIsBecomingReady, {} ms after the request", SinceRequest());
			}

			void OBodyIsBecomingUnready() override
			{
				spdlog::info("[obody] OBodyIsBecomingUnready, {} ms after the request", SinceRequest());
			}
		};

		Listener g_listener;
	}

	void Request()
	{
		if (g_requested) {
			return;
		}
		g_requested = true;
		g_requestedAt = Clock::now();

		// The gate that keeps the log clean - see the header. It is not the
		// answer: a null interface below is.
		if (!REX::W32::GetModuleHandleW(L"OBody.dll")) {
			spdlog::info("[obody] OBody.dll is not loaded - not asked; the body section will not be offered");
			return;
		}

		auto* messaging = SKSE::GetMessagingInterface();
		if (!messaging) {
			spdlog::warn("[obody] no messaging interface - OBody not asked");
			return;
		}

		API::SKSEMessages::RequestPluginInterface request{};
		request.version = API::PluginAPIVersion::v1;
		request.pluginInterface = &g_interface;
		request.readinessEventListener = &g_listener;

		const bool dispatched = messaging->Dispatch(API::SKSEMessages::RequestPluginInterface::type, &request,
			static_cast<std::uint32_t>(sizeof(request)), "OBody");

		if (g_interface) {
			spdlog::info("[obody] asked at kPostPostLoad: interface written (dispatch returned {}); waiting for "
						 "OBodyIsReady - an event, no timer",
				dispatched);
		} else {
			// The module is there and nothing came back: a real anomaly, not
			// the normal absence. ReportFiles adds what is on disk.
			spdlog::warn("[obody] asked at kPostPostLoad: OBody.dll is loaded but no interface was written "
						 "(dispatch returned {}); the body section will not be offered",
				dispatched);
		}
	}

	void ReportFiles()
	{
		const bool dll = REX::W32::GetModuleHandleW(L"OBody.dll") != nullptr;
		bool       esp = false;
		if (auto* data = RE::TESDataHandler::GetSingleton()) {
			esp = data->LookupLoadedModByName("OBody.esp") || data->LookupLoadedLightModByName("OBody.esp");
		}
		// Diagnostic only (see the header): these two never decide anything.
		spdlog::info("[obody] files, diagnostic only: OBody.dll {}, OBody.esp {}; interface {}, ready {}",
			dll ? "loaded" : "not loaded", esp ? "loaded" : "not loaded", g_interface ? "written" : "none",
			g_ready.load());
	}

	API::IPluginInterface* Ready()
	{
		return g_ready.load() ? g_interface : nullptr;
	}

	void OnReadinessChanged(Callback a_fn)
	{
		g_onChanged.push_back(a_fn);
	}
}
