// =====================================================================
// IBLPrecompute.cpp — IBL 预计算实现
//
// ────────────────────────────────────────────────────────────────────
//  为什么全部用原生 OpenGL，不经过引擎的 Shader/Framebuffer 抽象？
// ────────────────────────────────────────────────────────────────────
//    1) 预计算需要渲染到 Cubemap 的 6 个面和各 mip 层级，
//       引擎现有的 OpenGLFramebuffer 只支持 2D 颜色附件，改造成本高。
//    2) 预计算 shader 只在 Init 时编译运行一次，生命周期极短，
//       不需要走 shaderc/SPIR-V 缓存管线。
//    3) 内嵌 shader 源码避免了文件路径依赖和额外的资源管理。
//
// ────────────────────────────────────────────────────────────────────
//  五步流水线（Init 中顺序执行）：
// ────────────────────────────────────────────────────────────────────
//    Step 1: 读 HDR → GL_TEXTURE_2D (RGB16F)
//    Step 2: 等距柱状 → Environment Cubemap (512×512)
//    Step 3: Environment → Irradiance Map (32×32, 漫反射半球积分)
//    Step 4: Environment → Pre-filtered Map (128×128, 5 mip, GGX)
//    Step 5: BRDF LUT (512×512, RG16F, 与环境无关)
// =====================================================================

#include "rfpch.h"
#include "IBLPrecompute.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>

#include <cmath>

namespace Redflag {

	// =================================================================
	// 文件级静态数据
	// =================================================================
	static IBLTextures s_IBL;         // 四张输出纹理
	static bool        s_Ready = false;

	// 预计算过程中临时使用的 GL 对象（Init 末尾会全部删除）
	static uint32_t s_CaptureFBO = 0; // 离屏 FBO（颜色附件动态切换到 cubemap 各面）
	static uint32_t s_CaptureRBO = 0; // 深度 RBO（每步尺寸不同时重新分配）
	static uint32_t s_CubeVAO = 0, s_CubeVBO = 0;  // 单位立方体（渲染 6 面 & 天空盒用，生命周期长）
	static uint32_t s_QuadVAO = 0, s_QuadVBO = 0;  // 全屏四边形（BRDF LUT 用）

	// 天空盒 shader（Init 中编译，一直保留到 Shutdown）
	static uint32_t s_SkyboxProgram = 0;
	static int      s_SkyboxLocView = -1;
	static int      s_SkyboxLocProj = -1;

	// =================================================================
	// 单位立方体顶点数据（36 顶点 = 12 三角形 = 6 面）
	// 只含位置属性；从立方体中心向外看时，顶点局部坐标本身
	// 就是对应 cubemap 面上那个 texel 的采样方向。
	// =================================================================
	static float s_CubeVerts[] = {
		// back
		-1, -1, -1,  -1,  1, -1,   1,  1, -1,
		 1,  1, -1,   1, -1, -1,  -1, -1, -1,
		// front
		-1, -1,  1,   1, -1,  1,   1,  1,  1,
		 1,  1,  1,  -1,  1,  1,  -1, -1,  1,
		// left
		-1,  1,  1,  -1,  1, -1,  -1, -1, -1,
		-1, -1, -1,  -1, -1,  1,  -1,  1,  1,
		// right
		 1,  1,  1,   1, -1, -1,   1,  1, -1,
		 1, -1, -1,   1,  1,  1,   1, -1,  1,
		// bottom
		-1, -1, -1,   1, -1, -1,   1, -1,  1,
		 1, -1,  1,  -1, -1,  1,  -1, -1, -1,
		// top
		-1,  1, -1,  -1,  1,  1,   1,  1,  1,
		 1,  1,  1,   1,  1, -1,  -1,  1, -1,
	};

	// 全屏四边形（NDC 坐标 + UV，三角带 4 顶点）
	static float s_QuadVerts[] = {
		// pos      uv
		-1,  1,   0, 1,
		-1, -1,   0, 0,
		 1,  1,   1, 1,
		 1, -1,   1, 0,
	};

	// =================================================================
	// 从原点向 ±X / ±Y / ±Z 看的 6 个 view 矩阵
	// 注意 up 向量的选择要匹配 OpenGL cubemap 规范：
	//   +Y / -Y 面的 up 必须是 ±Z（不是常规的 Y 轴！）
	// =================================================================
	static glm::mat4 s_Proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
	static glm::mat4 s_Views[] = {
		glm::lookAt(glm::vec3(0), glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)), // +X
		glm::lookAt(glm::vec3(0), glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)), // -X
		glm::lookAt(glm::vec3(0), glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)), // +Y
		glm::lookAt(glm::vec3(0), glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)), // -Y
		glm::lookAt(glm::vec3(0), glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)), // +Z
		glm::lookAt(glm::vec3(0), glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)), // -Z
	};

	// =====================================================================
	// 内嵌 Shader 源码
	// =====================================================================

	// ---- 共用立方体顶点着色器：把局部坐标原样输出作为 cubemap 采样方向 ----
	static const char* s_CubeVS = R"(
