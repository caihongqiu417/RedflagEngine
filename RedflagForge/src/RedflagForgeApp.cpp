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
		RedflagForge()
			:Application("Redflag Forge")
		{
			if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
			{
				VulkanForgeLayer* editor = new VulkanForgeLayer();
				PushLayer(editor);
				editor->OpenDefaultScene();
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

	void BakeSceneProbes(const std::filesystem::path& scene)
	{
		Application app("Redflag Forge", false);
		VulkanForgeLayer* editor = new VulkanForgeLayer();
		app.PushLayer(editor);
		editor->BakeScene(scene);
	}
}
