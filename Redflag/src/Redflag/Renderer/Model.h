#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Renderer/Mesh.h"
#include "Redflag/Renderer/Texture.h"

#include <string>
#include <vector>

// 前向声明 Assimp 类型，避免头文件污染
struct aiNode;
struct aiMesh;
struct aiScene;
struct aiMaterial;

namespace Redflag {

	// 一个模型由多个子网格（SubMesh）组成
	// 每个 SubMesh 携带自己的漫反射贴图（可为 nullptr）
	struct SubMesh
	{
		Ref<Mesh>      MeshData;
		Ref<Texture2D> DiffuseTexture;
	};

	// -----------------------------------------------------------------------
	// Model
	//
	// 使用 Assimp 从文件加载一个 3D 模型，支持多子网格。
	// 获取漫反射贴图时会在模型文件所在目录下查找。
	//
	// 推荐通过 AssetManager 获取，避免重复加载：
	//   auto model = AssetManager::GetOrLoad<Model>("assets/models/gun.obj");
	//
	// 也可以直接调用：
	//   auto model = Model::Load("assets/models/gun.obj");
	// -----------------------------------------------------------------------
	class Model
	{
	public:
		const std::vector<SubMesh>& GetSubMeshes() const { return m_SubMeshes; }
		const std::string& GetFilePath() const { return m_FilePath; }

		// 从文件路径加载并构建 Model（AssetManager 约定接口）
		static Ref<Model> Load(const std::string& filepath);

	private:
		Model() = default;

		void ProcessNode(aiNode* node, const aiScene* scene);
		SubMesh ProcessMesh(aiMesh* mesh, const aiScene* scene);
		Ref<Texture2D> LoadMaterialTexture(aiMaterial* material, const aiScene* scene, int textureType);

	private:
		std::vector<SubMesh> m_SubMeshes;
		std::string          m_FilePath;
		std::string          m_Directory; // 模型文件所在目录，用于拼接贴图路径
	};

}
