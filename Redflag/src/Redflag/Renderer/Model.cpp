#include "rfpch.h"
#include "Model.h"
#include <assimp/pbrmaterial.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <stb_image.h>
#include <glm/glm.hpp>
#include <filesystem>
#include <algorithm>
#include <cctype>
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

			aiString aiMatName;
			aiMat->Get(AI_MATKEY_NAME, aiMatName);
			RF_CORE_INFO("[Model] Material '{0}' (mesh '{1}')", aiMatName.C_Str(), mesh->mName.C_Str());

			// ---- 1. Assimp 材质槽优先 ----
			mat->BaseColorMap = LoadMaterialTexture(aiMat, scene, aiTextureType_DIFFUSE);

			mat->NormalMap = LoadMaterialTexture(aiMat, scene, aiTextureType_NORMALS);
			if (!mat->NormalMap)
				mat->NormalMap = LoadMaterialTexture(aiMat, scene, aiTextureType_HEIGHT);

			mat->MetallicRoughnessMap = LoadMaterialTexture(aiMat, scene, aiTextureType_UNKNOWN);
			if (!mat->MetallicRoughnessMap)
				mat->MetallicRoughnessMap = MergeMetallicRoughnessTextures(aiMat);

			mat->AOMap = LoadMaterialTexture(aiMat, scene, aiTextureType_LIGHTMAP);

			// OPACITY 槽（Assimp 4.x FBX 导入器通常使用此槽存 alpha）
			Ref<Texture2D> opacityTex = LoadMaterialTexture(aiMat, scene, aiTextureType_OPACITY);
			if (opacityTex)
				mat->AlphaMap = opacityTex;

			// ---- 2. Polyhaven/Quixel 目录扫描回退 ----
			// Assimp 4.x FBX 导入器经常丢失 metal/rough/alpha/ao 贴图，
			// 用文件名后缀补齐。
			if (!mat->BaseColorMap)
			{
				std::string p = FindTextureBySuffix({ "_diff", "_basecolor", "_albedo", "_color" });
				if (!p.empty())
				{
					RF_CORE_WARN("[Model]   BaseColor fallback-scan: {0}", p);
					mat->BaseColorMap = Texture2D::Create(p);
				}
			}
			if (!mat->NormalMap)
			{
				std::string p = FindTextureBySuffix({ "_nor_gl", "_nor", "_normal" });
				if (!p.empty())
				{
					RF_CORE_WARN("[Model]   Normal fallback-scan: {0}", p);
					mat->NormalMap = Texture2D::Create(p);
				}
			}
			if (!mat->AOMap)
			{
				std::string p = FindTextureBySuffix({ "_ao", "_ambientocclusion", "_occlusion" });
				if (!p.empty())
				{
					RF_CORE_WARN("[Model]   AO fallback-scan: {0}", p);
					mat->AOMap = Texture2D::Create(p);
				}
			}
			if (!mat->AlphaMap)
			{
				// 注意：故意不对 _alpha / _opacity 文件名做目录扫描回退。
				// Polyhaven 的 *_alpha.png 通常只是"玻璃/钟面"区域的掩膜，
				// 但 FBX 里钟体/指针/玻璃可能共用同一个材质，此时套 Mask 会把
				// 所有子网格里 UV 落在透明区域的片元全部 discard，导致整个钟消失。
				// 只有在 Assimp 材质里明确声明 OPACITY 槽（AlphaMap 已在上面赋值）时才启用。
			}
			if (!mat->MetallicRoughnessMap)
			{
				// 分别找 metal / rough，合并为 MR 贴图
				std::string metalPath = FindTextureBySuffix({ "_metal", "_metallic" });
				std::string roughPath = FindTextureBySuffix({ "_rough", "_roughness" });
				if (!metalPath.empty() || !roughPath.empty())
				{
					RF_CORE_WARN("[Model]   MR fallback-scan: metal='{0}' rough='{1}'",
						metalPath, roughPath);
					mat->MetallicRoughnessMap = MergeMetallicRoughnessFromPaths(metalPath, roughPath);
				}
			}

			// ---- 3. 从 Assimp 材质读取标量参数 ----
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

			// 保留原始 alpha 以便后续识别"玻璃"材质
			float origAlpha = mat->BaseColor.a;

			// 读取 opacity 标量（FBX Standard Surface 玻璃常把 COLOR_DIFFUSE 保持 1.0
			// 但通过 OPACITY 控制透明度）。Assimp 4.x 没有 TRANSPARENCYFACTOR。
			float opacity = 1.0f;
			if (aiMat->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS)
				origAlpha = std::min(origAlpha, opacity);

			// 如果读到的颜色接近纯黑，但有漫反射贴图，则保持白色让贴图正常显示
			// （FBX PBR 材质经常将传统 diffuse color 设为 (0,0,0)）
			if (hasBaseColor && mat->BaseColorMap)
			{
				float luminance = mat->BaseColor.r * 0.299f + mat->BaseColor.g * 0.587f + mat->BaseColor.b * 0.114f;
				if (luminance < 0.01f)
					mat->BaseColor = glm::vec4(1.0f);
			}

			// 所有材质最终统一 alpha=1 参与 PBR 计算；真正的透明信息已保存在 origAlpha 中
			mat->BaseColor.a = 1.0f;

			float metallic = 0.0f;
			if (aiMat->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, metallic) == AI_SUCCESS)
				mat->Metallic = metallic;

			float roughness = 0.5f;
			if (aiMat->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
				mat->Roughness = roughness;

			// ---- 4. 只有在加载到真正的 alpha 贴图时才切 Mask 模式 ----
			// 注意：盲目切 Mask 会导致阈值裁剪误伤模型（例如 FBX 的 BaseColor.a 为脏值时）。
			// 我们在上面已经将 BaseColor.a 强制为 1，所以 AlphaMap 存在才是可靠信号。
			if (mat->AlphaMap)
			{
				mat->Alpha = AlphaMode::Mask;
				mat->AlphaCutoff = 0.5f;
				mat->DoubleSided = true; // 玻璃/镂空物通常两面都可见
			}

			// ---- 5. "玻璃"子网格检测 ----
			// FBX Standard Surface 玻璃材质的判定信号（任一满足都按玻璃处理）：
			//   a) origAlpha < 0.5 （COLOR_DIFFUSE.a 或 OPACITY 明确声明透明）
			//   b) COLOR_TRANSPARENT 非零
			//   c) "玻璃"材质名包含 glass/window/clear/transparent 关键字
			// 当前引擎没有半透明渲染管线，统一跳过渲染；否则玻璃会被当 Opaque
			// 画成实心黑盘遮住内部钟针。
			aiColor4D transparent;
			bool hasTransparentColor = (aiMat->Get(AI_MATKEY_COLOR_TRANSPARENT, transparent) == AI_SUCCESS)
				&& (transparent.r > 0.01f || transparent.g > 0.01f || transparent.b > 0.01f);

			std::string matNameLower = aiMatName.C_Str();
			std::transform(matNameLower.begin(), matNameLower.end(), matNameLower.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });
			bool hasGlassName = matNameLower.find("glass") != std::string::npos
				|| matNameLower.find("window") != std::string::npos
				|| matNameLower.find("clear") != std::string::npos
				|| matNameLower.find("transparent") != std::string::npos;

			bool isLikelyGlass = (origAlpha < 0.5f) || hasTransparentColor || hasGlassName;

			RF_CORE_INFO("[Model]   origAlpha={0:.3f} transparent={1} glassName={2} => isGlass={3}",
				origAlpha, hasTransparentColor, hasGlassName, isLikelyGlass);

			if (isLikelyGlass)
			{
				RF_CORE_WARN("[Model]   Glass-like submesh detected, skip render (mat '{0}')",
					aiMatName.C_Str());
				subMesh.MeshData = nullptr; // DrawModel 中 if (!MeshData) continue 会跳过
			}

			RF_CORE_INFO("[Model]   Maps: base={0} nor={1} mr={2} ao={3} alpha={4}",
				(bool)mat->BaseColorMap, (bool)mat->NormalMap, (bool)mat->MetallicRoughnessMap,
				(bool)mat->AOMap, (bool)mat->AlphaMap);

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

	// ---------------------------------------------------------------------
	// 文件名后缀扫描回退
	//
	// Polyhaven / Quixel 等站点的材质包遵循严格命名约定：
	//   *_diff.jpg        — 基础色 (base color / albedo)
	//   *_nor_gl.exr/png  — OpenGL 法线
	//   *_metal.exr/png   — 金属度
	//   *_rough.exr/png   — 粗糙度
	//   *_ao.exr/png      — 环境遮蔽（如有）
	//   *_alpha.png       — 透明度 / 裁剪掩膜
	//
	// Assimp 4.x 的 FBX 导入器对 Blender Principled BSDF 导出的 PBR 材质
	// 支持不完整（metal/rough/alpha 经常丢失）。这里根据文件名二次扫描模型目录
	// 作为回退，命中率高于 Assimp 的材质槽。
	// ---------------------------------------------------------------------
	std::string Model::FindTextureBySuffix(const std::vector<std::string>& suffixes) const
	{
		namespace fs = std::filesystem;

		// 优先扩展名（stb_image 可读）
		static const std::vector<std::string> kPreferredExts = {
			".png", ".jpg", ".jpeg", ".tga", ".bmp"
		};

		// 收集所有待搜索目录（模型目录 + textures/ 子目录）
		std::vector<fs::path> searchDirs;
		fs::path modelDir(m_Directory);
		if (fs::exists(modelDir) && fs::is_directory(modelDir))
			searchDirs.push_back(modelDir);
		fs::path texDir = modelDir / "textures";
		if (fs::exists(texDir) && fs::is_directory(texDir))
			searchDirs.push_back(texDir);

		auto toLower = [](std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });
			return s;
		};

		// 按优先扩展名顺序遍历，命中即返回
		for (const auto& ext : kPreferredExts)
		{
			for (const auto& dir : searchDirs)
			{
				for (const auto& entry : fs::directory_iterator(dir))
				{
					if (!entry.is_regular_file()) continue;
					std::string fileExt = toLower(entry.path().extension().string());
					if (fileExt != ext) continue;

					std::string stem = toLower(entry.path().stem().string());
					for (const auto& suf : suffixes)
					{
						std::string lowSuf = toLower(suf);
						// 后缀出现在文件名主干中（如 "vintage_clock_metal_4k" 包含 "_metal"）
						if (stem.find(lowSuf) != std::string::npos)
							return entry.path().string();
					}
				}
			}
		}

		return "";
	}

	Ref<Texture2D> Model::MergeMetallicRoughnessTextures(aiMaterial* material)
	{
		// Assimp 4.x 没有 METALNESS(0x15) / DIFFUSE_ROUGHNESS(0x16) 枚举，
		// FBX 导入器会把金属/粗糙度贴图映射到 SHININESS(0x7)。
		// （硬编码 0x15/0x16 在 4.x 下是 INVALID，会被直接跳过）
		std::string metalPath;
		std::string roughPath;

		std::string shininessPath = ResolveTexturePath(material, aiTextureType_SHININESS);
		if (!shininessPath.empty())
			roughPath = shininessPath;

		// SPECULAR 槽在部分 FBX 中被用来装 metallic
		std::string specPath = ResolveTexturePath(material, aiTextureType_SPECULAR);
		if (!specPath.empty())
			metalPath = specPath;

		return MergeMetallicRoughnessFromPaths(metalPath, roughPath);
	}

	Ref<Texture2D> Model::MergeMetallicRoughnessFromPaths(const std::string& metalPath, const std::string& roughPath)
	{
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

