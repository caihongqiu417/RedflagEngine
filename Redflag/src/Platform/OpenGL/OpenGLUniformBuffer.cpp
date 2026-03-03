#include "rfpch.h"
#include "OpenGLUniformBuffer.h"

#include <glad/glad.h>

namespace Redflag {

    OpenGLUniformBuffer::OpenGLUniformBuffer(uint32_t size, uint32_t binding)
    {
        glCreateBuffers(1, &m_RendererID);
        // 使用 GL_DYNAMIC_DRAW 因为我们会频繁更新它 (每一帧)
        glNamedBufferData(m_RendererID, size, nullptr, GL_DYNAMIC_DRAW);
        // 绑定到指定的 binding point (比如 binding=0 对应 Camera)
        glBindBufferBase(GL_UNIFORM_BUFFER, binding, m_RendererID);
    }

    OpenGLUniformBuffer::~OpenGLUniformBuffer()
    {
        glDeleteBuffers(1, &m_RendererID);
    }

    void OpenGLUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
    {
        // 更新 Buffer 数据
        glNamedBufferSubData(m_RendererID, offset, size, data);
    }

}