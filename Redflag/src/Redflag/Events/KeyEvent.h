#pragma once

#include "Event.h"

namespace Redflag {
	//Abstract KeyEvent
	class REDFLAG_API KeyEvent :public Event
	{
	public:
		inline int GetKeyCode() const { return m_Keycode; }

		EVENT_CLASS_CATEGORY(EventCategoryKeyboard|EventCategoryInput)

	protected:
		KeyEvent(int keycode)
			:m_Keycode(keycode) { }      //Derived classes must call the base class constructor!!!

		int m_Keycode;
	};






	class REDFLAG_API KeyPressedEvent :public KeyEvent
	{
	public:
		EVENT_CLASS_TYPE(KeyPressed)

		KeyPressedEvent(int keycode,int repeatCount)
			:KeyEvent(keycode),m_RepeatCount(repeatCount){}

		inline int GetRepeatCount()const { return m_RepeatCount; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyPressedEvent:" << m_Keycode << "(" << m_RepeatCount << "repeats)";
		}
	private:
		int m_RepeatCount;
	};

	class REDFLAG_API KeyRealeasedEvent :public KeyEvent
	{
	public:
		EVENT_CLASS_TYPE(KeyReleased)

		KeyRealeasedEvent(int keycode)
			:KeyEvent(keycode){}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyReleasedEvent" << m_Keycode;
			return ss.str();
		}


	};


}
