#include "rfpch.h"
#include "Material.h"

namespace Redflag {

	// ---- 材质创建与复制 ----

	Ref<Material> Material::Create(const glm::vec4& color)
	{
		Ref<Material> mat = Ref<Material>(new Material());
		mat->BaseColor = color;
		return mat;
	}

	Ref<Material> Material::Create(const Ref<Texture2D>& texture, const glm::vec4& tint)
	{
		Ref<Material> mat = Ref<Material>(new Material());
		mat->BaseColorMap = texture;
		mat->BaseColor    = tint;
		return mat;
	}

	Ref<Material> Material::Clone() const
	{
		Ref<Material> mat = Ref<Material>(new Material());
		mat->BaseColor             = BaseColor;
		// 复制共享引用，不重新创建纹理或上传像素。
		mat->BaseColorMap          = BaseColorMap;
		mat->NormalMap             = NormalMap;
		mat->MetallicRoughnessMap  = MetallicRoughnessMap;
		mat->AOMap                 = AOMap;
		mat->AlphaMap              = AlphaMap;
		mat->Metallic              = Metallic;
		mat->Roughness             = Roughness;
		mat->AO                    = AO;
		mat->Alpha                 = Alpha;
		mat->AlphaCutoff           = AlphaCutoff;
		mat->DoubleSided           = DoubleSided;
		return mat;
	}

}
