// TintRefresh.cpp
// Atelier

#include "Core/TintRefresh.h"

#include "Core/RaceMenuSync.h"

namespace Atelier::Core::TintRefresh
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		std::atomic<bool> g_inFlight{ false };
		std::atomic<bool> g_pending{ false };
		std::atomic<bool> g_pendingFinal{ false };

		// Touched only on the game thread (dispatch) and in the callback,
		// never at the same time: one call is in flight at most.
		Clock::time_point g_sentAt{};
		bool              g_sentFinal = false;
		std::size_t       g_calls = 0;
		std::size_t       g_failed = 0;
		long long         g_totalUs = 0;
		long long         g_maxUs = 0;

		void Dispatch();

		class Done : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			void operator()(RE::BSScript::Variable) override
			{
				const auto us =
					std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - g_sentAt).count();
				++g_calls;
				g_totalUs += us;
				g_maxUs = (std::max)(g_maxUs, static_cast<long long>(us));
				spdlog::info("[tintfix] #{} Game.UpdateTintMaskColors returned after {} us{}", g_calls, us,
					g_sentFinal ? " (drag ended)" : "");
				// The rest runs on the game thread, like any other write.
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([]() {
						RaceMenuSync::ShadersInvalidated();
						g_inFlight = false;
						if (g_pending.exchange(false)) {
							Dispatch();
						}
					});
				} else {
					g_inFlight = false;
				}
			}

			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
		};

		void Dispatch()
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm) {
				spdlog::warn("[tintfix] no Papyrus VM - tint change not shown");
				return;
			}
			// A call that never returned must not block every later one.
			if (g_inFlight && Clock::now() - g_sentAt > std::chrono::seconds(2)) {
				spdlog::warn("[tintfix] the previous call has not returned in 2 s - sending a new one");
				g_inFlight = false;
			}
			if (g_inFlight.exchange(true)) {
				g_pending = true;
				return;
			}
			g_sentFinal = g_pendingFinal.exchange(false);
			g_sentAt = Clock::now();
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new Done());
			if (!vm->DispatchStaticCall("Game", "UpdateTintMaskColors", RE::MakeFunctionArguments(), callback)) {
				++g_failed;
				g_inFlight = false;
				spdlog::warn("[tintfix] DispatchStaticCall(Game.UpdateTintMaskColors) failed");
			}
		}
	}

	void Request(bool a_final)
	{
		if (a_final) {
			g_pendingFinal = true;
		}
		Dispatch();
	}

	void LogAndReset()
	{
		if (g_calls || g_failed) {
			spdlog::info("[tintfix] menu session: {} calls, {} failed, mean {} us, max {} us", g_calls, g_failed,
				g_calls ? g_totalUs / static_cast<long long>(g_calls) : 0, g_maxUs);
		}
		g_calls = 0;
		g_failed = 0;
		g_totalUs = 0;
		g_maxUs = 0;
	}
}
