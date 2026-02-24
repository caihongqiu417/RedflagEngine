#pragma once


// Project headers  
#include "Redflag/Core/Core.h"
#include"string.h"

namespace Redflag {

	//For now events in Redflag are blocking,meaning when an event occurs
	//it immediately get dispatched.But for the future,it might be to buffer 
	//events in an event bus and defer them

	enum class EventType
	{
		None=0,
		WindowClose,WindowResize,WindowFocus,WindowLostFocus,WindowMoved,
		AppTick,AppUpdate,AppRender,
		KeyPressed,KeyReleased,KeyTyped,
		MouseButtonPressed,MouseButtonReleased,MouseMoved,MouseScrolled
	};

	enum EventCategory
	{
		None = 0,                           // 00000 - No category
		EventCategoryApplication = BIT(0),  // 00001 - Application events (window resize, app shutdown, etc.)
		EventCategoryInput = BIT(1),        // 00010 - Input events (general input category)
		EventCategoryKeyboard = BIT(2),     // 00100 - Keyboard events (key press, release, etc.)
		EventCategoryMouse = BIT(3),        // 01000 - Mouse events (mouse move, scroll, etc.)
		EventCategoryMouseButton = BIT(4)   // 10000 - Mouse button events (mouse button press, release, etc.)
	};




	class REDFLAG_API Event
	{
		friend class EventDispatcher;
	public:
		virtual EventType GetEventType() const = 0;
		virtual int GetCategoryFlags()const = 0;
		inline bool IsInCategory(EventCategory category)
		{
			return GetCategoryFlags() & category;
		}

		virtual const char* GetName() const = 0;
		virtual std::string ToString() const { return GetName(); }

		inline bool Handled() { return m_Handled; }
		inline void SetHandled(bool handled) { m_Handled = handled; }

		bool m_Handled = false;           //Control dispatching to lawyers
	};

	// Macro to define event type methods - provides static and virtual type and name accessors
#define EVENT_CLASS_TYPE(type) \
    static EventType GetStaticType() { return EventType::##type; } \
    virtual EventType GetEventType() const override { return GetStaticType(); } \
    virtual const char* GetName() const override { return #type; }

// Macro to define event category - returns category flags for event filtering  
#define EVENT_CLASS_CATEGORY(category) \
    virtual int GetCategoryFlags() const override { return category; }




	class EventDispatcher
	{
		template<typename T>
		using EventFn = std::function<bool(T&)>;  //EventFn is a variable which stores function(input T& and return bool)

	public:
		EventDispatcher(Event& event)
			:m_Event(event){}

		// How it works:
				// 1. Checks if the actual event type matches the requested type T
				// 2. If types match, casts the base Event reference to the specific type T
				// 3. Calls the provided handler function with the typed event reference
				// 4. Sets the event's handled flag based on the handler's return value
				// 5. Returns true to indicate successful dispatch and handling
		template<typename T>
		bool Dispatch(EventFn<T> func)
		{
			// Check if the stored event matches the type we're trying to dispatch
			if (m_Event.GetEventType() == T::GetStaticType())
			{
				// Cast the base event to the specific derived event type and call the handler
				// The handler's return value determines if the event is marked as handled
				m_Event.m_Handled = func(*(T*)&m_Event);
				return true;  // Indicate that the event was successfully dispatched and processed
			}
			return false;  // Event type didn't match, no handler was called
		}

	private:
		Event& m_Event;
	};


	inline std::ostream& operator<<(std::ostream& os, const Event& e) {
		return  os << e.ToString();
	}
}