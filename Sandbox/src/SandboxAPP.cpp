#include "rfpch.h"
#include "Redflag.h"

#include "Redflag/Core/EntryPoint.h"

#include "Sandbox2D.h"
 #include "ExampleLayer.h"  // 可选

class Sandbox : public Redflag::Application
{
public:
    Sandbox()
    {
        // PushLayer(new ExampleLayer());  // 可选
        PushLayer(new Sandbox2D());
    }

    ~Sandbox()
    {
    }
};

Redflag::Application* Redflag::CreateApplication()
{
    return new Sandbox();
}