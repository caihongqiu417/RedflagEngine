#pragma once

#include "Redflag/Core/Layer.h"
#include "Redflag/Renderer/EditorCamera.h"
#include "Redflag/Scene/Panel/SceneHierachyPanel.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "LightingPreview.h"
#include "GameViewport.h"
#include "ProjectPanel.h"
#include "NpcVisionPanel.h"

#include <deque>
#include <filesystem>

namespace Redflag
{
	/* Vulkan 编辑视图沿用场景文件、层级面板与编辑器相机。面板持有离屏图，模型的选择交给 SceneRenderer。 */
	class VulkanForgeLayer : public Layer
	{
	public:
		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep ts) override;
		void OnEvent(Event& event) override;
		void OnImGuiRender() override;
		void OpenDefaultScene();
		bool OpenScene(const std::filesystem::path& path);
		void BakeScene(const std::filesystem::path& path);

	private:
		void NewScene();
		void ToggleSimulation();
		void SaveScene(bool choosePath);
		void AddCamera();
		void Report(const std::string& message);
		void ImportModel(const std::filesystem::path& path);
		void DrawViewport();
		void DrawTransformTools();
		bool DrawSceneGizmos(const glm::mat4& projection, const glm::mat4& view, ImVec2 corner, ImVec2 size);
		void DrawMenu();

		Scope<VulkanDraw> m_Draw;
		Scope<LightingPreview> m_Lighting;
		ProjectPanel m_Project;
		NpcVisionPanel m_NpcVision;
		VkDescriptorSet m_PanelTexture = VK_NULL_HANDLE;
		EditorCamera m_Camera{ 45.0f, 16.0f / 9.0f, 0.1f, 1000.0f };
		Ref<Scene> m_Scene;
		Ref<Scene> m_EditScene;
		SceneHierachyPanel m_Hierarchy;
		std::filesystem::path m_ScenePath;
		std::deque<std::string> m_Messages;
		bool m_GameView = false;
		GameViewport m_GameViewport;
		bool m_SelectSceneTab = false;
		bool m_ResetLayout = false;
		bool m_Hovered = false;
		bool m_ViewportFocused = false;
		bool m_HasSceneImage = false;
		glm::mat4 m_LastView{ 1 };
		double m_NavigationUntil = 0;
		bool m_Navigating = false;
		int m_TransformTool = 0;
		bool m_ShowLights = true;
	};
}
