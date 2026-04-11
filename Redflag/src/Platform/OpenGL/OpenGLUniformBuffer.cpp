#include "rfpch.h"
#include "OpenGLUniformBuffer.h"

#include <glad/glad.h>

namespace Redflag {

    OpenGLUniformBuffer::OpenGLUniformBuffer(uint32_t size, uint32_t binding)
        : m_Binding(binding)
    {
        glCreateBuffers(1, &m_RendererID);
        // ʹ�� GL_DYNAMIC_DRAW ��Ϊ���ǻ�Ƶ�������� (ÿһ֡)
        glNamedBufferData(m_RendererID, size, nullptr, GL_DYNAMIC_DRAW);
        // �󶨵�ָ���� binding point (���� binding=0 ��Ӧ Camera)
        glBindBufferBase(GL_UNIFORM_BUFFER, binding, m_RendererID);
    }

    OpenGLUniformBuffer::~OpenGLUniformBuffer()
    {
        glDeleteBuffers(1, &m_RendererID);
    }

    void OpenGLUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
    {
        // ȷ�� binding point ָ��������ĵ�ǰ buffer
        glBindBufferBase(GL_UNIFORM_BUFFER, m_Binding, m_RendererID);
        // ���� Buffer ����
        glNamedBufferSubData(m_RendererID, offset, size, data);
    }

}