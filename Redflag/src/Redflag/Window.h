#pragma once
#pragma once

#include"rfpch.h"
#include"Redflag/Core.h"
#include"Redflag/Events/Event.h"

namespace Redflag {
	struct WindowProps {
		std::string Title;
		unsigned int Width;
		unsigned int Height;
		WindowProps(const std::string& title = "Redflag Engine", unsigned int width = 1600, unsigned int height = 900)
			:Title(title), Width(width), Height(height) {
		}
	};
	class REDFLAG_API Window {
	public:
		//用EventCallbackFn代替std::function<void(Event&)>：输入为Event&返回值为void 的函数
		using EventCallbackFn = std::function<void(Event&)>;
		virtual ~Window() {}
		//=0为纯虚函数
		virtual void OnUpdate() = 0;
		virtual unsigned int GetWidth() const = 0;
		virtual unsigned int GetHeight() const = 0;

		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		virtual void SetVSync(bool enable) = 0;
		virtual bool IsVSync() const = 0;

		virtual void* GetNativeWindow() const= 0;

		static Window* Create(const WindowProps& props = WindowProps());

	};
}