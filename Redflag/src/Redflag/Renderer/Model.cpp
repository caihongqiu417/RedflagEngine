#include "rfpch.h"
#include "Model.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace Redflag {

	Ref<Model> Model::Load(const std::string& filepath)
	{
		Assimp::Importer importer;

		const aiScene* scene = importer.ReadFile(filepath,
			aiProcess_Triangulate      |
			aiProcess_GenSmoothNormals |
			aiProcess_CalcTangentSpace
		);

		if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
		{
			RF_CORE_ERROR("Assimp failed to load model '{0}': {1}", filepath, importer.GetErrorString());
			return nullptr;
		}

		Ref<Model> model = Ref<Model>(new Model());
		model->m_FilePath  = filepath;

		// 提取目录路径，供加载贴图时拼接子路径使用
		size_t lastSlash = filepath.find_last_of("/\\");
		model->m_Directory = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash) : ".";

		model->ProcessNode(scene->mRootNode, scene);
		return model;
	}

	void Model::ProcessNode(aiNode* node, const aiScene* scene)
	{
		// 处理本节点的所有网格
		for (uint32_t i = 0; i < node->mNumMeshes; i++)
		{
			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
			m_SubMeshes.push_back(ProcessMesh(mesh, scene));
		}

		// 递归处理子节点
		for (uint32_t i = 0; i < node->mNumChildren; i++)
			ProcessNode(node->mChildren[i], scene);
	}

	SubMesh Model::ProcessMesh(aiMesh* mesh, const aiScene* scene)
	{
		std::vector<MeshVertex> vertices;
		std::vector<uint32_t>   indices;

		vertices.reserve(mesh->mNumVertices);

		for (uint32_t i = 0; i < mesh->mNumVertices; i++)
		{
			MeshVertex vertex;

			vertex.Position = {
				mesh->mVertices[i].x,
				mesh->mVertices[i].y,
				mesh->mVertices[i].z
			};

			vertex.Normal = mesh->HasNormals()
				? glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z)
				: glm::vec3(0.0f, 1.0f, 0.0f);

			// Assimp 支持最多 8 组 UV，只取第 0 组
			vertex.TexCoord = mesh->mTextureCoords[0]
				? glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y)
				: glm::vec2(0.0f);

			vertices.push_back(vertex);
		}

		// 每个面已被 aiProcess_Triangulate 确保为三角形
		for (uint32_t i = 0; i < mesh->mNumFaces; i++)
		{
			const aiFace& face = mesh->mFaces[i];
			for (uint32_t j = 0; j < face.mNumIndices; j++)
				indices.push_back(face.mIndices[j]);
		}

		SubMesh subMesh;
		subMesh.MeshData = Mesh::Create(vertices, indices);

		// 加载漫反射贴图
		if (mesh->mMaterialIndex >= 0)
		{
			aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
			subMesh.DiffuseTexture = LoadMaterialTexture(material, scene, aiTextureType_DIFFUSE);
		}

		return subMesh;
	}

	Ref<Texture2D> Model::LoadMaterialTexture(aiMaterial* material, const aiScene* scene, int textureType)
	{
		aiTextureType type = static_cast<aiTextureType>(textureType);
		if (material->GetTextureCount(type) == 0)
			return nullptr;

		aiString texPath;
		if (material->GetTexture(type, 0, &texPath) != AI_SUCCESS)
			return nullptr;

		// 嵌入式纹理（路径以 '*' 开头）暂不支持，跳过
		if (texPath.data[0] == '*')
		{
			RF_CORE_WARN("Model::LoadMaterialTexture: embedded textures are not supported yet, skipping.");
			return nullptr;
		}

		// 外部纹理文件：拼接目录 + 相对路径
		std::string fullPath = m_Directory + "/" + texPath.C_Str();
		return Texture2D::Create(fullPath);
	}

}
