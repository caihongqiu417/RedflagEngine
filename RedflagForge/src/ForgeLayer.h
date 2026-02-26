#pragma once

#include"Redflag.h"
#include"Scene/Panel/SceneHierachyPanel.h"

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
		OrthographicCameraController m_CameraController;

		Ref<Redflag::Texture2D> m_RedCloudTexture;
		Ref<Redflag::Texture2D> m_CheckerboardTexture;

		

		Ref<Redflag::Framebuffer> m_Framebuffer;
		glm::vec2 m_ViewportSize = { 0.0f,0.0f };
		

		Ref<Scene> m_ActiveScene;
		Entity m_SquareEntity;
		Entity m_CameraEntity;
		Entity m_SecondCamera;

		bool m_PrimaryCamera = true;

		bool m_ViewportFocused=false;
		bool m_ViewportHovered=false;

		//Panels
		SceneHierachyPanel m_SceneHierachyPanel;
	};
}
