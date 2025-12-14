#include "rfpch.h"
#include "Redflag.h"

class ExampleLayer : public Redflag::Layer
{
public:
    ExampleLayer()
        : Layer("Example")
    {
    }

    void OnUpdate() override
    {
        if (Redflag::Input::IsKeyPressed(RF_KEY_TAB))
        {
            RF_CLIENT_INFO("ExampleLayerOnUpdate:TAB∞¥œ¬¡À");
        }
    }

    void OnEvent(Redflag::Event& event) override
    {
        RF_CLIENT_TRACE("{0}", event.ToString());

        if (event.GetEventType() == Redflag::EventType::KeyPressed)
        {
            Redflag::KeyPressedEvent& e = (Redflag::KeyPressedEvent&)event;
            RF_CLIENT_INFO("{0}", (char)e.GetKeyCode());
        }
    }
};

class Sandbox : public Redflag::Application
{
public:
    Sandbox()
    {
        PushLayer(new ExampleLayer());
        PushOverlay(new Redflag::ImGuiLayer());
    }

    ~Sandbox()
    {
    }
};

Redflag::Application* Redflag::CreateApplication()
{
    return new Sandbox();
}