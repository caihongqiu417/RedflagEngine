#pragma once

#include"Redflag.h"


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

		Ref<Redflag::Shader> m_FlatColorShader;
		Ref<Redflag::VertexArray> m_SquareVA;
		Ref<Redflag::Framebuffer> m_Framebuffer;

		Ref<Redflag::Texture2D> m_RedCloudTexture;
		Ref<Redflag::Texture2D> m_CheckerboardTexture;

		Ref<Redflag::Texture2D> m_SpriteSheet;
		Ref<Redflag::SubTexture2D> m_TextureStairs;
		Ref<Redflag::SubTexture2D> m_TextureBarrel;
		Ref<Redflag::SubTexture2D> m_TextureTree;


		Ref<Redflag::Texture2D> m_rogueLikeCharSpriteSheet;
		Ref<Redflag::SubTexture2D> m_TextureRogueLikeCharFrame1;
		Ref<Redflag::SubTexture2D> m_TextureRogueLikeCharFrame2;

		glm::vec4 m_SquareColor = { 0.95f, 0.5f, 0.5f, 1.0f }; // 樱花粉

		ParticleSystem m_ParticleSystem;
		ParticleProps m_Particle;

		uint32_t m_MapWidth, m_MapHeight;
		std::unordered_map<char, Redflag::Ref<Redflag::SubTexture2D>> s_TextureMap; // 存储地图字符与子纹理的映射关系

	};
}
