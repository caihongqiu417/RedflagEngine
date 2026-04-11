#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/Texture.h"

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace Redflag {

	// -----------------------------------------------------------------------
	// Material
	//
	// 封装一个渲染材质：漫反射纹理 + 颜色叠加。
	// 用于 SubMesh 和 Renderer3D 的批次排序键。
	//
	// 用法：
	//   auto mat = Material::Create(texture);            // 纹理材质
	//   auto mat = Material::Create(glm::vec4(1,0,0,1)); // 纯色材质
	// -----------------------------------------------------------------------
	class Material
	{
	public:
		static Ref<Material> Create(const glm::vec4& color = glm::vec4(1.0f));
		static Ref<Material> Create(const Ref<Texture2D>& texture, const glm::vec4& tint = glm::vec4(1.0f));

		const Ref<Texture2D>& GetTexture() const { return m_Texture; }
		const glm::vec4&      GetColor()   const { return m_Color; }

		bool HasTexture() const { return m_Texture != nullptr; }

		// 排序键：以纹理指针为主键（nullptr = 纯色），整数值唯一标识一种状态组合
		uintptr_t GetSortKey() const { return reinterpret_cast<uintptr_t>(m_Texture.get()); }

	private:
		Ref<Texture2D> m_Texture;
		glm::vec4      m_Color{ 1.0f };
	};

	// -----------------------------------------------------------------------
	// MaterialLibrary
	//
	// 以名字为索引的材质注册表，避免重复创建相同材质。
	// -----------------------------------------------------------------------
	class MaterialLibrary
	{
	public:
		// 注册或获取（若已存在则返回已有的）
		static Ref<Material> GetOrCreate(const std::string& name, const Ref<Texture2D>& texture, const glm::vec4& tint = glm::vec4(1.0f));
		static Ref<Material> GetOrCreate(const std::string& name, const glm::vec4& color);

		static bool          Exists(const std::string& name);
		static Ref<Material> Get(const std::string& name);

	private:
		static std::unordered_map<std::string, Ref<Material>> s_Materials;
	};

}
