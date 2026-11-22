#include"rfpch.h"

#include <Redflag.h>
#include<Redflag/Core/EntryPoint.h>

#include "ForgeLayer.h"
#include "VulkanForgeLayer.h"
#include "Redflag/Renderer/RendererAPI.h"

namespace Redflag{

	class RedflagForge :public Application
	{
	public:
		explicit RedflagForge(const std::filesystem::path& scene = {})
			:Application("Redflag Forge")
		{
			if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
			{
				VulkanForgeLayer* editor = new VulkanForgeLayer();
				PushLayer(editor);
				if (scene.empty()) editor->OpenDefaultScene();
				else if (!editor->OpenScene(scene)) throw std::runtime_error("Cannot open scene: " + scene.u8string());
			}
			else
				PushLayer(new ForgeLayer());
		}
		
		~RedflagForge()
		{

		}
	};

	Application* CreateApplication()
	{
		return new RedflagForge();
	}

	Application* OpenVulkanEditor(const std::filesystem::path& scene)
	{
		RendererAPI::SetAPI(RendererAPI::API::Vulkan);
		return new RedflagForge(scene);
	}

	void BakeSceneProbes(const std::filesystem::path& scene)
	{
		Application app("Redflag Forge", false);
		VulkanForgeLayer* editor = new VulkanForgeLayer();
		app.PushLayer(editor);
		editor->BakeScene(scene);
	}
}
