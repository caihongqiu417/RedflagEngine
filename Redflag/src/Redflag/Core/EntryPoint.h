#pragma once

#ifdef RF_PLATFORM_WINDOWS

extern Redflag::Application* Redflag::CreateApplication();  
int main()
{
	Redflag::Log::Init();
	RF_CORE_WARN("Intialized Log System!");
	RF_CLIENT_INFO("Welcome to Redflag Engine!!!");

	auto app = Redflag::CreateApplication();
	app->Run();
	delete app;
}

#endif