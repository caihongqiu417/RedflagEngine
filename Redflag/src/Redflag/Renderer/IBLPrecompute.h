#pragma once
// =====================================================================
// IBLPrecompute.h — IBL (Image-Based Lighting) 预计算资源管理器
//
// ────────────────────────────────────────────────────────────────────
//  为什么需要 IBL？
// ────────────────────────────────────────────────────────────────────
//  传统光照只考虑直接光源（方向光 / 点光），场景中的物体被"周围环境"
//  反射的颜色（比如天空、地面）则完全被忽略。这会让金属和光滑表面
//  看起来"假"——因为它们在现实中主要反射的是环境光而不是某一盏灯。
//
//  IBL 的做法：把整张 HDR 环境图当作"无数个方向的光源"，用积分
//  把它们对某个像素的贡献累加起来。
//
// ────────────────────────────────────────────────────────────────────
//  Split Sum 近似（理论核心，对应课上讲义）：
// ────────────────────────────────────────────────────────────────────
//      L_o ≈ ( 预滤波环境贴图 ) × ( BRDF 积分 LUT )
//             └─ 左半：按 roughness 存多个 mip 层级
//             └─ 右半：(NdotV, roughness) 索引的 2D 查找表
//
//  这样原本不可实时计算的积分，变成了两次纹理采样 + 一次乘法。
//
// ────────────────────────────────────────────────────────────────────
//  本类负责的四张 GPU 纹理（Init 时一次性生成）：
// ────────────────────────────────────────────────────────────────────
//    1) EnvCubemap     (512³)  — 把等距柱状 HDR 转成的 cubemap
//    2) IrradianceMap  (32³)   — 漫反射 IBL（半球余弦积分预计算）
//    3) PrefilterMap   (128³)  — 镜面 IBL（5 个 mip，GGX 重要性采样）
//    4) BRDFLUT        (512²)  — 与环境无关的 BRDF 积分查找表
//
// ────────────────────────────────────────────────────────────────────
//  调用位置（见 SceneRenderer / Renderer3D）：
// ────────────────────────────────────────────────────────────────────
//    SceneRenderer::Init()      → IBLPrecompute::Init("env.hdr")
//    Renderer3D::EndScene()     → IBLPrecompute::Bind()
//    SceneRenderer::Shutdown()  → IBLPrecompute::Shutdown()
// =====================================================================

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace Redflag {

	// 预计算生成的四张 GPU 纹理 ID 集合
	struct IBLTextures
	{
		uint32_t EnvCubemap    = 0;  // GL_TEXTURE_CUBE_MAP — 原始环境 cubemap（可用于天空盒）
		uint32_t IrradianceMap = 0;  // GL_TEXTURE_CUBE_MAP — 漫反射辐照度图
		uint32_t PrefilterMap  = 0;  // GL_TEXTURE_CUBE_MAP — 镜面预滤波图（含 mip chain）
		uint32_t BRDFLUT       = 0;  // GL_TEXTURE_2D       — BRDF 积分查找表 (RG16F)
	};

	class IBLPrecompute
	{
	public:
		// 加载 HDR 等距柱状投影图并执行全部预计算，只在 Init 阶段调一次
		// hdrPath 例：  "assets/textures/environment.hdr"
		static void Init(const std::string& hdrPath);

		// 释放所有 GPU 纹理资源
		static void Shutdown();

		// 把 IrradianceMap / PrefilterMap / BRDFLUT 绑定到 slot 7/8/9
		// 在 Renderer3D::EndScene() 的 shader->Bind() 之后调用
		static void Bind();

		// 渲染天空盒（把 EnvCubemap 作为背景绘制）
		// 在所有不透明几何体渲染完之后调用，让它只填充未被覆盖的像素
		static void RenderSkybox(const glm::mat4& view, const glm::mat4& projection);

		// 释放旧资源并用新的 HDR 路径重新初始化，可在运行时动态切换环境图
		static void Reload(const std::string& hdrPath);

		// 是否已成功完成预计算（shader 会根据此标志选择 IBL 或常数回退）
		static bool IsReady();

		// 获取纹理 ID（给需要直接访问原始 GL 资源的模块用，如天空盒渲染）
		static const IBLTextures& GetTextures();
	};

} // namespace Redflag
