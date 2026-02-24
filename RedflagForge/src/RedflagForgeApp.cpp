#include"rfpch.h"

#include <Redflag.h>
#include<Redflag/Core/EntryPoint.h>

#include "ForgeLayer.h"

namespace Redflag{

	class RedflagForge :public Application
	{
	public:
		RedflagForge()
			:Application("Redflag Forge")
		{
			PushLayer(new ForgeLayer());
		}
		
		~RedflagForge()
		{

		}
	};

	Application* CreateApplication()
	{
		return new RedflagForge();
	}
}