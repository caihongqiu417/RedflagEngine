#include "hzpch.h"
#include "Application.h"
#include "Redflag/Log.h"

//test for event
#include "Events/ApplicationEvent.h"


namespace Redflag
{
	Application::Application()
	{
	}

	Application::~Application()
	{
	}


    //test event system
    void Application::Run() {
        WindowResizeEvent e(1280, 720);
        if (e.IsInCategory(EventCategoryApplication)) {
            // 直接传递完整的字符串，不使用格式化占位符
            RF_CORE_TRACE("Application Event: " + e.ToString());
        }
        if (e.IsInCategory(EventCategoryInput)) {
            RF_CORE_ERROR("Input Event: " + e.ToString());
        }

        while (true)
        {

        }
	}
}