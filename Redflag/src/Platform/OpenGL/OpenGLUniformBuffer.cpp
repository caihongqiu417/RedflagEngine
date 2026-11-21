#include "rfpch.h"
#include "OpenGLUniformBuffer.h"

#include <glad/glad.h>

namespace Redflag {

    OpenGLUniformBuffer::OpenGLUniformBuffer(uint32_t size, uint32_t binding)
        : m_Binding(binding)
    {
        glCreateBuffers(1, &m_RendererID);
        // 空间一次分配好，之后每帧可以直接改相机矩阵、曝光等参数。
        glNamedBufferData(m_RendererID, size, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, binding, m_RendererID);
    }

    OpenGLUniformBuffer::~OpenGLUniformBuffer()
    {
        glDeleteBuffers(1, &m_RendererID);
    }

    void OpenGLUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
    {
        // 让着色器用这份参数。比如画第二个视口时，用它自己的曝光值。
        glBindBufferBase(GL_UNIFORM_BUFFER, m_Binding, m_RendererID);
        glNamedBufferSubData(m_RendererID, offset, size, data);
    }

}