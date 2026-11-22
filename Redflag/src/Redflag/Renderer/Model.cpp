#include "rfpch.h"
#include "Model.h"
#include "Redflag/Animation/ModelAnimation.h"
#include "ModelNodes.h"
#include "ModelImages.h"
#include "ModelEmission.h"
#include <assimp/pbrmaterial.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <stb_image.h>
#include "tinyexr.h"
#include <glm/glm.hpp>
#include <filesystem>
#include <algorithm>
#include <cctype>
namespace Redflag {

	// ---- 模型导入 ----
	Ref<Model> Model::Load(const std::string& filepath)
	{
		// importer 销毁时会释放 aiScene，下面递归复制顶点、材质和节点变换。
		Assimp::Importer importer;

		std::string extension = std::filesystem::path(filepath).extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		// glTF 缺少 NORMAL 时按面生成。跨过箱子的直角边平滑，会把平墙照出三角形。
		const bool gltf = extension == ".gltf" || extension == ".glb";
		const auto normalFlags = gltf ? aiProcess_GenNormals : (aiProcess_GenSmoothNormals | aiProcess_FixInfacingNormals);
		const aiScene* scene = importer.ReadFile(filepath, aiProcess_Triangulate | aiProcess_CalcTangentSpace | normalFlags);

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

		EmbeddedTextures textures;
		VisitModelMeshes(*scene, *scene->mRootNode, glm::mat4(1.0f), [&](aiMesh& mesh, const glm::mat4& transform) {
			model->m_SubMeshes.push_back(model->ProcessMesh(&mesh, scene, transform, textures, gltf));
			const auto& part = model->m_SubMeshes.back();
			const auto& lo = part.MeshData->GetAABBMin();
			const auto& hi = part.MeshData->GetAABBMax();
			for (int i = 0; i < 8; ++i)
			{
				glm::vec3 p = transform * glm::vec4(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z, 1);
				model->m_BoundsMin = glm::min(model->m_BoundsMin, p);
				model->m_BoundsMax = glm::max(model->m_BoundsMax, p);
			}
		});
		return model;
	}

	const Ref<ModelAnimation>& Model::GetAnimation()
	{
		if (!m_ReadAnimation) {
			m_Animation = ModelAnimation::Load(std::filesystem::u8path(m_FilePath));
			m_ReadAnimation = true;
		}
		return m_Animation;
	}

	// ---- 节点与几何 ----
	float Model::GetScreenHeight(const glm::mat4& modelToClip) const
	{
		float bottom = std::numeric_limits<float>::max(), top = -bottom;
		for (int i = 0; i < 8; ++i)
		{
			const glm::vec4 p = modelToClip * glm::vec4(i & 1 ? m_BoundsMax.x : m_BoundsMin.x,
				i & 2 ? m_BoundsMax.y : m_BoundsMin.y, i & 4 ? m_BoundsMax.z : m_BoundsMin.z, 1);
			// 穿过近裁剪面时透视除法不能给出完整高度，按近景保留原网格。
			if (p.w <= 0 || p.z < -p.w) return std::numeric_limits<float>::max();
			const float y = p.y / p.w;
			bottom = std::min(bottom, y);
			top = std::max(top, y);
		}
		return (top - bottom) * .5f;
	}

	SubMesh Model::ProcessMesh(aiMesh* mesh, const aiScene* scene, const glm::mat4& nodeTransform, EmbeddedTextures& textures, bool gltf)
	{
		// 顶点保留网格局部坐标；节点变换单独存储，绘制时与实体变换组合。
		std::vector<MeshVertex> vertices;
		std::vector<uint32_t>   indices;

		unsigned emissiveUV = 0;
		if (scene->HasMaterials())
			scene->mMaterials[mesh->mMaterialIndex]->Get(
				AI_MATKEY_GLTF_TEXTURE_TEXCOORD(aiTextureType_EMISSIVE, 0), emissiveUV);
		const auto* emissionCoords = emissiveUV && mesh->HasTextureCoords(emissiveUV)
			? mesh->mTextureCoords[emissiveUV] : nullptr;
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

			vertex.TexCoord = mesh->mTextureCoords[0]
				? glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y)
				: glm::vec2(0.0f);
			if (emissionCoords)
				vertex.EmissiveUVOffset = glm::vec2(emissionCoords[i].x, emissionCoords[i].y) - vertex.TexCoord;