#version 450 core
layout(location=0) in vec3 a_Pos;
uniform mat4 u_Proj;
uniform mat4 u_View;
out vec3 v_LocalPos;
void main(){
    v_LocalPos = a_Pos;                                  // 片段里用来做 cubemap 采样
    gl_Position = u_Proj * u_View * vec4(a_Pos, 1.0);
}
)";

	// ─────────────────────────────────────────────────────────────────
	// Step 2 片元着色器：等距柱状投影 → Cubemap
	//
	//   每个 cubemap texel 对应一个 3D 方向 v
	//   → 转换成球面坐标 (经度 φ, 纬度 θ)
	//   → 映射到 HDR 等距柱状图的 UV 进行采样
	// ─────────────────────────────────────────────────────────────────
	static const char* s_EquirectFS = R"(
#version 450 core
in vec3 v_LocalPos;
out vec4 FragColor;
uniform sampler2D u_HDR;

const vec2 invAtan = vec2(0.1591, 0.3183);  // (1/(2π), 1/π)

vec2 SampleSphericalMap(vec3 v){
    // atan(z,x) → 经度  [-π, π]  ×  1/(2π)  → [-0.5, 0.5]
    // asin(y)   → 纬度  [-π/2, π/2] × 1/π    → [-0.5, 0.5]
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;                              // 平移到 [0, 1]
    return uv;
}

void main(){
    vec2 uv = SampleSphericalMap(normalize(v_LocalPos));
    FragColor = vec4(texture(u_HDR, uv).rgb, 1.0);
}
)";

	// ─────────────────────────────────────────────────────────────────
	// Step 3 片元着色器：辐照度卷积 (Irradiance Convolution)
	//
	//   对应课上"平均入射光强度"（漫反射部分）
	//
	//   对每个法线 N，在其上半球做余弦加权积分：
	//       E(N) = ∫_Ω  L_i(ω)·cos(θ)·dω
	//
	//   离散化：用均匀步长遍历 (φ, θ)，每次采样 cubemap 后
	//   乘 cos(θ)·sin(θ) 再累加（sin(θ) 是球面面积微元雅可比校正）
	// ─────────────────────────────────────────────────────────────────
	static const char* s_IrradianceFS = R"(
#version 450 core
in vec3 v_LocalPos;
out vec4 FragColor;
uniform samplerCube u_EnvMap;
const float PI = 3.14159265359;

void main(){
    vec3 N = normalize(v_LocalPos);           // 当前 texel 的方向 = 法线
    vec3 irr = vec3(0.0);

    // 以 N 为中心构建切线坐标系（up / right / N）
    vec3 up    = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up         = normalize(cross(N, right));

    float delta = 0.025;                      // 步长越小越准，0.025 ≈ 2500 样本
    float count = 0.0;

    // φ ∈ [0, 2π)：绕 N 旋转一圈
    // θ ∈ [0, π/2)：从 N 到赤道
    for(float phi = 0.0; phi < 2.0*PI; phi += delta){
        for(float theta = 0.0; theta < 0.5*PI; theta += delta){
            // 球面坐标 → 切线空间笛卡尔
            vec3 ts = vec3(sin(theta)*cos(phi),
                           sin(theta)*sin(phi),
                           cos(theta));
            // 切线空间 → 世界空间
            vec3 w = ts.x*right + ts.y*up + ts.z*N;

            // cos(θ) = Lambert 余弦权重
            // sin(θ) = 球面积分的面积微元校正
            irr += texture(u_EnvMap, w).rgb * cos(theta) * sin(theta);
            count++;
        }
    }
    // 归一化：π 来自 Lambert BRDF 的 1/π 与积分 2π 相消后的余项
    irr = PI * irr / count;
    FragColor = vec4(irr, 1.0);
}
)";

	// ─────────────────────────────────────────────────────────────────
	// Step 4 片元着色器：预滤波环境贴图 (Pre-filter, GGX)
	//
	//   对应课上"借鉴 MipMap 思想对 pre-filtering 图像的采样"
	//
	//   Split Sum 左半（镜面部分）：
	//     对每个反射方向 R，按 GGX NDF 做重要性采样
	//     roughness 越大 → 样本越散 → 结果越模糊
	//     每个 roughness 对应一个 mip 层级
	//
	//   关键简化：假设 V = R = N
	//     这样预计算只依赖 roughness，不依赖观察方向
	// ─────────────────────────────────────────────────────────────────
	static const char* s_PrefilterFS = R"(
#version 450 core
in vec3 v_LocalPos;
out vec4 FragColor;
uniform samplerCube u_EnvMap;
uniform float u_Roughness;
uniform float u_EnvMapSize;   // 源 cubemap 单面分辨率（例如 512.0）

const float PI = 3.14159265359;
const uint SAMPLES = 1024u;

