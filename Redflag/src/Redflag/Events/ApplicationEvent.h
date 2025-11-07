#pragma once

#include"Event.h"
#include<sstream>

namespace Redflag {
	class REDFLAG_API WindowResizeEvent :public Event
	{
	public:
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
		EVENT_CLASS_TYPE(WindowResize)

		WindowResizeEvent(unsigned int width,unsigned int height)
			:m_Width(width),m_Height(height){ }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss<<"WindowResizeEvent:"<<m_Width<<","<<m_Height;
			return ss.str();
		}

	private:
		unsigned int m_Width, m_Height;
	};

	class REDFLAG_API WindowClosedEvent :public Event
	{
	public:
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
		EVENT_CLASS_TYPE(WindowClose)
		WindowClosedEvent() {}
	};


	//If we would like to update,render,tick functions to be propagated as events...
	class REDFLAG_API AppTickEvent :public Event {
	public:
		AppTickEvent() {}
		EVENT_CLASS_TYPE(AppTick)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};
	class REDFLAG_API AppUpdateEvent :public Event {
	public:
		AppUpdateEvent() {}
		EVENT_CLASS_TYPE(AppUpdate)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};
	class REDFLAG_API AppRenderEvent :public Event {
	public:
		AppRenderEvent() {}
		EVENT_CLASS_TYPE(AppRender)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};
}