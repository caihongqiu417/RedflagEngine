#pragma once

#include "Redflag/Core/Core.h"

namespace Redflag {

    class UniformBuffer
    {
    public:
        virtual ~UniformBuffer() {}

        // 向 Buffer 中写入数据
        virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) = 0;

        // 创建函数 (工厂方法)
        static Ref<UniformBuffer> Create(uint32_t size, uint32_t binding);
    };

}