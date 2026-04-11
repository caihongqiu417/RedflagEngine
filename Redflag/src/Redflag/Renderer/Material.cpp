#include "rfpch.h"
#include "Material.h"

namespace Redflag {

	std::unordered_map<std::string, Ref<Material>> MaterialLibrary::s_Materials;

	// --------------- Material -----------------------------------------------

	Ref<Material> Material::Create(const glm::vec4& color)
	{
		Ref<Material> mat = Ref<Material>(new Material());
		mat->m_Color = color;
		return mat;
	}

	Ref<Material> Material::Create(const Ref<Texture2D>& texture, const glm::vec4& tint)
	{
		Ref<Material> mat = Ref<Material>(new Material());
		mat->m_Texture = texture;
		mat->m_Color   = tint;
		return mat;
	}

	// --------------- MaterialLibrary ----------------------------------------

	Ref<Material> MaterialLibrary::GetOrCreate(const std::string& name, const Ref<Texture2D>& texture, const glm::vec4& tint)
	{
		auto it = s_Materials.find(name);
		if (it != s_Materials.end())
			return it->second;

		auto mat = Material::Create(texture, tint);
		s_Materials[name] = mat;
		return mat;
	}

	Ref<Material> MaterialLibrary::GetOrCreate(const std::string& name, const glm::vec4& color)
	{
		auto it = s_Materials.find(name);
		if (it != s_Materials.end())
			return it->second;

		auto mat = Material::Create(color);
		s_Materials[name] = mat;
		return mat;
	}

	bool MaterialLibrary::Exists(const std::string& name)
	{
		return s_Materials.find(name) != s_Materials.end();
	}

	Ref<Material> MaterialLibrary::Get(const std::string& name)
	{
		RF_CORE_ASSERT(Exists(name), "Material '{0}' not found in MaterialLibrary", name);
		return s_Materials.at(name);
	}

}
