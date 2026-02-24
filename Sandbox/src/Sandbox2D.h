#pragma once

#include"Redflag.h"

#include"ParticleSystem.h"

class Sandbox2D :public Redflag::Layer
{
public:
	Sandbox2D();
	virtual ~Sandbox2D() = default;

	virtual void OnAttach() override;
	virtual void OnDetach() override;

	virtual void OnUpdate(Redflag::Timestep ts) override;
	virtual void OnImGuiRender() override;
	virtual void OnEvent(Redflag::Event& event) override;
private:
	Redflag::OrthographicCameraController m_CameraController;

	Redflag::Ref<Redflag::Shader> m_FlatColorShader;
	Redflag::Ref<Redflag::VertexArray> m_SquareVA;
	Redflag::Ref<Redflag::Framebuffer> m_Framebuffer;

	Redflag::Ref<Redflag::Texture2D> m_RedCloudTexture;
	Redflag::Ref<Redflag::Texture2D> m_CheckerboardTexture;

	Redflag::Ref<Redflag::Texture2D> m_SpriteSheet;
	Redflag::Ref<Redflag::SubTexture2D> m_TextureStairs;
	Redflag::Ref<Redflag::SubTexture2D> m_TextureBarrel;
	Redflag::Ref<Redflag::SubTexture2D> m_TextureTree;
	

	Redflag::Ref<Redflag::Texture2D> m_rogueLikeCharSpriteSheet;
	Redflag::Ref<Redflag::SubTexture2D> m_TextureRogueLikeCharFrame1;
	Redflag::Ref<Redflag::SubTexture2D> m_TextureRogueLikeCharFrame2;

	glm::vec4 m_SquareColor = { 0.95f, 0.5f, 0.5f, 1.0f }; // 樱花粉

	ParticleSystem m_ParticleSystem;
	ParticleProps m_Particle;

	uint32_t m_MapWidth, m_MapHeight;
	std::unordered_map<char, Redflag::Ref<Redflag::SubTexture2D>> s_TextureMap; // 存储地图字符与子纹理的映射关系

};