// Van der Corput 序列：整数二进制位镜像翻转 → [0,1) 准随机数
float VdC(uint bits){
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
vec2 Hammersley(uint i, uint N){ return vec2(float(i)/float(N), VdC(i)); }

// GGX NDF（用于计算采样 PDF）
float DistributionGGX(float NdH, float rough){
    float a = rough*rough;
    float a2 = a*a;
    float denom = NdH*NdH*(a2-1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

vec3 SampleGGX(vec2 Xi, vec3 N, float rough){
    float a = rough*rough;
    float phi = 2.0*PI*Xi.x;
    float cosT = sqrt((1.0-Xi.y)/(1.0+(a*a-1.0)*Xi.y));
    float sinT = sqrt(1.0-cosT*cosT);
    vec3 H = vec3(cos(phi)*sinT, sin(phi)*sinT, cosT);
    vec3 up = abs(N.z)<0.999 ? vec3(0,0,1) : vec3(1,0,0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);
    return normalize(T*H.x + B*H.y + N*H.z);
}

void main(){
    vec3 N = normalize(v_LocalPos);
    vec3 R = N;
    vec3 V = R;

    float totalW = 0.0;
    vec3  color  = vec3(0.0);

    // 每个 texel 对应的立体角（cubemap 一个面的立体角 = 4π/6，除以 texel 数）
    float saTexel = 4.0 * PI / (6.0 * u_EnvMapSize * u_EnvMapSize);

    for(uint i=0u; i<SAMPLES; ++i){
        vec2 Xi = Hammersley(i, SAMPLES);
        vec3 H  = SampleGGX(Xi, N, u_Roughness);
        vec3 L  = normalize(2.0*dot(V,H)*H - V);

        float NdL = max(dot(N,L), 0.0);
        if(NdL > 0.0){
            // ===== Filtered Importance Sampling（消除 firefly 噪点）=====
            //
            // 原理：GGX 重要性采样的 PDF 在某些方向上非常稀疏，
            //       单个 HDR pixel（如太阳）可能被几个样本采到，造成高亮噪点。
            // 解法：根据样本的 PDF 算出一个"等价于多少个 texel"的立体角，
            //       再用 0.5·log2(saSample/saTexel) 得到 mip 层级。
            //       PDF 越低（样本越稀疏）→ mip 越高 → 采样更平滑的 mipmap
            //       达到无偏降噪效果（Chetan Jags / Epic Games UE4 方案）
            float NdH = max(dot(N, H), 0.0);
            float HdV = max(dot(H, V), 0.0);
            float D   = DistributionGGX(NdH, u_Roughness);
            // GGX 重要性采样的 PDF（乘以 Jacobian 4*HdV 得到 L 空间的 PDF）
            float pdf = (D * NdH / (4.0 * HdV)) + 0.0001;
            float saSample = 1.0 / (float(SAMPLES) * pdf + 0.0001);

            // roughness=0 时直接用 mip 0（纯镜面反射不需要模糊）
            float mipLevel = u_Roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);

            color  += textureLod(u_EnvMap, L, mipLevel).rgb * NdL;
            totalW += NdL;
        }
    }
    FragColor = vec4(color / totalW, 1.0);
}
)";

	// ---- 全屏四边形顶点着色器（BRDF LUT 用）----
	static const char* s_QuadVS = R"(
#version 450 core
layout(location=0) in vec2 a_Pos;
layout(location=1) in vec2 a_UV;
out vec2 v_UV;
void main(){
    v_UV = a_UV;
    gl_Position = vec4(a_Pos, 0.0, 1.0);
}
)";

	// ─────────────────────────────────────────────────────────────────
	// Step 5 片元着色器：BRDF 积分 LUT
	//
	//   对应课上右侧 Schlick 近似展开后的两项积分
	//
	//   把 F(θ) = F0 + (1-F0)(1-cosθ)⁵ 代入渲染方程后可拆成：
	//
	//     R 通道 (scale) = ∫ (f_r/F) · [1-(1-cosθ)⁵] · cosθ dω
	//     G 通道 (bias)  = ∫ (f_r/F) · (1-cosθ)⁵     · cosθ dω
	//
	//   运行时合成：
	//     specular = prefiltered × (F0 * lut.r + lut.g)
	//
	//   这张 LUT 与环境贴图无关，只取决于 BRDF 模型，
	//   所以全局只算一次，所有场景共享。
	//
	//   注意几何项：用的是 IBL 版 k = α²/2，
	//   区别于直接光照的 k = (α+1)²/8
	//   原因：IBL 入射光来自整个半球，需要更强的几何遮蔽
	// ─────────────────────────────────────────────────────────────────
	static const char* s_BRDFFS = R"(
#version 450 core
in vec2 v_UV;
out vec4 FragColor;
const float PI = 3.14159265359;
const uint SAMPLES = 1024u;

