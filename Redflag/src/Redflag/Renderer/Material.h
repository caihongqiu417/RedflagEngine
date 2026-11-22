#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/Texture.h"
#include "Redflag/Renderer/HeightMapping.h"

#include <glm/glm.hpp>

namespace Redflag {

	// ----------
	// 透明度
	// ----------
	enum class AlphaMode : int
	{
		Opaque = 0,   // 完全不透明，忽略 alpha
		Mask   = 1,   // 用于树叶、铁丝网这类有孔洞的表面
		Blend  = 2    // 单独合成半透明表面，保留后面物体的颜色。
	};

	/*
	 * 描述物体表面的颜色、金属度、粗糙度和贴图，Renderer3D 负责把参数传给显卡。
	 * BaseColorMap 与 BaseColor 相乘；金属粗糙度贴图使用 G=粗糙度、B=金属度。
	 * NormalMap 存切线空间法线，AlphaMap 的 R 通道用于透明度裁剪。
	 */
	class Material
	{
	public:
		// ---- 创建与复制 ----
		static Ref<Material> Create(const glm::vec4& color = glm::vec4(1.0f));
		static Ref<Material> Create(const Ref<Texture2D>& texture, const glm::vec4& tint = glm::vec4(1.0f));

		// 给一个实体单独调颜色或粗糙度时复制材质，贴图仍用原来那张。
		Ref<Material> Clone() const;

		// ---- 表面参数 ----
		glm::vec4 BaseColor{ 1.0f };  // 基础颜色因子（RGBA）
		glm::vec3 Emission{ 0.0f };   // 表面自己发出的线性 RGB，可超过 1。

		// ---- 贴图 ----
		Ref<Texture2D> BaseColorMap;           // 漫反射贴图
		Ref<Texture2D> EmissiveMap;            // sRGB 发光图案，乘上 Emission。
		Ref<Texture2D> HeightMap;
		HeightMapping HeightMode = HeightMapping::Off;
		float HeightScale = .02f; // 白处与黑处的高度差，单位为模型局部坐标中的米。
		float HeightReference = 1.f; // 顶点置换时，取到这个灰度的顶点留在原位。
		Ref<Texture2D> NormalMap;              // 切线空间法线贴图
		Ref<Texture2D> MetallicRoughnessMap;   // G=粗糙度, B=金属度
		Ref<Texture2D> AOMap;                  // 环境遮蔽
		Ref<Texture2D> AlphaMap;               // 独立 alpha/opacity 贴图（R 通道）

		// ---- 光照参数 ----
		float Metallic  = 0.0f;   // 金属度 [0, 1]，非金属默认 0
		float Roughness = 0.5f;   // 越接近 0，高光越集中；越接近 1，高光越宽
		float AO        = 1.0f;   // 环境遮蔽强度，1.0 = 无遮蔽

		// ---- 透明度与面朝向 ----
		AlphaMode Alpha       = AlphaMode::Opaque;
		float     AlphaCutoff = 0.5f;   // Mask 模式只画 alpha 大于或等于这个值的像素。
		bool      DoubleSided = false;
		float RefractionIndex = 1.0f; // 空气取 1，普通玻璃约 1.5。
		float Thickness = 0.0f;       // 沿表面法线的厚度，使用场景长度单位。

		// ---- 查询 ----
		bool HasBaseColorMap()          const { return BaseColorMap != nullptr; }
		bool HasNormalMap()             const { return NormalMap != nullptr; }
		bool HasMetallicRoughnessMap()  const { return MetallicRoughnessMap != nullptr; }
		bool HasAOMap()                 const { return AOMap != nullptr; }
		bool HasAlphaMap()              const { return AlphaMap != nullptr; }

		// 让使用同一份材质的绘制排在一起。
		uintptr_t GetSortKey() const { return reinterpret_cast<uintptr_t>(this); }
	};

}
