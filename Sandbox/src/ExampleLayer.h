#pragma once

#include "Redflag.h"

class ExampleLayer : public Redflag::Layer
{
public:
    ExampleLayer();
    virtual ~ExampleLayer() = default;

    virtual void OnUpdate(Redflag::Timestep ts) override;
    virtual void OnImGuiRender() override;
    virtual void OnEvent(Redflag::Event& event) override;

private:
    Redflag::ShaderLibrary m_ShaderLibrary;

    // 三角形渲染相关成员变量
    Redflag::Ref<Redflag::VertexArray> m_VertexArray;
    Redflag::Ref<Redflag::Shader> m_Shader;

    // 正方形渲染相关成员变量
    Redflag::Ref<Redflag::VertexArray> m_SquareVA;
    Redflag::Ref<Redflag::Shader> m_FlatShader;
    glm::vec3 m_SquareColor = { 0.4f, 0.8f, 0.6f };
    Redflag::Ref<Redflag::Texture2D> m_Texture;
    Redflag::Ref<Redflag::Texture2D> m_RedCloudTexture;

    Redflag::OrthographicCameraController m_CameraController;
};