float VdC(uint bits){
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
vec2 Hammersley(uint i, uint N){ return vec2(float(i)/float(N), VdC(i)); }

vec3 SampleGGX(vec2 Xi, vec3 N, float rough){
    float a = rough*rough;
    float phi = 2.0*PI*Xi.x;
    float cosT = sqrt((1.0-Xi.y)/(1.0+(a*a-1.0)*Xi.y));
    float sinT = sqrt(1.0-cosT*cosT);
    vec3 H = vec3(cos(phi)*sinT, sin(phi)*sinT, cosT);
    vec3 up = abs(N.z)<0.999 ? vec3(0,0,1) : vec3(1,0,0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);
    return normalize(T*H.x + B*H.y + N*H.z);
}

// IBL 几何项（Schlick-GGX 单方向），k = α²/2
float G1(float NdV, float rough){
    float k = (rough*rough) / 2.0;
    return NdV / (NdV*(1.0-k) + k);
}
// Smith 几何（组合视线 + 光线）
float GSmith(vec3 N, vec3 V, vec3 L, float rough){
    return G1(max(dot(N,V),0.0), rough) * G1(max(dot(N,L),0.0), rough);
}

// 核心积分：对给定 (NdV, rough)，计算 Fresnel scale 和 bias
vec2 IntegrateBRDF(float NdV, float rough){
    // 构造 V 在 XZ 平面上，与 N=(0,0,1) 夹角余弦为 NdV
    vec3 V = vec3(sqrt(1.0-NdV*NdV), 0.0, NdV);
    vec3 N = vec3(0, 0, 1);

    float A = 0.0, B = 0.0;

    for(uint i=0u; i<SAMPLES; ++i){
        vec2 Xi = Hammersley(i, SAMPLES);
        vec3 H  = SampleGGX(Xi, N, rough);
        vec3 L  = normalize(2.0*dot(V,H)*H - V);

        float NdL = max(L.z, 0.0);
        float NdH = max(H.z, 0.0);
        float VdH = max(dot(V,H), 0.0);

        if(NdL > 0.0){
            float G     = GSmith(N, V, L, rough);
            // 从 Cook-Torrance 分母中抵消 F 后剩下的"可见性项"
            float G_Vis = (G * VdH) / (NdH * NdV);
            float Fc    = pow(1.0 - VdH, 5.0);   // Schlick 的 (1-cosθ)⁵

            // 按课上公式拆分：
            //   A 累加 (1-Fc)·G_Vis   → 对应 1-(1-cosθ)⁵ → scale
            //   B 累加     Fc ·G_Vis   → 对应   (1-cosθ)⁵ → bias
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return vec2(A, B) / float(SAMPLES);
}

void main(){
    // UV.x = NdotV，UV.y = roughness
    FragColor = vec4(IntegrateBRDF(v_UV.x, v_UV.y), 0.0, 1.0);
}
)";

	// ─────────────────────────────────────────────────────────────────
	// 天空盒顶点着色器
	//   · 用立方体顶点的局部坐标作为采样方向 v_LocalPos
	//   · 从 view 矩阵里"去掉平移"——让天空盒永远跟着相机走，
	//     这样无论相机怎么移动，天空背景保持不动
	//   · gl_Position.z = gl_Position.w 让透视除法后深度 = 1.0
	//     (配合 glDepthFunc(GL_LEQUAL)) 让天空盒恰好落在"最远处"，
	//     只填充没有被不透明几何覆盖的像素，不会挡住前景
	// ─────────────────────────────────────────────────────────────────
	static const char* s_SkyboxVS = R"(
#version 450 core
layout(location=0) in vec3 a_Pos;
uniform mat4 u_View;        // 已经去掉平移的 view 矩阵 (mat4(mat3(view)))
uniform mat4 u_Projection;
out vec3 v_LocalPos;
void main(){
    v_LocalPos = a_Pos;
    vec4 clip  = u_Projection * u_View * vec4(a_Pos, 1.0);
    gl_Position = clip.xyww;   // z = w → NDC.z = 1.0（最远处）
}
)";

	// ─────────────────────────────────────────────────────────────────
	// 天空盒片元着色器
	//   · 直接对 EnvCubemap 采样
	//   · 做和 PBR shader 一样的 Reinhard tone map + gamma，
	//     保证天空亮度与场景物体匹配（不然 HDR 天空会"太亮发白"）
	// ─────────────────────────────────────────────────────────────────
	static const char* s_SkyboxFS = R"(
#version 450 core
in  vec3 v_LocalPos;
out vec4 FragColor;
uniform samplerCube u_EnvMap;
void main(){
    // textureLod(..., 0) 强制采样 mip 0（原始分辨率）
    // 不能用 texture()，它会根据屏幕空间导数自动选高 mip，导致天空盒模糊
    vec3 color = textureLod(u_EnvMap, normalize(v_LocalPos), 0.0).rgb;
    // Reinhard 色调映射 + gamma 2.2（与主 PBR shader 保持一致）
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));
    FragColor = vec4(color, 1.0);
}
)";

	// =====================================================================
	// 工具函数
	// =====================================================================

	// 原生 OpenGL 编译并链接一个 program（简化版，不做缓存）
	static uint32_t Compile(const char* vs, const char* fs)
	{
		auto compileStage = [](GLenum type, const char* src) -> uint32_t {
			uint32_t s = glCreateShader(type);
			glShaderSource(s, 1, &src, nullptr);
			glCompileShader(s);
			int ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
			if (!ok) {
				char log[1024];
				glGetShaderInfoLog(s, 1024, nullptr, log);
				RF_CORE_ERROR("IBL shader compile: {0}", log);
			}
			return s;
		};
		uint32_t v = compileStage(GL_VERTEX_SHADER,   vs);
		uint32_t f = compileStage(GL_FRAGMENT_SHADER, fs);
		uint32_t p = glCreateProgram();
		glAttachShader(p, v);
		glAttachShader(p, f);
		glLinkProgram(p);
		int ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
		if (!ok) {
			char log[1024];
			glGetProgramInfoLog(p, 1024, nullptr, log);
			RF_CORE_ERROR("IBL shader link: {0}", log);
		}
		glDeleteShader(v);
		glDeleteShader(f);
		return p;
	}

	static void MakeCubeVAO()
	{
		glGenVertexArrays(1, &s_CubeVAO);
		glGenBuffers(1, &s_CubeVBO);
		glBindVertexArray(s_CubeVAO);
		glBindBuffer(GL_ARRAY_BUFFER, s_CubeVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(s_CubeVerts), s_CubeVerts, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
		glBindVertexArray(0);
	}

	static void MakeQuadVAO()
	{
		glGenVertexArrays(1, &s_QuadVAO);
		glGenBuffers(1, &s_QuadVBO);
		glBindVertexArray(s_QuadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, s_QuadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(s_QuadVerts), s_QuadVerts, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
		glBindVertexArray(0);
	}

	static void DrawCube()
	{
		glBindVertexArray(s_CubeVAO);
		glDrawArrays(GL_TRIANGLES, 0, 36);
		glBindVertexArray(0);
	}

	static void DrawQuad()
	{
		glBindVertexArray(s_QuadVAO);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glBindVertexArray(0);
	}

	// 把 cubemap 的 6 个面依次附加到 FBO 并渲染（通用流程）
	// 调用前提：已 glUseProgram 并设置好 u_Proj
	static void RenderCubeFaces(uint32_t program, uint32_t cubeTex, int size, int mipLevel = 0)
	{
		glViewport(0, 0, size, size);
		glBindFramebuffer(GL_FRAMEBUFFER, s_CaptureFBO);
		for (uint32_t i = 0; i < 6; ++i)
		{
			glUniformMatrix4fv(
				glGetUniformLocation(program, "u_View"),
				1, GL_FALSE, &s_Views[i][0][0]);
			// 把 cubemap 的第 i 面（在指定 mip 层）设为颜色输出
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubeTex, mipLevel);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			DrawCube();
		}
	}

	// =====================================================================
	// Init — 执行全部预计算
	// =====================================================================
	void IBLPrecompute::Init(const std::string& hdrPath)
	{
		// ---- 保存当前 GL 状态（结束时恢复）----
		// 因为预计算会切换 FBO / Viewport，必须不能影响外部流程
		GLint prevFBO = 0, prevVP[4] = { 0, 0, 0, 0 };
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
		glGetIntegerv(GL_VIEWPORT, prevVP);

		// ---- 创建辅助几何体 ----
		MakeCubeVAO();
		MakeQuadVAO();

		// EnvCubemap 单面分辨率：提高此值可获得更清晰的天空盒（预计算耗时同步增加）
		constexpr int ENV_SIZE = 2048;

		// ---- 创建离屏 FBO + 深度 RBO ----
		glGenFramebuffers(1, &s_CaptureFBO);
		glGenRenderbuffers(1, &s_CaptureRBO);
		glBindFramebuffer(GL_FRAMEBUFFER, s_CaptureFBO);
		glBindRenderbuffer(GL_RENDERBUFFER, s_CaptureRBO);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, ENV_SIZE, ENV_SIZE);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			GL_RENDERBUFFER, s_CaptureRBO);

		// ==============================================================
		// Step 1: 加载 HDR 等距柱状投影图
		// ==============================================================
		stbi_set_flip_vertically_on_load(true);
		int w = 0, h = 0, ch = 0;
		float* pixels = stbi_loadf(hdrPath.c_str(), &w, &h, &ch, 0);
		if (!pixels)
		{
			RF_CORE_ERROR("IBL: Failed to load HDR: {0}", hdrPath);
			// 失败也要恢复状态
			glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
			glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
			return;
		}
		RF_CORE_INFO("IBL: Loaded HDR {0}x{1} from {2}", w, h, hdrPath);

		// ─── 钳制 HDR 像素到有限范围 ───
		// 真实 HDR（如 Poly Haven）太阳位置的亮度可能是 +Inf 或极大值。
		// 这些像素经过 mipmap 下采样 → 生成 NaN；再被 irradiance/prefilter
		// 卷积后整张贴图都变成 NaN → 最终模型表面全是 NaN 污染（显示为黑 / 乱色）。
		// 标准做法：预先钳制到一个合理上限（这里用 50.0，与 UE4/Filament 一致）
		const int pixelCount = w * h * ch;
		for (int i = 0; i < pixelCount; ++i)
		{
			float v = pixels[i];
			if (!std::isfinite(v))       v = 0.0f;   // inf/NaN 清零
			else if (v > 50.0f)          v = 50.0f;  // 极亮像素削顶（防止 firefly）
			else if (v < 0.0f)           v = 0.0f;   // 负值清零（HDR 文件可能有噪声）
			pixels[i] = v;
		}

		uint32_t hdrTex = 0;
		glGenTextures(1, &hdrTex);
		glBindTexture(GL_TEXTURE_2D, hdrTex);
		// HDR 数据必须用浮点内部格式才能保留 > 1.0 的高动态范围信息
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		stbi_image_free(pixels);

		// ==============================================================
		// Step 2: 等距柱状 → Environment Cubemap
		// ==============================================================
		glGenTextures(1, &s_IBL.EnvCubemap);
		glBindTexture(GL_TEXTURE_CUBE_MAP, s_IBL.EnvCubemap);
		for (int i = 0; i < 6; ++i)
		{
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
				ENV_SIZE, ENV_SIZE, 0, GL_RGB, GL_FLOAT, nullptr);
		}
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		// 注意：先用 LINEAR（非 mipmap）创建，否则此刻纹理不完整，
		// 后续作为 FBO 颜色附件渲染或作为源被其它阶段采样时会返回黑。
		// 等 Step 2 末尾 glGenerateMipmap 之后再切到 LINEAR_MIPMAP_LINEAR。
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		uint32_t prog = Compile(s_CubeVS, s_EquirectFS);
		glUseProgram(prog);
		glUniform1i(glGetUniformLocation(prog, "u_HDR"), 0);
		glUniformMatrix4fv(glGetUniformLocation(prog, "u_Proj"), 1, GL_FALSE, &s_Proj[0][0]);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, hdrTex);

		glBindRenderbuffer(GL_RENDERBUFFER, s_CaptureRBO);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, ENV_SIZE, ENV_SIZE);
		RenderCubeFaces(prog, s_IBL.EnvCubemap, ENV_SIZE);
		glDeleteProgram(prog);
		glDeleteTextures(1, &hdrTex);   // HDR 原图已经用完

		// 给环境 cubemap 生成 mipmap，供 prefilter 阶段按 PDF 采样使用
		// 这是消除 GGX 重要性采样 firefly 噪点的关键
		glBindTexture(GL_TEXTURE_CUBE_MAP, s_IBL.EnvCubemap);
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
		// mipmap 已存在，现在才能安全切到 mipmap 过滤（供 Step 4 prefilter 使用 textureLod）
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

		RF_CORE_INFO("IBL: Environment cubemap generated ({0}, mipmapped)", ENV_SIZE);

		// ==============================================================
		// Step 3: Irradiance Map (32×32)
		//
		// 为什么尺寸这么小？
		//   辐照度是整个半球的积分结果，变化极其平滑，
		//   32×32 足以表示所有方向的漫反射环境光。
		// ==============================================================
		glGenTextures(1, &s_IBL.IrradianceMap);
		glBindTexture(GL_TEXTURE_CUBE_MAP, s_IBL.IrradianceMap);
		for (int i = 0; i < 6; ++i)
		{
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
				32, 32, 0, GL_RGB, GL_FLOAT, nullptr);
		}
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		prog = Compile(s_CubeVS, s_IrradianceFS);
		glUseProgram(prog);
		glUniform1i(glGetUniformLocation(prog, "u_EnvMap"), 0);
		glUniformMatrix4fv(glGetUniformLocation(prog, "u_Proj"), 1, GL_FALSE, &s_Proj[0][0]);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_CUBE_MAP, s_IBL.EnvCubemap);

		glBindRenderbuffer(GL_RENDERBUFFER, s_CaptureRBO);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 32, 32);
		RenderCubeFaces(prog, s_IBL.IrradianceMap, 32);
		glDeleteProgram(prog);
		RF_CORE_INFO("IBL: Irradiance map generated (32)");

		// ==============================================================
		// Step 4: Pre-filtered Map (128×128, 5 mip levels)
		//
		//   mip 0 (128³)  → roughness = 0.00  （镜面反射）
		//   mip 1 (64³)   → roughness = 0.25
		//   mip 2 (32³)   → roughness = 0.50
		//   mip 3 (16³)   → roughness = 0.75
		//   mip 4 (8³)    → roughness = 1.00  （完全粗糙）
		//
		//   对应课上："对不同 glossy 程度采样不同滤波层级的 EnvMap"
		// ==============================================================
		const int PF_SIZE  = 128;
		const int MIP_CNT  = 5;

		glGenTextures(1, &s_IBL.PrefilterMap);
		glBindTexture(GL_TEXTURE_CUBE_MAP, s_IBL.PrefilterMap);
		for (int i = 0; i < 6; ++i)
		{
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
				PF_SIZE, PF_SIZE, 0, GL_RGB, GL_FLOAT, nullptr);
		}
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		// 必须启用 MIPMAP 过滤，因为运行时要按 roughness 插值 mip 层
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		// 让驱动分配 mip 存储空间（会同时生成初始 mip 数据，稍后被覆盖）
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

		prog = Compile(s_CubeVS, s_PrefilterFS);
		glUseProgram(prog);
		glUniform1i(glGetUniformLocation(prog, "u_EnvMap"), 0);
		glUniformMatrix4fv(glGetUniformLocation(prog, "u_Proj"), 1, GL_FALSE, &s_Proj[0][0]);
		// 告诉 shader 源 cubemap 每面分辨率（用于 filtered importance sampling 的 mip 计算）
		glUniform1f(glGetUniformLocation(prog, "u_EnvMapSize"), (float)ENV_SIZE);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_CUBE_MAP, s_IBL.EnvCubemap);

		glBindFramebuffer(GL_FRAMEBUFFER, s_CaptureFBO);
		for (int mip = 0; mip < MIP_CNT; ++mip)
		{
			// 每层 mip 的尺寸 = base / 2^mip
			int mipSize = (int)(PF_SIZE * std::pow(0.5f, (float)mip));

			glBindRenderbuffer(GL_RENDERBUFFER, s_CaptureRBO);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipSize, mipSize);

			// mip 层级 → roughness（线性映射）
			float roughness = (float)mip / (float)(MIP_CNT - 1);
			glUniform1f(glGetUniformLocation(prog, "u_Roughness"), roughness);

			// 渲染当前 mip 的 6 个面
			RenderCubeFaces(prog, s_IBL.PrefilterMap, mipSize, mip);
		}
		glDeleteProgram(prog);
		RF_CORE_INFO("IBL: Pre-filter map generated (128, 5 mips)");

		// ==============================================================
		// Step 5: BRDF LUT (512×512, RG16F)
		//
		//   这张 LUT 与具体环境无关，只取决于 BRDF 模型（GGX）。
		//   所有场景可以共享同一张 BRDF LUT。
		// ==============================================================
		glGenTextures(1, &s_IBL.BRDFLUT);
		glBindTexture(GL_TEXTURE_2D, s_IBL.BRDFLUT);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		prog = Compile(s_QuadVS, s_BRDFFS);
		glUseProgram(prog);

		glBindRenderbuffer(GL_RENDERBUFFER, s_CaptureRBO);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
		glBindFramebuffer(GL_FRAMEBUFFER, s_CaptureFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, s_IBL.BRDFLUT, 0);
		glViewport(0, 0, 512, 512);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		DrawQuad();
		glDeleteProgram(prog);
		RF_CORE_INFO("IBL: BRDF LUT generated (512)");

		// ---- 编译天空盒 shader（持久保留到 Shutdown） ----
		s_SkyboxProgram = Compile(s_SkyboxVS, s_SkyboxFS);
		s_SkyboxLocView = glGetUniformLocation(s_SkyboxProgram, "u_View");
		s_SkyboxLocProj = glGetUniformLocation(s_SkyboxProgram, "u_Projection");
		glUseProgram(s_SkyboxProgram);
		glUniform1i(glGetUniformLocation(s_SkyboxProgram, "u_EnvMap"), 0);
		glUseProgram(0);

		// ---- 清理所有临时 GL 对象 ----
		glDeleteFramebuffers(1, &s_CaptureFBO);
		glDeleteRenderbuffers(1, &s_CaptureRBO);
		// 注意：s_CubeVAO / s_CubeVBO **不** 在这里删除 —— 天空盒每帧还要用
		glDeleteVertexArrays(1, &s_QuadVAO);  glDeleteBuffers(1, &s_QuadVBO);
		s_CaptureFBO = s_CaptureRBO = 0;
		s_QuadVAO = s_QuadVBO = 0;

		// ---- 恢复外部 GL 状态 ----
		glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
		glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);

		s_Ready = true;
		RF_CORE_INFO("IBL: All pre-computation complete!");
	}

	void IBLPrecompute::Reload(const std::string& hdrPath)
	{
		Shutdown();
		Init(hdrPath);
	}

	void IBLPrecompute::Shutdown()
	{
		if (s_IBL.EnvCubemap)    { glDeleteTextures(1, &s_IBL.EnvCubemap);    s_IBL.EnvCubemap    = 0; }
		if (s_IBL.IrradianceMap) { glDeleteTextures(1, &s_IBL.IrradianceMap); s_IBL.IrradianceMap = 0; }
		if (s_IBL.PrefilterMap)  { glDeleteTextures(1, &s_IBL.PrefilterMap);  s_IBL.PrefilterMap  = 0; }
		if (s_IBL.BRDFLUT)       { glDeleteTextures(1, &s_IBL.BRDFLUT);       s_IBL.BRDFLUT       = 0; }

		// 释放天空盒专用资源
		if (s_SkyboxProgram) { glDeleteProgram(s_SkyboxProgram); s_SkyboxProgram = 0; }
		if (s_CubeVAO)       { glDeleteVertexArrays(1, &s_CubeVAO); s_CubeVAO = 0; }
		if (s_CubeVBO)       { glDeleteBuffers(1, &s_CubeVBO);      s_CubeVBO = 0; }
		s_SkyboxLocView = s_SkyboxLocProj = -1;

		s_Ready = false;
	}

	void IBLPrecompute::Bind()
	{
		// 对应 PBR shader 中的 binding = 7/8/9：
		//   slot 7: IrradianceMap   — 漫反射 IBL
		//   slot 8: PrefilterMap    — 镜面 IBL
		//   slot 9: BRDFLUT         — Fresnel 积分查找表
		if (s_IBL.IrradianceMap) glBindTextureUnit(7, s_IBL.IrradianceMap);
		if (s_IBL.PrefilterMap)  glBindTextureUnit(8, s_IBL.PrefilterMap);
		if (s_IBL.BRDFLUT)       glBindTextureUnit(9, s_IBL.BRDFLUT);
	}

	// =====================================================================
	// RenderSkybox
	// ---------------------------------------------------------------------
	// 作为"背景"绘制 EnvCubemap，调用时机：
	//   SceneRenderer 在所有不透明 3D 几何渲染完之后调用
	//
	// 关键 GL 状态：
	//   · glDepthFunc(GL_LEQUAL)
	//         因为顶点着色器把 z 强制写成 1.0，
	//         默认 GL_LESS 会让片段被 depth=1.0 的 clear buffer 拒绝。
	//   · glDepthMask(GL_FALSE)
	//         天空盒不写入深度，防止后续透明/后处理被破坏。
	//   · 禁用剔除（或 glCullFace(GL_FRONT)）
	//         因为我们从立方体内部往外看，默认逆时针背面就是朝向相机那面。
	//
	// view 矩阵必须去掉平移：mat4(mat3(view))。
	// 这样相机移动时天空盒不会被"穿过"，永远居中。
	// =====================================================================
	void IBLPrecompute::RenderSkybox(const glm::mat4& view, const glm::mat4& projection)
	{
		if (!s_Ready || !s_SkyboxProgram || !s_CubeVAO || !s_IBL.EnvCubemap)
			return;

		// 只支持透视投影：正交投影下 gl_Position.xyww 技巧不成立，
		// 直接跳过而不是画出错误的天空盒。透视矩阵的 [3][3] 分量为 0，
		// 正交矩阵为 1。
		if (std::abs(projection[3][3]) > 0.5f)
			return;

		// --- 保存外部 GL 状态，结束时恢复 ---
		GLboolean prevDepthMask;        glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
		GLint     prevDepthFunc;        glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
		GLboolean prevCullEnabled   = glIsEnabled(GL_CULL_FACE);
		GLboolean prevDepthEnabled  = glIsEnabled(GL_DEPTH_TEST);

		// --- 设置天空盒绘制所需状态 ---
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_FALSE);
		glDisable(GL_CULL_FACE);

		// --- 去掉 view 的平移分量（只保留旋转），让天空盒跟随相机 ---
		glm::mat4 viewNoTrans = glm::mat4(glm::mat3(view));

		glUseProgram(s_SkyboxProgram);
		glUniformMatrix4fv(s_SkyboxLocView, 1, GL_FALSE, &viewNoTrans[0][0]);
		glUniformMatrix4fv(s_SkyboxLocProj, 1, GL_FALSE, &projection[0][0]);

		// EnvCubemap 绑到 slot 0（shader 中 u_EnvMap 已经在 Init 时设成 0）
		glBindTextureUnit(0, s_IBL.EnvCubemap);

		glBindVertexArray(s_CubeVAO);
		glDrawArrays(GL_TRIANGLES, 0, 36);
		glBindVertexArray(0);
		glUseProgram(0);

		// --- 恢复外部 GL 状态 ---
		glDepthMask(prevDepthMask);
		glDepthFunc(prevDepthFunc);
		if (prevCullEnabled)  glEnable(GL_CULL_FACE);   else glDisable(GL_CULL_FACE);
		if (prevDepthEnabled) glEnable(GL_DEPTH_TEST);  else glDisable(GL_DEPTH_TEST);
	}

	bool IBLPrecompute::IsReady()                     { return s_Ready; }
	const IBLTextures& IBLPrecompute::GetTextures()   { return s_IBL; }

} // namespace Redflag
