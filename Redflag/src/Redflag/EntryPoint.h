#pragma once

#ifdef RF_PLATFORM_WINDOWS

extern Redflag::Application* Redflag::CreateApplication();  //在基类Application中被声明，派生类Sandbox中被实现

int main()
{
	auto app = Redflag::CreateApplication();
	app->Run();
	delete app;
}

#endif