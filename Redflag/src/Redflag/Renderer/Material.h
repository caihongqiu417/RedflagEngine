#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/Texture.h"

#include <glm/glm.hpp>

namespace Redflag {

	// ----------
	// AlphaMode — 透明度处理方式
	// ----------
	enum class AlphaMode : int
	{
		Opaque = 0,   // 完全不透明，忽略 alpha
		Mask   = 1,   // Alpha 裁剪（alpha < cutoff 则 discard）
		Blend  = 2    // 半透明混合
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

		// 复制参数到独立材质，贴图仍共享；用于实体单独修改材质参数。
		Ref<Material> Clone() const;

		// ---- 表面参数 ----
		glm::vec4 BaseColor{ 1.0f };  // 基础颜色因子（RGBA）

		// ---- 贴图 ----
		Ref<Texture2D> BaseColorMap;           // 漫反射贴图
		Ref<Texture2D> NormalMap;              // 切线空间法线贴图
		Ref<Texture2D> MetallicRoughnessMap;   // G=粗糙度, B=金属度
		Ref<Texture2D> AOMap;                  // 环境遮蔽
		Ref<Texture2D> AlphaMap;               // 独立 alpha/opacity 贴图（R 通道）

		// ---- 光照参数 ----
		float Metallic  = 0.0f;   // 金属度 [0, 1]，非金属默认 0
		float Roughness = 0.5f;   // 粗糙度 [0, 1]
		float AO        = 1.0f;   // 环境遮蔽强度，1.0 = 无遮蔽

		// ---- 透明度与面朝向 ----
		AlphaMode Alpha       = AlphaMode::Opaque;
		float     AlphaCutoff = 0.5f;   // Mask 模式下的裁剪阈值
		bool      DoubleSided = false;

		// ---- 查询 ----
		bool HasBaseColorMap()          const { return BaseColorMap != nullptr; }
		bool HasNormalMap()             const { return NormalMap != nullptr; }
		bool HasMetallicRoughnessMap()  const { return MetallicRoughnessMap != nullptr; }
		bool HasAOMap()                 const { return AOMap != nullptr; }
		bool HasAlphaMap()              const { return AlphaMap != nullptr; }

		// 排序键：以材质对象指针为主键，确保不同材质各自上传 UBO
		uintptr_t GetSortKey() const { return reinterpret_cast<uintptr_t>(this); }
	};

}
