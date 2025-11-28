#pragma once
#include"Event.h"

namespace Redflag {
	class REDFLAG_API MouseMovedEvent :public Event
	{
	public:
		EVENT_CLASS_CATEGORY(EventCategoryMouse|EventCategoryMouse)
		EVENT_CLASS_TYPE(MouseMoved)

		MouseMovedEvent(float x,float y)
			:m_MouseX(x),m_MouseY(y){}


		inline float GetX()const { return m_MouseX; }
		inline float GetY()const { return m_MouseY; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseMovedEvent:" << m_MouseX << "," << m_MouseY;
			return ss.str();
		}

	private:
		float m_MouseX, m_MouseY;
	};

	class REDFLAG_API MouseScrolledEvent :public Event
	{
	public:
		EVENT_CLASS_TYPE(MouseScrolled)
		EVENT_CLASS_CATEGORY(EventCategoryMouse|EventCategoryInput)
		
		MouseScrolledEvent(float xOffset,float yOffset)
			:m_XOffset(xOffset),m_YOffset(yOffset){}

		inline float GetXOffset() const { return m_XOffset; }
		inline float GetYOffset() const { return m_YOffset; }

	private:
		float m_XOffset, m_YOffset;
	};






	class REDFLAG_API MouseButtonEvent :public Event
	{
	public:
		EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
		inline int GetMouseButton()const { return m_Button; }
	protected:
		MouseButtonEvent(int button)
			:m_Button(button) {}

		int m_Button; //Derived classes must call the base class constructor!!!
	};

	class REDFLAG_API MouseButtonPressedEvent :public MouseButtonEvent
	{
	public:
		EVENT_CLASS_TYPE(MouseButtonPressed)

		MouseButtonPressedEvent(int button)
			:MouseButtonEvent(button){}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseButtonPressedEvent:" << m_Button;
			return ss.str();
		}
	};
	class REDFLAG_API MouseButtonReleasedEvent :public MouseButtonEvent
	{
	public:
		EVENT_CLASS_TYPE(MouseButtonReleased)

			MouseButtonReleasedEvent(int button)
			:MouseButtonEvent(button) {}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseButtonReleasedEvent:" << m_Button;
			return ss.str();
		}
	};

}