			// Assimp 根据贴图坐标计算切线。模型没有切线时，这里用网格的 X 轴方向。
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

		// ---- 材质导入 ----
		if (scene->HasMaterials())
		{
			aiMaterial* aiMat = scene->mMaterials[mesh->mMaterialIndex];
			auto mat = Ref<Material>(new Material());

			aiString aiMatName;
			aiMat->Get(AI_MATKEY_NAME, aiMatName);
			RF_CORE_INFO("[Model] Material '{0}' (mesh '{1}')", aiMatName.C_Str(), mesh->mName.C_Str());

			// ---- 模型文件里指定的贴图 ----
			mat->BaseColorMap = LoadMaterialTexture(*scene, aiMat, aiTextureType_DIFFUSE, textures);
			mat->Emission = GetModelEmission(*aiMat);
			mat->EmissiveMap = LoadMaterialTexture(*scene, aiMat, aiTextureType_EMISSIVE, textures);

			mat->NormalMap = LoadMaterialTexture(*scene, aiMat, aiTextureType_NORMALS, textures);
			if (!mat->NormalMap && aiMat->GetTextureCount(aiTextureType_NORMALS) > 0)
			{
				// 材质写了法线贴图却没加载成功时，在日志里保留模型文件给的路径。
				aiString p; aiMat->GetTexture(aiTextureType_NORMALS, 0, &p);
				RF_CORE_WARN("[Model]   NORMALS slot path not resolved: '{0}'", p.C_Str());
			}

			mat->HeightMap = LoadMaterialTexture(*scene, aiMat, aiTextureType_HEIGHT, textures);
			if (mat->HeightMap)
			{
				// 部分旧 OBJ 在 map_Bump 写的是命名为 _normal 或 _nor_gl 的 RGB 法线图。
				aiString path;
				aiMat->GetTexture(aiTextureType_HEIGHT, 0, &path);
				std::string name = std::filesystem::u8path(path.C_Str()).stem().u8string();
				std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
				if (name.find("_normal") != std::string::npos || name.find("_nor_gl") != std::string::npos)
				{
					if (!mat->NormalMap) mat->NormalMap = mat->HeightMap;
					mat->HeightMap.reset();
				}
				else mat->HeightMode = HeightMapping::Bump;
			}
			if (!mat->HeightMap) mat->HeightMap = LoadMaterialTexture(*scene, aiMat, aiTextureType_DISPLACEMENT, textures);

			mat->MetallicRoughnessMap = LoadMaterialTexture(*scene, aiMat, aiTextureType_UNKNOWN, textures);
			mat->AOMap = LoadMaterialTexture(*scene, aiMat, aiTextureType_LIGHTMAP, textures);

			// 只读取材质显式声明的 OPACITY，不按文件名给共用材质套透明度掩膜。
			Ref<Texture2D> opacityTex = LoadMaterialTexture(*scene, aiMat, aiTextureType_OPACITY, textures);
			if (opacityTex)
				mat->AlphaMap = opacityTex;

			// glTF 的每个材质都指定自己的贴图。只有旧格式才按文件名补找。
			if (!gltf) LoadLegacyMaps(*mat, aiMat);

			// ---- 基础色、金属度与粗糙度 ----
			aiColor4D baseColor;
			if (aiMat->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR, baseColor) == AI_SUCCESS)
			{
				mat->BaseColor = { baseColor.r, baseColor.g, baseColor.b, baseColor.a };
			}
			else if (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == AI_SUCCESS)
			{
				mat->BaseColor = { baseColor.r, baseColor.g, baseColor.b, baseColor.a };
			}

