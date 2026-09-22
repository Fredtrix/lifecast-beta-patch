// MeridianLink.cpp
// Atelier

#include "MeridianLink.h"

#include "MeridianUIAPI/SKSELoader.h"

namespace Atelier::Core::MeridianLink
{
	namespace
	{
		namespace MUI = Meridian::UI;

		MUI::IUIPlatformAPI* g_platform = nullptr;
		MUI::View::IViewAPI* g_views = nullptr;
		MUI::NifScene::INifSceneAPI4* g_nifScene = nullptr;
		MUI::Input::IInputAPI* g_input = nullptr;
		MUI::RenderLayer::IRenderLayerAPI* g_renderLayer = nullptr;

		// The consumer name Meridian sees, and the mod:// host the panel is
		// addressed by. Data/MeridianUI/Atelier/ is the folder, so
		// mod://Atelier/index.html is the URL and ViewCreateInfo.ownerName
		// has to match that host - Meridian pins native bindings to the
		// initial trusted origin and rejects a mismatch.
		constexpr auto kConsumerName = "Atelier";

		// One Settings for the whole plugin. It must outlive the
		// ProcessSKSEMessage call that dispatches it, and the extension
		// queries take it too. Defaults are the release-correct values -
		// never assign structSize, and never memset this after construction.
		MUI::Settings& PluginSettings()
		{
			static MUI::Settings s_settings{};
			return s_settings;
		}
	}

	void Init()
	{
		spdlog::info(
			"[meridian] compiled against MeridianUIAPI lib {} / API {}; "
			"asking for Meridian.View v{}, Meridian.NifScene v{}, "
			"Meridian.Input v{} and Meridian.RenderLayer v{}",
			MUI::LibVersion::AS_STRING,
			MUI::APIVersion::AS_STRING,
			MUI::View::INTERFACE_VERSION,
			MUI::NifScene::ACTOR_APPEARANCE_INTERFACE_VERSION,
			MUI::Input::INTERFACE_VERSION,
			MUI::RenderLayer::INTERFACE_VERSION);

		MUI::SKSELoader::GetUIPlatformAPIWithVersionCheck([](MUI::IUIPlatformAPI* a_api) {
			g_platform = a_api;
			spdlog::info("[meridian] platform API ready: {}",
				static_cast<const void*>(a_api));
		});
	}

	void ForwardSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		MUI::SKSELoader::ProcessSKSEMessage(a_msg, &PluginSettings());
	}

	void QueryExtensions(const char* a_moment)
	{
		auto* settings = &PluginSettings();

		if (!g_views) {
			g_views = MUI::View::Query(settings, kConsumerName);
			spdlog::info("[meridian] [{}] Meridian.View v{} = {}",
				a_moment, MUI::View::INTERFACE_VERSION,
				static_cast<const void*>(g_views));
		}

		if (!g_nifScene) {
			// Query4 is the actor-appearance interface. Asking for it and
			// getting null does not mean the extension is absent: an older
			// Meridian can answer version 1, 2 or 3 and refuse 4. The
			// side-by-side comparison turns on that distinction, so the
			// failure is narrowed by asking for version 1 as well.
			g_nifScene = MUI::NifScene::Query4(settings, kConsumerName);
			spdlog::info("[meridian] [{}] Meridian.NifScene v{} = {}",
				a_moment, MUI::NifScene::ACTOR_APPEARANCE_INTERFACE_VERSION,
				static_cast<const void*>(g_nifScene));

			if (!g_nifScene) {
				auto* base = MUI::NifScene::Query(settings, kConsumerName);
				spdlog::info(
					"[meridian] [{}] Meridian.NifScene v{} = {} - {}",
					a_moment, MUI::NifScene::INTERFACE_VERSION,
					static_cast<const void*>(base),
					base
						? "extension present but refuses the actor-appearance version"
						: "extension not answering at all");
			}
		}

		if (!g_input) {
			g_input = MUI::Input::Query(settings, kConsumerName);
			spdlog::info("[meridian] [{}] Meridian.Input v{} = {}",
				a_moment, MUI::Input::INTERFACE_VERSION,
				static_cast<const void*>(g_input));
		}

		if (!g_renderLayer) {
			g_renderLayer = MUI::RenderLayer::Query(settings, kConsumerName);
			spdlog::info("[meridian] [{}] Meridian.RenderLayer v{} = {}",
				a_moment, MUI::RenderLayer::INTERFACE_VERSION,
				static_cast<const void*>(g_renderLayer));
		}
	}

	MUI::IUIPlatformAPI* Platform()
	{
		return g_platform;
	}

	MUI::View::IViewAPI* Views()
	{
		return g_views;
	}

	MUI::NifScene::INifSceneAPI4* NifScene()
	{
		return g_nifScene;
	}

	MUI::Input::IInputAPI* Input()
	{
		return g_input;
	}

	MUI::RenderLayer::IRenderLayerAPI* RenderLayer()
	{
		return g_renderLayer;
	}
}
