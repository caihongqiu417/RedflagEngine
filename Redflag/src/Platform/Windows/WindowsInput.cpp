#include "rfpch.h"
#include"WindowsInput.h"

#include"Redflag/Application.h"
#include<GLFW/glfw3.h>

namespace Redflag
{
	//全局初始化单例
	Input* Input::s_Instance = new WindowsInput();

	bool WindowsInput::IsKeyPressedImpl(int keycode)
	{
		
		auto window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow()); //返回glfw窗口
		auto state = glfwGetKey(window, keycode);   //在glfw窗口主动轮询
		return state == GLFW_PRESS || state == GLFW_REPEAT;   
	}
	
	bool WindowsInput::IsMouseButtonPressedImpl(int button)
	{
		auto window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		auto state = glfwGetMouseButton(window, button);
		return state == GLFW_PRESS;
	}
	std::pair<float, float> WindowsInput::GetMousePositionImpl()
	{
		auto window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		double xpos, ypos;
		glfwGetCursorPos(window, &xpos, &ypos);
		return { (float)xpos,(float)ypos };
	}
	float WindowsInput::GetMouseXImpl()
	{
		auto [x, y] = GetMousePositionImpl();
		return x;
	}
	float WindowsInput::GetMouseYImpl()
	{
		auto[x, y] = GetMousePositionImpl();
		return y;
	}
}