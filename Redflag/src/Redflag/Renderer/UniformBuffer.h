#pragma once

#include "Redflag/Core/Core.h"

namespace Redflag {

    /* 把一组参数一起交给着色器。C++ 写入各字段的顺序和间距要与 GLSL 的声明一致。 */
    class UniformBuffer
    {
    public:
        virtual ~UniformBuffer() {}

        // offset 表示从第几个字节开始写，默认从开头写。
        virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) = 0;

        // size 是要预留的字节数。binding 与 GLSL 声明中的编号相同，着色器靠它找到这份数据。
        static Ref<UniformBuffer> Create(uint32_t size, uint32_t binding);
    };

}