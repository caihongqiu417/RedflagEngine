#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include"Redflag/Core/Core.h"
#include"Redflag/Events/Event.h"

namespace Redflag {
	class GraphicsContext;
	struct WindowProps {
		std::string Title;
		uint32_t Width;
		uint32_t Height;
		bool Visible = true;
		WindowProps(const std::string& title = "Redflag Engine", 
					uint32_t width = 2560, uint32_t height = 1440)
					:Title(title), Width(width), Height(height) 
				{
				}
	};
	class REDFLAG_API Window {
	public:
		//用EventCallbackFn代替std::function<void(Event&)>：输入为Event&返回值为void 的函数
		using EventCallbackFn = std::function<void(Event&)>;
		virtual ~Window() {}
		//=0为纯虚函数
		virtual void OnUpdate() = 0;
		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;

		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		virtual void SetVSync(bool enable) = 0;
		virtual bool IsVSync() const = 0;

		virtual GraphicsContext& GetGraphicsContext() const = 0;
		virtual void* GetNativeWindow() const= 0;

		static Window* Create(const WindowProps& props = WindowProps());

	};
}