#pragma once

#include"Redflag.h"

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


	glm::vec4 m_SquareColor = { 0.95f, 0.5f, 0.5f, 1.0f }; // Ó£»¨·Û

};