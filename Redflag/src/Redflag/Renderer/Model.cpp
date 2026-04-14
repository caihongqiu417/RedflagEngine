#include "rfpch.h"
#include "Model.h"
#include <assimp/pbrmaterial.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <stb_image.h>
#include <glm/glm.hpp>
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

		model->ProcessNode(scene->mRootNode, scene, glm::mat4(1.0f));
		return model;
	}

	void Model::ProcessNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform)
	{
		// Assimp 使用行主序矩阵，glm 使用列主序，需要转置
		const aiMatrix4x4& am = node->mTransformation;
		glm::mat4 localMat(
			am.a1, am.b1, am.c1, am.d1,
			am.a2, am.b2, am.c2, am.d2,
			am.a3, am.b3, am.c3, am.d3,
			am.a4, am.b4, am.c4, am.d4
		);

		// 将本节点的局部变换与父节点累积变换相乘
		glm::mat4 nodeTransform = parentTransform * localMat;

		// 处理本节点的所有网格
		for (uint32_t i = 0; i < node->mNumMeshes; i++)
		{
			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
			m_SubMeshes.push_back(ProcessMesh(mesh, scene, nodeTransform));
		}

		// 递归处理子节点，传入当前累积变换
		for (uint32_t i = 0; i < node->mNumChildren; i++)
			ProcessNode(node->mChildren[i], scene, nodeTransform);
	}

	SubMesh Model::ProcessMesh(aiMesh* mesh, const aiScene* scene, const glm::mat4& nodeTransform)
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

			// 切线向量（由 aiProcess_CalcTangentSpace 计算）
			// 如果无 UV 导致 Assimp 无法计算，则回退到 (1,0,0)
			vertex.Tangent = mesh->HasTangentsAndBitangents()
				? glm::vec3(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z)
				: glm::vec3(1.0f, 0.0f, 0.0f);

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
		subMesh.MeshData       = Mesh::Create(vertices, indices);
		subMesh.LocalTransform = nodeTransform;

		// 加载 PBR 材质贴图
		if (mesh->mMaterialIndex >= 0)
		{
			aiMaterial* aiMat = scene->mMaterials[mesh->mMaterialIndex];
			auto mat = Ref<Material>(new Material());

			// BaseColor / Diffuse 贴图
			mat->BaseColorMap = LoadMaterialTexture(aiMat, scene, aiTextureType_DIFFUSE);

			// 法线贴图
			mat->NormalMap = LoadMaterialTexture(aiMat, scene, aiTextureType_NORMALS);
			if (!mat->NormalMap)
				mat->NormalMap = LoadMaterialTexture(aiMat, scene, aiTextureType_HEIGHT);

			// 金属度粗糙度贴图（glTF: UNKNOWN, FBX: 分离的 METALNESS + DIFFUSE_ROUGHNESS）
			mat->MetallicRoughnessMap = LoadMaterialTexture(aiMat, scene, aiTextureType_UNKNOWN);
			if (!mat->MetallicRoughnessMap)
				mat->MetallicRoughnessMap = MergeMetallicRoughnessTextures(aiMat);

			// 环境遮蔽贴图（Assimp 4.x: LIGHTMAP = Ambient Occlusion）
			mat->AOMap = LoadMaterialTexture(aiMat, scene, aiTextureType_LIGHTMAP);

			// 从 Assimp 材质读取标量参数
			// 优先尝试 glTF PBR base color factor，再回退到传统 diffuse color
			aiColor4D baseColor;
			bool hasBaseColor = false;
			if (aiMat->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR, baseColor) == AI_SUCCESS)
			{
				mat->BaseColor = { baseColor.r, baseColor.g, baseColor.b, baseColor.a };
				hasBaseColor = true;
			}
			else if (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == AI_SUCCESS)
			{
				mat->BaseColor = { baseColor.r, baseColor.g, baseColor.b, baseColor.a };
				hasBaseColor = true;
			}

			// 如果读到的颜色接近纯黑，但有漫反射贴图，则保持白色让贴图正常显示
			// （FBX PBR 材质经常将传统 diffuse color 设为 (0,0,0)）
			if (hasBaseColor && mat->BaseColorMap)
			{
				float luminance = mat->BaseColor.r * 0.299f + mat->BaseColor.g * 0.587f + mat->BaseColor.b * 0.114f;
				if (luminance < 0.01f)
					mat->BaseColor = glm::vec4(1.0f);
			}

			float metallic = 0.0f;
			if (aiMat->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, metallic) == AI_SUCCESS)
				mat->Metallic = metallic;

			float roughness = 0.5f;
			if (aiMat->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
				mat->Roughness = roughness;

			subMesh.Mat = mat;
		}

		return subMesh;
	}

	Ref<Texture2D> Model::LoadMaterialTexture(aiMaterial* material, const aiScene* scene, int textureType)
	{
		std::string fullPath = ResolveTexturePath(material, textureType);
		if (fullPath.empty())
			return nullptr;
		return Texture2D::Create(fullPath);
	}

	std::string Model::ResolveTexturePath(aiMaterial* material, int textureType)
	{
		aiTextureType type = static_cast<aiTextureType>(textureType);
		if (material->GetTextureCount(type) == 0)
			return "";

		aiString texPath;
		if (material->GetTexture(type, 0, &texPath) != AI_SUCCESS)
			return "";

		// 嵌入式纹理（路径以 '*' 开头）暂不支持
		if (texPath.data[0] == '*')
			return "";

		std::string rawPath = texPath.C_Str();

		// 统一路径分隔符
		for (auto& c : rawPath)
			if (c == '\\') c = '/';

		// 尝试 1：直接拼接目录 + 相对路径
		std::string fullPath = m_Directory + "/" + rawPath;

		// 尝试 2：提取文件名在模型目录下搜索
		{
			std::ifstream test(fullPath);
			if (!test.good())
			{
				size_t lastSlash = rawPath.find_last_of('/');
				std::string filename = (lastSlash != std::string::npos)
					? rawPath.substr(lastSlash + 1) : rawPath;

				std::string alt1 = m_Directory + "/" + filename;
				std::ifstream t1(alt1);
				if (t1.good())
				{
					fullPath = alt1;
				}
				else
				{
					std::string alt2 = m_Directory + "/textures/" + filename;
					std::ifstream t2(alt2);
					if (t2.good())
						fullPath = alt2;
					else
						return "";
				}
			}
		}

		// stb_image 不支持 .exr，尝试同名 .png 替代
		{
			size_t dotPos = fullPath.rfind('.');
			if (dotPos != std::string::npos)
			{
				std::string ext = fullPath.substr(dotPos);
				if (ext == ".exr" || ext == ".EXR")
				{
					std::string pngPath = fullPath.substr(0, dotPos) + ".png";
					std::ifstream t(pngPath);
					if (t.good())
						fullPath = pngPath;
				}
			}
		}

		return fullPath;
	}

	Ref<Texture2D> Model::MergeMetallicRoughnessTextures(aiMaterial* material)
	{
		// Assimp 5.x 定义了 aiTextureType_METALNESS(0x15) 和 aiTextureType_DIFFUSE_ROUGHNESS(0x16)
		// 但旧版 Assimp 4.x 没有这些枚举，FBX 导入器会映射到 SHININESS(0x7)
		// 先尝试 Assimp 5.x 枚举值，再回退到 SHININESS
		std::string metalPath = ResolveTexturePath(material, 0x15); // aiTextureType_METALNESS
		std::string roughPath = ResolveTexturePath(material, 0x16); // aiTextureType_DIFFUSE_ROUGHNESS

		// 回退：Assimp 4.x FBX 导入器将金属度/粗糙度映射到 SHININESS
		if (metalPath.empty() && roughPath.empty())
		{
			std::string shininessPath = ResolveTexturePath(material, aiTextureType_SHININESS);
			if (!shininessPath.empty())
			{
				// SHININESS 通常是粗糙度贴图
				roughPath = shininessPath;
			}
		}

		if (metalPath.empty() && roughPath.empty())
			return nullptr;

		stbi_set_flip_vertically_on_load(1);

		int mw = 0, mh = 0, mc = 0, rw = 0, rh = 0, rc = 0;
		stbi_uc* metalData = nullptr;
		stbi_uc* roughData = nullptr;

		if (!metalPath.empty())
			metalData = stbi_load(metalPath.c_str(), &mw, &mh, &mc, 1);
		if (!roughPath.empty())
			roughData = stbi_load(roughPath.c_str(), &rw, &rh, &rc, 1);

		if (!metalData && !roughData)
			return nullptr;

		int width  = metalData ? mw : rw;
		int height = metalData ? mh : rh;

		if (metalData && roughData && (mw != rw || mh != rh))
		{
			RF_CORE_WARN("MergeMetallicRoughness: size mismatch (metal {0}x{1}, rough {2}x{3}), using metal size",
				mw, mh, rw, rh);
			width = mw; height = mh;
		}

		// 合并: R=unused, G=roughness, B=metallic, A=255 (glTF 规范)
		uint32_t pixelCount = width * height;
		std::vector<uint8_t> merged(pixelCount * 4);

		for (uint32_t i = 0; i < (uint32_t)pixelCount; i++)
		{
			merged[i * 4 + 0] = 0;                                        // R
			merged[i * 4 + 1] = roughData ? roughData[i] : (uint8_t)128;  // G = roughness
			merged[i * 4 + 2] = metalData ? metalData[i] : (uint8_t)0;    // B = metallic
			merged[i * 4 + 3] = 255;                                      // A
		}

		auto tex = Texture2D::Create(width, height);
		tex->SetData(merged.data(), pixelCount * 4);

		if (metalData) stbi_image_free(metalData);
		if (roughData) stbi_image_free(roughData);

		RF_CORE_INFO("MergeMetallicRoughness: merged {0}x{1} MR texture from separate maps", width, height);
		return tex;
	}

}

