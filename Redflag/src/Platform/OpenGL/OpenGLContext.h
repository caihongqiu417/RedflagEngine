#pragma once
#include"Redflag/Renderer/GraphicsContext.h"
#include "Redflag/Log.h"

struct GLFWwindow;

namespace Redflag {
    class OpenGLContext : public GraphicsContext {
    public:
        OpenGLContext(GLFWwindow* windowHandle);
        virtual void Init() override;
        virtual void SwapBuffers() override;
    private:
        //º¬ÓÐglfw´°¿Ú¾ä±ú
        GLFWwindow* m_WindowHandle;
    };
}