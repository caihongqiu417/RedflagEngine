// OpenGLContext.cpp
#include "rfpch.h"

// ȷ��GLFW��glad֮ǰ����
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include "OpenGLContext.h"

namespace Redflag {
    OpenGLContext::OpenGLContext(GLFWwindow* windowHandle)
        : m_WindowHandle(windowHandle) {
        RF_CORE_ASSERT(windowHandle, "Window handle is null!");
    }

    void OpenGLContext::Init()
    {
        RF_PROFILE_FUNCTION();

        glfwMakeContextCurrent(m_WindowHandle);

        // ��ʼ��GLAD
        int status = gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress));
        RF_CORE_ASSERT(status, "Failed to initialize Glad!");

        // ���OpenGL��Ϣ
        RF_CORE_INFO("OpenGL Info:");
        RF_CORE_INFO("  Vendor: {0}", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
        RF_CORE_INFO("  Renderer: {0}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        RF_CORE_INFO("  Version: {0}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

        // 获取窗口的帧缓冲区尺寸并设置OpenGL视口
        int width, height;
        glfwGetFramebufferSize(m_WindowHandle, &width, &height);
        RF_CORE_INFO("Framebuffer Size: {0}x{1}", width, height);
        glViewport(0, 0, width, height);
    }

    void OpenGLContext::SwapBuffers()
    {
        RF_PROFILE_FUNCTION();

        glfwSwapBuffers(m_WindowHandle);
    }
}