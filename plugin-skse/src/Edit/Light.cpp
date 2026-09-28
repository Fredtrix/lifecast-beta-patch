// Light.cpp
// Atelier

#include "Edit/Light.h"

#include "Core/RaceMenuSync.h"
#include "Edit/Session.h"
#include "Panel/Bridge.h"

namespace Atelier::Edit::Light
{
	namespace
	{
		// RaceMenu creates its light already lit; this is that assumption,
		// not a read. Reset on every menu open.
		bool g_on = true;

		void PushLight(const char* a_why)
		{
			spdlog::info("[light] {}: on = {}", a_why, g_on);
			Panel::Bridge::Send("light", { { "on", g_on } });
		}

		void OnLightGet(const nlohmann::json&) { PushLight("the page asked"); }

		void OnLightSet(const nlohmann::json& a_data)
		{
			if (!Session::MenuOpen()) {
				spdlog::warn("[light] light-set with the RaceSex Menu closed - ignored");
				return;
			}
			if (!a_data.is_object() || !a_data.contains("on") || !a_data["on"].is_boolean()) {
				spdlog::warn("[light] light-set without a boolean \"on\" - ignored");
				return;
			}
			g_on = a_data["on"].get<bool>();
			Core::RaceMenuSync::ToggleLight(g_on);
			PushLight("after light-set");
		}

		void OnOpened() { g_on = true; }

		void OnClosed()
		{
			if (!g_on) {
				Core::RaceMenuSync::ToggleLight(true);
				spdlog::info("[light] the RaceSex Menu closed with the light off - turned back on");
			}
			g_on = true;
		}

		bool g_installed = false;
	}

	void Install()
	{
		if (g_installed) {
			return;
		}
		Session::OnMenuOpened(&OnOpened);
		Session::OnMenuClosed(&OnClosed);
		Panel::Bridge::On("light-get", &OnLightGet);
		Panel::Bridge::On("light-set", &OnLightSet);
		g_installed = true;
		spdlog::info("[light] installed");
	}
}
