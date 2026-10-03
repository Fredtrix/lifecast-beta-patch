// Panel.cpp
// Lifecast

#include "Panel/Panel.h"

#include "Core/MeridianLink.h"
#include "Panel/Bridge.h"
#include "Version.h"

namespace Lifecast::Panel
{
	namespace
	{
		namespace MUI = Meridian::UI;

		MUI::IUIPlatformAPI::BrowserRefHandle g_handle = MUI::IUIPlatformAPI::InvalidBrowserRefHandle;
		Meridian::CEF::IBrowser* g_browser = nullptr;
		bool g_visible = false;

		// Set by Meridian's shutdown callback. From then on no browser call
		// is allowed, and Ensure() must not create a new one.
		std::atomic_bool g_shutdown = false;

		void OnMeridianShutdown()
		{
			g_shutdown = true;
			g_browser = nullptr;
			g_handle = MUI::IUIPlatformAPI::InvalidBrowserRefHandle;
			spdlog::info("[panel] Meridian is shutting down - browser dropped");
		}

		void LogState(const char* a_what)
		{
			if (!g_browser) {
				spdlog::info("[panel] {} - no browser", a_what);
				return;
			}

			spdlog::info(
				"[panel] {} - handle {}, browserReady {}, pageLoaded {}, visible {}, focused {}",
				a_what,
				g_handle,
				g_browser->IsBrowserReady(),
				g_browser->IsPageLoaded(),
				g_browser->IsBrowserVisible(),
				g_browser->IsBrowserFocused());
		}

		// The page says hello when its script starts, and the answer tells it
		// which DLL it is talking to and whether they speak the same
		// protocol. A page that gets no answer at all knows the bridge is
		// down; a page that gets compatible = false knows it is out of date.
		void OnHello(const nlohmann::json& a_data)
		{
			std::uint32_t pageProtocol = 0;
			if (a_data.contains("protocol") && a_data["protocol"].is_number_unsigned()) {
				pageProtocol = a_data["protocol"].get<std::uint32_t>();
			}

			std::string pageVersion = "?";
			if (a_data.contains("page") && a_data["page"].is_string()) {
				pageVersion = a_data["page"].get<std::string>();
			}

			const bool compatible = pageProtocol == Bridge::kProtocolVersion;
			spdlog::info("[panel] hello from page {} speaking protocol v{} - {}",
				pageVersion, pageProtocol, compatible ? "compatible" : "INCOMPATIBLE");

			Bridge::Send("hello", {
				{ "dll", Version::kString },
				{ "protocol", Bridge::kProtocolVersion },
				{ "compatible", compatible },
				{ "visible", g_visible },
			});
		}
	}

	void Init()
	{
		Bridge::On("hello", &OnHello);
	}

	bool Ensure()
	{
		if (g_shutdown) {
			return false;
		}

		if (g_browser) {
			return true;
		}

		auto* platform = Core::MeridianLink::Platform();
		if (!platform) {
			spdlog::error("[panel] the Meridian platform API never answered - no panel");
			return false;
		}

		static bool s_shutdownRegistered = false;
		if (!s_shutdownRegistered) {
			platform->RegisterOnShutdown(&OnMeridianShutdown);
			s_shutdownRegistered = true;
		}

		Meridian::CEF::IBrowser* browser = nullptr;
		const auto handle = platform->AddOrGetBrowser(
			kBrowserName,
			Bridge::Bindings(),
			Bridge::BindingCount(),
			kStartUrl,
			browser);

		spdlog::info("[panel] AddOrGetBrowser(\"{}\", \"{}\") = handle {}, browser {}",
			kBrowserName, kStartUrl, handle, static_cast<const void*>(browser));

		if (handle == MUI::IUIPlatformAPI::InvalidBrowserRefHandle || !browser) {
			spdlog::error("[panel] the browser was refused - no panel");
			return false;
		}

		g_handle = handle;
		g_browser = browser;

		// Created hidden: what shows it is the caller's decision, never a
		// side effect of creation.
		g_browser->SetBrowserVisible(false);
		g_visible = false;

		LogState("created");
		return true;
	}

	void SetVisible(bool a_visible)
	{
		if (!g_browser) {
			return;
		}

		g_browser->SetBrowserVisible(a_visible);
		g_visible = a_visible;
		LogState(a_visible ? "shown" : "hidden");

		Bridge::Send("visibility", { { "visible", a_visible } });
	}

	bool IsVisible()
	{
		return g_browser && g_visible;
	}

	void ArmFocusChord(std::uint32_t a_key1, std::uint32_t a_key2)
	{
		if (!g_browser) {
			return;
		}

		g_browser->ToggleBrowserFocusByKeys(a_key1, a_key2);
		spdlog::info("[panel] focus chord armed: 0x{:X} + 0x{:X}", a_key1, a_key2);
	}

	void DisarmFocusChord()
	{
		if (!g_browser) {
			return;
		}

		g_browser->ToggleBrowserFocusByKeys(0, 0);
		spdlog::info("[panel] focus chord disarmed");
	}

	void RequestFocus()
	{
		if (!g_browser) {
			return;
		}

		g_browser->SetBrowserFocused(true);
		LogState("focus requested");
	}

	bool IsFocused()
	{
		return g_browser && g_browser->IsBrowserFocused();
	}

	Meridian::CEF::IBrowser* Browser()
	{
		return g_shutdown ? nullptr : g_browser;
	}
}
