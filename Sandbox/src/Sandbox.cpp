#include"rfpch.h"

#include "Redflag.h"

class ExampleLayer :public Redflag::Layer 
{
public:
	ExampleLayer()
		:Layer("Example")
	{

	}

	void OnUpdate() override
	{
		RF_CLIENT_INFO("Examplelayer::Update");
	}

	void OnEvent(Redflag::Event& event)override
	{
		RF_CLIENT_TRACE("{0}", event.ToString());
	}
};

class Sandbox :public Redflag::Application
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

