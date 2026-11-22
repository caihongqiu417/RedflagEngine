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
		explicit RedflagForge(const std::filesystem::path& scene = {}, bool visible = true, bool play = false, unsigned renderHeight = 1080)
			:Application(WindowProps(play ? "Redflag Game" : "Redflag Forge", play ? 1920 : 2560, play ? 1080 : 1440, visible))
		{
			if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
			{
				VulkanForgeLayer* editor = new VulkanForgeLayer(renderHeight);
				PushLayer(editor);
				if (scene.empty()) editor->OpenDefaultScene();
				else if (!editor->OpenScene(scene)) throw std::runtime_error("Cannot open scene: " + scene.u8string());
				if (play) editor->PlayGame();
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

	Application* CreateForgeEditor(const std::filesystem::path& scene, bool visible, bool play, unsigned renderHeight)
	{
		if (!scene.empty())
			RendererAPI::SetAPI(RendererAPI::API::Vulkan);
		return new RedflagForge(scene, visible, play, renderHeight);
	}

	void BakeSceneProbes(const std::filesystem::path& scene)
	{
		Application app("Redflag Forge", false);
		VulkanForgeLayer* editor = new VulkanForgeLayer();
		app.PushLayer(editor);
		editor->BakeScene(scene);
	}
}
