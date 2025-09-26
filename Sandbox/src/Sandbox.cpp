/*
* Sandbox 作为独立宿主可执行程序，
*链接并加载 Redflag.dll，
*实现并返回 Application 的派生实例，
*用来调试与演示
*/

#include "Redflag.h"

class Sandbox :public Redflag::Application
{
public:
	Sandbox()
	{

	}

	~Sandbox()
	{
	
	}

};

Redflag::Application* Redflag::CreateApplication()
{
	return new Sandbox();
}

