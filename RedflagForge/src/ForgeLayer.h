#pragma once

#include"Redflag.h"
#include"Scene/Panel/SceneHierachyPanel.h"
#include"Scene/Panel/ContentBrowserPanel.h"

namespace Redflag {

	class ForgeLayer :public Layer
	{
	public:
		ForgeLayer ();
		virtual ~ForgeLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;

		virtual void OnUpdate(Timestep ts) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Event& event) override;
	private:
		bool OnKeyPressed(KeyPressedEvent& e);
		bool OnMouseButtonPressed(MouseButtonPressedEvent& e);

		void NewScene();
		void OpenScene();
		void SaveSceneAs();
		void SaveScene();

		void OnScenePlay();
		void OnSceneStop();
		void OnSceneSimulate();
		void OnSceneSimulateStop();

		void OnDuplicateEntity();

		//UI���
		void UI_Toolbar();

	private:
		OrthographicCameraController m_CameraController;

		Ref<Redflag::Texture2D> m_RedCloudTexture;
		Ref<Redflag::Texture2D> m_CheckerboardTexture;

		// 3D 测试用立方体
		Ref<Redflag::Mesh> m_CubeMesh;

		

		Ref<Redflag::Framebuffer> m_Framebuffer;
		glm::vec2 m_ViewportSize = { 0.0f,0.0f };
		glm::vec2 m_ViewportBounds[2];
		

		Ref<Scene> m_EditorScene;   // ԭʼ����
		Ref<Scene> m_ActiveScene;   // ��ǰ��Ծ���༭ʱָ�� m_EditorScene��Play ʱָ�򸱱���

		Entity m_SquareEntity;
		Entity m_SecondSquareEntity;
		Entity m_Camera;
		Entity m_SecondCamera;

		Entity m_HoveredEntity;
		int m_LastHoveredEntityID = -1;

		bool m_PrimaryCamera = true;

		EditorCamera m_EditorCamera;

		bool m_ViewportFocused=false;
		bool m_ViewportHovered=false;

		std::string m_SceneFilePath;

		//Panels
		SceneHierachyPanel m_SceneHierachyPanel;
		ContentBrowserPanel m_ContentBrowserPanel;

		//Gizmo
		int m_GizmoType = -1;

		// 碰撞体可视化开关（编辑模式下显示绿色线框）
		bool m_ShowPhysicsColliders = true;

		// FPS 显示缓存（每 0.5s 刷新一次）
		float m_DisplayedFPS      = 0.0f;
		float m_DisplayedFrameMs  = 0.0f;
		float m_FpsAccumTime      = 0.0f;

		//工具栏按钮
		Ref<Texture2D> m_IconPlay, m_IconStop, m_IconSimulate;

		enum class SceneState
		{
			Edit=0, Play=1, Simulate=2
		};

		SceneState m_SceneState = SceneState::Edit;
	};
}
