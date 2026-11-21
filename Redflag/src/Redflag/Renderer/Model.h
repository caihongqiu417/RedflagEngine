#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/Mesh.h"
#include "Redflag/Renderer/Texture.h"
#include "Redflag/Renderer/Material.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

// 前向声明 Assimp 类型，避免头文件污染
struct aiNode;
struct aiMesh;
struct aiScene;
struct aiMaterial;

namespace Redflag {

	// 一个模型由多个子网格（SubMesh）组成
	// 每个 SubMesh 携带自己的材质（可为 nullptr，回退到纯白色）
	// LocalTransform 为该节点在模型本地空间中的累积变换（从根节点乘到当前节点）
	struct SubMesh
	{
		Ref<Mesh>     MeshData;
		Ref<Material> Mat;
		glm::mat4     LocalTransform{ 1.0f };
	};

	/*
	 * 把模型文件转换成可绘制的子网格和材质，并持有这些对象及其贴图。
	 * 顶点保留网格局部坐标，节点累积变换存入 SubMesh，绘制时再乘实体世界变换。
	 * Load 用 Assimp 读取文件并创建 GPU 缓冲和纹理，应在图形线程调用；AssetManager 按 ID 复用结果。
	 */
	class Model
	{
	public:
		// ---- 资源查询 ----
		const std::vector<SubMesh>& GetSubMeshes() const { return m_SubMeshes; }
		const std::string& GetFilePath() const { return m_FilePath; }

		// ---- 模型导入 ----
		// 导入失败返回空引用，返回对象不保留 Assimp 场景指针。
		static Ref<Model> Load(const std::string& filepath);

	private:
		Model() = default;

		// ---- 节点与几何 ----
		void ProcessNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform /* 父节点到模型空间 */);
		SubMesh ProcessMesh(aiMesh* mesh, const aiScene* scene, const glm::mat4& nodeTransform);
		// ---- 材质贴图 ----
		std::string ResolveTexturePath(aiMaterial* material, int textureType);
		Ref<Texture2D> LoadMaterialTexture(aiMaterial* material, int textureType);
		Ref<Texture2D> MergeMetallicRoughnessTextures(aiMaterial* material);
		Ref<Texture2D> MergeMetallicRoughnessFromPaths(const std::string& metalPath, const std::string& roughPath);

		// 在模型所在目录（含 textures/ 子目录）搜索文件名包含指定后缀（如 "_diff", "_nor", "_metal"）
		// 且扩展名可被 stb_image 读取（.png/.jpg/.tga/.bmp/.jpeg）的文件。优先匹配 .png，再退回 .jpg。
		std::string FindTextureBySuffix(const std::vector<std::string>& suffixes) const;

	private:
		// ---- 模型数据 ----
		std::vector<SubMesh> m_SubMeshes;
		std::string          m_FilePath;
		std::string          m_Directory; // 模型文件所在目录，用于拼接贴图路径
	};

}
