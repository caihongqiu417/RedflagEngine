#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/Texture.h"

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace Redflag {

	// -----------------------------------------------------------------------
	// AlphaMode — 透明度处理方式
	// -----------------------------------------------------------------------
	enum class AlphaMode : int
	{
		Opaque = 0,   // 完全不透明，忽略 alpha
		Mask   = 1,   // Alpha 裁剪（alpha < cutoff 则 discard）
		Blend  = 2    // 半透明混合
	};

	// -----------------------------------------------------------------------
	// Material — PBR Metallic-Roughness 材质
	//
	// 遵循 glTF 2.0 材质模型：
	//   BaseColor × BaseColorMap          — 表面漫反射颜色
	//   Metallic / Roughness              — 金属度 / 粗糙度标量
	//   MetallicRoughnessMap              — R = 无用, G = 粗糙度, B = 金属度
	//   NormalMap                         — 切线空间法线贴图
	//   AOMap                             — 环境遮蔽（可选）
	//   EmissiveFactor × EmissiveMap      — 自发光（可选，先留空）
	//
	// 向后兼容：
	//   旧的 Create(color) / Create(texture, tint) 接口保留不变，
	//   只设置 BaseColor / BaseColorMap，其余保持默认值。
	// -----------------------------------------------------------------------
	class Material
	{
	public:
		// --- 保留旧接口，向后兼容 ---
		static Ref<Material> Create(const glm::vec4& color = glm::vec4(1.0f));
		static Ref<Material> Create(const Ref<Texture2D>& texture, const glm::vec4& tint = glm::vec4(1.0f));

		// --- PBR 参数 ---
		glm::vec4 BaseColor{ 1.0f };  // 基础颜色因子（RGBA）

		Ref<Texture2D> BaseColorMap;           // 漫反射贴图
		Ref<Texture2D> NormalMap;              // 切线空间法线贴图
		Ref<Texture2D> MetallicRoughnessMap;   // G=粗糙度, B=金属度
		Ref<Texture2D> AOMap;                  // 环境遮蔽

		float Metallic  = 0.0f;   // 金属度 [0, 1]，非金属默认 0
		float Roughness = 0.5f;   // 粗糙度 [0, 1]
		float AO        = 1.0f;   // 环境遮蔽强度，1.0 = 无遮蔽

		AlphaMode Alpha       = AlphaMode::Opaque;
		float     AlphaCutoff = 0.5f;   // Mask 模式下的裁剪阈值
		bool      DoubleSided = false;

		// --- 查询 ---
		bool HasBaseColorMap()          const { return BaseColorMap != nullptr; }
		bool HasNormalMap()             const { return NormalMap != nullptr; }
		bool HasMetallicRoughnessMap()  const { return MetallicRoughnessMap != nullptr; }
		bool HasAOMap()                 const { return AOMap != nullptr; }

		// 向后兼容旧代码：GetTexture() = BaseColorMap, GetColor() = BaseColor
		const Ref<Texture2D>& GetTexture() const { return BaseColorMap; }
		const glm::vec4&      GetColor()   const { return BaseColor; }
		bool HasTexture() const { return BaseColorMap != nullptr; }

		// 排序键：以 BaseColorMap 指针为主键
		uintptr_t GetSortKey() const { return reinterpret_cast<uintptr_t>(BaseColorMap.get()); }

	private:
		// 旧字段已合并到 PBR 参数中，不再单独保留
	};

	// -----------------------------------------------------------------------
	// MaterialLibrary — 以名字为索引的材质注册表
	// -----------------------------------------------------------------------
	class MaterialLibrary
	{
	public:
		static Ref<Material> GetOrCreate(const std::string& name, const Ref<Texture2D>& texture, const glm::vec4& tint = glm::vec4(1.0f));
		static Ref<Material> GetOrCreate(const std::string& name, const glm::vec4& color);

		static bool          Exists(const std::string& name);
		static Ref<Material> Get(const std::string& name);

	private:
		static std::unordered_map<std::string, Ref<Material>> s_Materials;
	};

}
