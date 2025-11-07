

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

