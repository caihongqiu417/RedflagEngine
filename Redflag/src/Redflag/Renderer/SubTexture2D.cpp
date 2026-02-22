#include"rfpch.h"
#include"SubTexture2D.h"

namespace Redflag
{
	SubTexture2D::SubTexture2D(const Ref<Texture2D>& texture, const glm::vec2& min, const glm::vec2& max)
		: m_Texture(texture)
	{
		m_TexCoords[0] = { min.x, min.y };
		m_TexCoords[1] = { max.x, min.y };
		m_TexCoords[2] = { max.x, max.y };
		m_TexCoords[3] = { min.x, max.y };
	}

    /// <summary>
    /// 工厂方法：通过网格坐标系统创建子纹理，用于从精灵表中获取单个精灵
    /// </summary>
    /// <param name="texture">精灵表纹理</param>
    /// <param name="coords">精灵在网格中的坐标（如 {7,6}）</param>
    /// <param name="cellSize">每个网格单元的大小（如 {128,128}）</param>
    /// <param name="spriteSize">精灵占用的网格单元数（默认 {1,1}）</param>
    /// <returns>子纹理对象</returns>
    Ref<SubTexture2D> SubTexture2D::CreateFromCoords(
        const Ref<Texture2D>& texture,
        const glm::vec2& coords,
        const glm::vec2& cellSize,
        const glm::vec2& spriteSize)
    {
        // 计算纹理坐标范围：网格坐标 → 像素坐标 → 纹理坐标
        glm::vec2 min = {
            (coords.x * cellSize.x) / texture->GetWidth(),   // 左下角x
            (coords.y * cellSize.y) / texture->GetHeight()   // 左下角y
        };

        glm::vec2 max = {
            ((coords.x + spriteSize.x) * cellSize.x) / texture->GetWidth(),   // 右上角x
            ((coords.y + spriteSize.y) * cellSize.y) / texture->GetHeight()   // 右上角y
        };

        return CreateRef<SubTexture2D>(texture, min, max);
    }
}