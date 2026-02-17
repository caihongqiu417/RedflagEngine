#pragma once

#ifdef RF_PLATFORM_WINDOWS

extern Redflag::Application* Redflag::CreateApplication();  
int main()
{
	Redflag::Log::Init();
	RF_CORE_WARN("Intialized Log System");
	RF_CLIENT_INFO("Welcome to Redflag Engine");

	RF_PROFILE_BEGIN_SESSION("Startup", "RedflagProfile-Startup.json");
	auto app = Redflag::CreateApplication();
	RF_PROFILE_END_SESSION();
	
	RF_PROFILE_BEGIN_SESSION("Runtime", "RedflagProfile-Runtime.json");
	app->Run();
	RF_PROFILE_END_SESSION();

	RF_PROFILE_BEGIN_SESSION("Shutdown", "RedflagProfile-Shutdown.json");
	delete app;
	RF_PROFILE_END_SESSION();
}

#endif