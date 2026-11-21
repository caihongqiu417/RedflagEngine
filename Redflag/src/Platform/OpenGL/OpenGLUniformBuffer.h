#pragma once

#include "Redflag/Renderer/UniformBuffer.h"

namespace Redflag {

    /* 在显存中保存一组着色器参数，例如相机矩阵或后处理用的曝光值。 */
    class OpenGLUniformBuffer : public UniformBuffer
    {
    public:
        OpenGLUniformBuffer(uint32_t size, uint32_t binding);
        virtual ~OpenGLUniformBuffer();

        virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0 /* 字节偏移 */) override;

    private:
        uint32_t m_RendererID = 0;
        uint32_t m_Binding    = 0;
    };
}