			// glTF 默认把金属度和粗糙度都乘以 1。材质写了 0 时，保留它给出的 0。
			mat->Metallic = gltf || mat->MetallicRoughnessMap ? 1.0f : 0.0f;
			mat->Roughness = gltf || mat->MetallicRoughnessMap ? 1.0f : 0.5f;
			float metallic = 0.0f;
			if (aiMat->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, metallic) == AI_SUCCESS)
				mat->Metallic = metallic;

			float roughness = 0.5f;
			if (aiMat->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
				mat->Roughness = roughness;

			// 树叶贴图中 alpha 低于 AlphaCutoff 的部分留空。玻璃保留每个像素的透明度，稍后和背后的物体一起合成。
			aiString alphaMode;
			if (aiMat->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS)
			{
				const std::string mode = alphaMode.C_Str();
				if (mode == "MASK") mat->Alpha = AlphaMode::Mask;
				if (mode == "BLEND") mat->Alpha = AlphaMode::Blend;
				aiMat->Get(AI_MATKEY_GLTF_ALPHACUTOFF, mat->AlphaCutoff);
				int twoSided = 0;
				aiMat->Get(AI_MATKEY_TWOSIDED, twoSided);
				mat->DoubleSided = twoSided != 0;
			}
			else if (mat->AlphaMap)
			{
				mat->Alpha = AlphaMode::Mask;
				mat->AlphaCutoff = 0.5f;
				mat->DoubleSided = true; // 薄片的正反两面都可能看见。
			}

			RF_CORE_INFO("[Model]   Maps: base={0} nor={1} mr={2} ao={3} alpha={4}",
				(bool)mat->BaseColorMap, (bool)mat->NormalMap, (bool)mat->MetallicRoughnessMap,
				(bool)mat->AOMap, (bool)mat->AlphaMap);

			subMesh.Mat = mat;
		}

		return subMesh;
	}

	// ---- 贴图路径与加载 ----
	Ref<Texture2D> Model::LoadMaterialTexture(const aiScene& scene, aiMaterial* material, int textureType, EmbeddedTextures& textures)
	{
		aiString name;
		if (material->GetTexture(static_cast<aiTextureType>(textureType), 0, &name) != AI_SUCCESS)
			return nullptr;
		if (const auto* embedded = FindModelImage(scene, name))
		{
			auto found = textures.find(embedded);
			if (found != textures.end()) return found->second;
			int w = embedded->mWidth, h = embedded->mHeight, channels;
			std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> bytes(nullptr, stbi_image_free);
			std::vector<glm::u8vec4> rgba;
			if (h == 0)
			{
				// glTF 的 PNG/JPEG 在模型内。翻转行序，与独立文件贴图使用相同的 UV。
				stbi_set_flip_vertically_on_load(1);
				bytes.reset(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(embedded->pcData),
					embedded->mWidth, &w, &h, &channels, STBI_rgb_alpha));
				if (!bytes) return nullptr;
			}
			else
			{
				rgba.resize(size_t(w) * h);
				for (int y = 0; y < h; ++y)
					for (int x = 0; x < w; ++x)
					{
						const aiTexel& c = embedded->pcData[(h - y - 1) * w + x];
						rgba[y*w+x] = {c.r, c.g, c.b, c.a};
					}
			}
			auto texture = Texture2D::Create(w, h);
			texture->SetData(bytes ? static_cast<void*>(bytes.get()) : rgba.data(), uint32_t(size_t(w) * h * 4));
			textures.emplace(embedded, texture);
			return texture;
		}

		std::string fullPath = ResolveTexturePath(material, textureType);
		if (fullPath.empty())
			return nullptr;

		auto tex = Texture2D::Create(fullPath);

		// EXR 加载失败（如 DWAA 压缩不支持），尝试同名 PNG
		if (tex && !tex->IsLoaded())
		{
			size_t dotPos = fullPath.rfind('.');
			if (dotPos != std::string::npos)
			{
				std::string ext = fullPath.substr(dotPos);
				if (ext == ".exr" || ext == ".EXR")
				{
					std::string pngPath = fullPath.substr(0, dotPos) + ".png";
					std::ifstream probe(pngPath);
					if (probe.good())
					{
						RF_CORE_WARN("[Model] EXR unsupported compression, fallback PNG: {0}", pngPath);
						auto fallback = Texture2D::Create(pngPath);
						if (fallback && fallback->IsLoaded())
							return fallback;
					}
				}
			}
			return nullptr;
		}

		return tex;
	}

	std::string Model::ResolveTexturePath(aiMaterial* material, int textureType)
	{
		aiTextureType type = static_cast<aiTextureType>(textureType);
		if (material->GetTextureCount(type) == 0)
			return "";

		aiString texPath;
		if (material->GetTexture(type, 0, &texPath) != AI_SUCCESS)
			return "";

		// 内嵌图像直接从模型读取，这里只解析文件路径。
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

		// EXR 有同名 PNG 时优先用 PNG（Polyhaven EXR 用 DWAA 压缩，tinyexr 不支持）
		{
			size_t dotPos = fullPath.rfind('.');
			if (dotPos != std::string::npos)
			{
				std::string ext = fullPath.substr(dotPos);
				if (ext == ".exr" || ext == ".EXR")
				{
					std::string pngPath = fullPath.substr(0, dotPos) + ".png";
					if (std::ifstream(pngPath).good())
						fullPath = pngPath;
				}
			}
		}

		return fullPath;
	}

	void Model::LoadLegacyMaps(Material& material, aiMaterial* imported)
	{
		if (!material.MetallicRoughnessMap)
			material.MetallicRoughnessMap = MergeMetallicRoughnessTextures(imported);

		if (!material.BaseColorMap)
		{
			std::string p = FindTextureBySuffix({ "_diff", "_basecolor", "_albedo", "_color" });
			if (!p.empty())
			{
				RF_CORE_WARN("[Model]   BaseColor fallback-scan: {0}", p);
				material.BaseColorMap = Texture2D::Create(p);
			}
		}
		if (!material.NormalMap)
		{
			std::string p = FindTextureBySuffix({ "_nor_gl", "_nor", "_normal" });
			if (!p.empty())
			{
				RF_CORE_WARN("[Model]   Normal fallback-scan: {0}", p);
				material.NormalMap = Texture2D::Create(p);
			}
		}
		if (!material.AOMap)
		{
			std::string p = FindTextureBySuffix({ "_ao", "_ambientocclusion", "_occlusion" });
			if (!p.empty())
			{
				RF_CORE_WARN("[Model]   AO fallback-scan: {0}", p);
				material.AOMap = Texture2D::Create(p);
			}
		}
		if (!material.MetallicRoughnessMap)
		{
			// 分别找 metal / rough，合并为 MR 贴图
			std::string metalPath = FindTextureBySuffix({ "_metal", "_metallic" });
			std::string roughPath = FindTextureBySuffix({ "_rough", "_roughness" });
			if (!metalPath.empty() || !roughPath.empty())
			{
				RF_CORE_WARN("[Model]   MR fallback-scan: metal='{0}' rough='{1}'",
					metalPath, roughPath);
				material.MetallicRoughnessMap = MergeMetallicRoughnessFromPaths(metalPath, roughPath);
			}
		}
	}

	// 模型没写明贴图路径时，从模型目录和 textures 子目录按名称查找，例如用 _rough 找粗糙度图。
	std::string Model::FindTextureBySuffix(const std::vector<std::string>& suffixes) const
	{
		namespace fs = std::filesystem;

		// 同时找到 PNG 和 JPG 时优先用 PNG。
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

		// 按列表里的格式顺序找贴图，找到第一张文件名匹配的就返回。
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

	// ---- 金属粗糙度通道合并 ----
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

		auto isExr = [](const std::string& p) {
			return p.size() > 4 &&
				(p.compare(p.size() - 4, 4, ".exr") == 0 ||
				 p.compare(p.size() - 4, 4, ".EXR") == 0);
		};

		stbi_set_flip_vertically_on_load(1);

		int mw = 0, mh = 0, rw = 0, rh = 0;
		const stbi_uc* metalData = nullptr;
		const stbi_uc* roughData = nullptr;
		// stb 像素用 stbi_image_free 释放，EXR 转换结果由下面的 vector 持有。
		stbi_uc* metalStbi = nullptr;
		stbi_uc* roughStbi = nullptr;
		std::vector<uint8_t> metalEXR, roughEXR;

		auto loadEXRGray = [](const std::string& path, int& w, int& h, std::vector<uint8_t>& buf) -> bool
		{
			float* exrData = nullptr; const char* err = nullptr;
			if (LoadEXR(&exrData, &w, &h, path.c_str(), &err) != TINYEXR_SUCCESS)
			{
				if (err) FreeEXRErrorMessage(err);
				return false;
			}
			buf.resize(w * h);
			for (int i = 0; i < w * h; ++i)
				buf[i] = (uint8_t)std::min(255, (int)(exrData[i * 4] * 255.0f)); // R channel (grayscale)
			free(exrData);
			return true;
		};

		if (!metalPath.empty())
		{
			if (isExr(metalPath)) {
				if (loadEXRGray(metalPath, mw, mh, metalEXR)) metalData = metalEXR.data();
			} else {
				int mc = 0;
				metalStbi = stbi_load(metalPath.c_str(), &mw, &mh, &mc, 1);
				metalData = metalStbi;
			}
		}
		if (!roughPath.empty())
		{
			if (isExr(roughPath)) {
				if (loadEXRGray(roughPath, rw, rh, roughEXR)) roughData = roughEXR.data();
			} else {
				int rc = 0;
				roughStbi = stbi_load(roughPath.c_str(), &rw, &rh, &rc, 1);
				roughData = roughStbi;
			}
		}

		if (!metalData && !roughData)
		{
			if (metalStbi) stbi_image_free(metalStbi);
			if (roughStbi) stbi_image_free(roughStbi);
			return nullptr;
		}

		// 使用现有输入的尺寸。两张图尺寸不同时拒绝合并，避免按同一索引越界读取。
		int width  = metalData ? mw : rw;
		int height = metalData ? mh : rh;

		if (metalData && roughData && (mw != rw || mh != rh))
		{
			RF_CORE_WARN("MergeMetallicRoughness: size mismatch (metal {0}x{1}, rough {2}x{3}), skipping texture",
				mw, mh, rw, rh);
			if (metalStbi) stbi_image_free(metalStbi);
			if (roughStbi) stbi_image_free(roughStbi);
			return nullptr;
		}

		// 打包为 RGBA8：R=0，G=粗糙度，B=金属度，A=255。
		// 缺粗糙度图用 128/255，缺金属图用 0；SetData 复制后即可释放 CPU 像素。
		uint32_t pixelCount = (uint32_t)(width * height);
		std::vector<uint8_t> merged(pixelCount * 4);

		for (uint32_t i = 0; i < pixelCount; i++)
		{
			merged[i * 4 + 0] = 0;
			merged[i * 4 + 1] = roughData ? roughData[i] : (uint8_t)128;  // G = roughness
			merged[i * 4 + 2] = metalData ? metalData[i] : (uint8_t)0;    // B = metallic
			merged[i * 4 + 3] = 255;
		}

		auto tex = Texture2D::Create(width, height);
		tex->SetData(merged.data(), pixelCount * 4);

		if (metalStbi) stbi_image_free(metalStbi);
		if (roughStbi) stbi_image_free(roughStbi);

		RF_CORE_INFO("MergeMetallicRoughness: merged {0}x{1} MR texture from separate maps", width, height);
		return tex;
	}

}

