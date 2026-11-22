#pragma once

#ifdef RF_PLATFORM_WINDOWS
#include "Redflag/Renderer/RendererAPI.h"
#include <charconv>
#include <filesystem>
#include <memory>
#include <string_view>

extern Redflag::Application* Redflag::CreateApplication();
#ifdef RF_FORGE
namespace Redflag { void BakeSceneProbes(const std::filesystem::path& scene); }
#endif
int main(int argc, char** argv)
{
	Redflag::Log::Init();
	unsigned frames = 0;
#ifdef RF_FORGE
	std::filesystem::path bakeScene;
#endif
	for (int i = 1; i < argc; ++i)
	{
		const std::string_view option(argv[i]);
		if (option == "--opengl")
			Redflag::RendererAPI::SetAPI(Redflag::RendererAPI::API::OpenGL);
#ifdef RF_FORGE
		else if (option == "--vulkan")
			Redflag::RendererAPI::SetAPI(Redflag::RendererAPI::API::Vulkan);
		else if (option == "--bake-probes")
		{
			if (++i == argc || !*argv[i])
			{
				RF_CORE_ERROR("--bake-probes requires a scene file");
				return 2;
			}
			bakeScene = argv[i];
		}
#endif
		else if (option == "--frames" && i + 1 < argc)
		{
			const std::string_view count(argv[++i]);
			const auto end = count.data() + count.size();
			const auto result = std::from_chars(count.data(), end, frames);
			if (result.ec != std::errc() || result.ptr != end || !frames)
			{
				RF_CORE_ERROR("--frames requires a positive frame count");
				return 2;
			}
		}
		else
		{
			RF_CORE_ERROR("Unknown or incomplete option: {0}", argv[i]);
			return 2;
		}
	}
	try
	{
#ifdef RF_FORGE
		if (!bakeScene.empty())
		{
			Redflag::RendererAPI::SetAPI(Redflag::RendererAPI::API::Vulkan);
			Redflag::BakeSceneProbes(bakeScene);
			return 0;
		}
#endif
		RF_PROFILE_BEGIN_SESSION("Startup", "RedflagProfile-Startup.json");
		std::unique_ptr<Redflag::Application> app(Redflag::CreateApplication());
		RF_PROFILE_END_SESSION();
		RF_PROFILE_BEGIN_SESSION("Runtime", "RedflagProfile-Runtime.json");
		app->Run(frames);
		RF_PROFILE_END_SESSION();
		RF_PROFILE_BEGIN_SESSION("Shutdown", "RedflagProfile-Shutdown.json");
		app.reset();
	}
	catch (const std::exception& error)
	{
		RF_CORE_ERROR("{0}", error.what());
		RF_PROFILE_END_SESSION();
		return 1;
	}
	RF_PROFILE_END_SESSION();
}

#endif
