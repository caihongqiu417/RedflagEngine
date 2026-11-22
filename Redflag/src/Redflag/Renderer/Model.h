#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/Mesh.h"
#include "Redflag/Renderer/Texture.h"
#include "Redflag/Renderer/Material.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <limits>

// 导入模型时才需要 Assimp 的完整类型。
struct aiNode;
struct aiMesh;
struct aiScene;
struct aiMaterial;
struct aiTexture;

namespace Redflag {

	class ModelAnimation;

	// 模型的每个部件保存自己的网格和材质。LocalTransform 把部件顶点从自身坐标变换到模型坐标。
	struct SubMesh
	{
		Ref<Mesh>     MeshData;
		Ref<Material> Mat;
		glm::mat4     LocalTransform{ 1.0f };
	};

	/*
	 * Model 保存导入的网格、材质和贴图。Load 用 Assimp 读文件，还会创建显卡资源，因此要在图形线程上调用。
	 * AssetManager 按资源 ID 保存加载结果，多个实体可以共用同一个模型。
	 */
	class Model
	{
	public:
		// ---- 资源查询 ----
		const std::vector<SubMesh>& GetSubMeshes() const { return m_SubMeshes; }

		/// <summary>模型包围盒投影后的高度占视口的比例。相机穿入包围盒时取最高细节。</summary>
		float GetScreenHeight(const glm::mat4& modelToClip) const;

		// 编辑器可为单个实体替换某个部件的材质。空项继续用模型自带的材质。
		const Ref<Material>& GetMaterial(size_t part, const std::vector<Ref<Material>>& overrides) const
		{
			return part < overrides.size() && overrides[part] ? overrides[part] : m_SubMeshes[part].Mat;
		}

		const std::string& GetFilePath() const { return m_FilePath; }
		const Ref<ModelAnimation>& GetAnimation();

		// ---- 模型导入 ----
		// 导入失败返回空引用，返回对象不保留 Assimp 场景指针。
		static Ref<Model> Load(const std::string& filepath);

	private:
		Model() = default;

		// ---- 节点与几何 ----
		using EmbeddedTextures = std::unordered_map<const aiTexture*, Ref<Texture2D>>;
		SubMesh ProcessMesh(aiMesh* mesh, const aiScene* scene, const glm::mat4& nodeTransform, EmbeddedTextures& textures, bool gltf);

		// ---- 材质贴图 ----
		void LoadLegacyMaps(Material& material, aiMaterial* imported);
		std::string ResolveTexturePath(aiMaterial* material, int textureType);
		Ref<Texture2D> LoadMaterialTexture(const aiScene& scene, aiMaterial* material, int textureType, EmbeddedTextures& textures);
		Ref<Texture2D> MergeMetallicRoughnessTextures(aiMaterial* material);
		Ref<Texture2D> MergeMetallicRoughnessFromPaths(const std::string& metalPath, const std::string& roughPath);

		// 在模型所在目录（含 textures/ 子目录）搜索文件名包含指定后缀（如 "_diff", "_nor", "_metal"）
		// 且扩展名可被 stb_image 读取（.png/.jpg/.tga/.bmp/.jpeg）的文件。优先匹配 .png，再退回 .jpg。
		std::string FindTextureBySuffix(const std::vector<std::string>& suffixes) const;

	private:
		// ---- 模型数据 ----
		std::vector<SubMesh> m_SubMeshes;
		Ref<ModelAnimation> m_Animation;
		bool m_ReadAnimation = false;
		glm::vec3 m_BoundsMin{ std::numeric_limits<float>::max() };
		glm::vec3 m_BoundsMax{ std::numeric_limits<float>::lowest() };
		std::string          m_FilePath;
		std::string          m_Directory; // 模型文件所在目录，用于拼接贴图路径
	};

}
