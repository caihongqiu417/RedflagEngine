/* 环境光预计算。立方体的每一面都相当于朝一个方向拍摄，
 * 绘制前把对应的面设为颜色输出。 */

#include "rfpch.h"
#include "IBLPrecompute.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>

#include <cmath>

namespace Redflag {

	// ---- 环境贴图与绘制资源 ----
	static IBLTextures s_IBL;
	static bool        s_Ready = false;

	// 预计算结束后释放临时绘制资源，天空盒继续使用立方体。
	static uint32_t s_CaptureFBO = 0;  // 切换颜色输出，依次画六个面。
	static uint32_t s_CaptureRBO = 0;  // 深度图的尺寸随当前输出调整。
	static uint32_t s_CubeVAO = 0, s_CubeVBO = 0;  // 预计算和天空盒共享。
	static uint32_t s_QuadVAO = 0, s_QuadVBO = 0;  // 画满 BRDF 查找表。

	static uint32_t s_SkyboxProgram = 0;
	static int      s_SkyboxLocView = -1;
	static int      s_SkyboxLocProj = -1;

	// 立方体的中心就是摄像机位置，顶点位置也给出了查找环境颜色的方向。
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

	// 从中心朝六个面分别绘制。顶面和底面用 Z 轴定朝向，使纹理边缘接得上。
	static glm::mat4 s_Proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
	static glm::mat4 s_Views[] = {
		glm::lookAt(glm::vec3(0), glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)), // +X
		glm::lookAt(glm::vec3(0), glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)), // -X
		glm::lookAt(glm::vec3(0), glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)), // +Y
		glm::lookAt(glm::vec3(0), glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)), // -Y
		glm::lookAt(glm::vec3(0), glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)), // +Z
		glm::lookAt(glm::vec3(0), glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)), // -Z
	};

	// ---- 环境图转换 ----

	// 六个面共用这段顶点着色器。
	static const char* s_CubeVS = R"(
#version 450 core
layout(location=0) in vec3 a_Pos;
uniform mat4 u_Proj;
uniform mat4 u_View;
out vec3 v_LocalPos;
void main(){
    v_LocalPos = a_Pos;
    gl_Position = u_Proj * u_View * vec4(a_Pos, 1.0);
}
)";

	// 把观察方向换成经纬度，在 HDR 全景上找到对应的颜色。
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
    uv += 0.5;
    return uv;
}

void main(){
    vec2 uv = SampleSphericalMap(normalize(v_LocalPos) /* 从中心向外看的方向 */);
    FragColor = vec4(texture(u_HDR, uv).rgb, 1.0);
}
)";

	// ---- 漫反射环境光 ----
	// 对表面上方的半球积分。越正对表面，同样一束光照到的面积越小，每单位面积收到的光越多。
	static const char* s_IrradianceFS = R"(
#version 450 core
in vec3 v_LocalPos;
out vec4 FragColor;
uniform samplerCube u_EnvMap;
const float PI = 3.14159265359;

void main(){
    vec3 N = normalize(v_LocalPos);  // 每个输出方向代表一种表面法线。
    vec3 irr = vec3(0.0);

    // 以 N 为中心构建切线坐标系（up / right / N）
    vec3 up    = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up         = normalize(cross(N, right));

    float delta = 0.025;  // 经纬度的角度步长，单位为弧度。
    float count = 0.0;

    // phi 绕法线转一圈，theta 从正上方走到地平线。
    for(float phi = 0.0; phi < 2.0*PI; phi += delta){
        for(float theta = 0.0; theta < 0.5*PI; theta += delta){
            vec3 ts = vec3(sin(theta)*cos(phi),
                           sin(theta)*sin(phi),
                           cos(theta));
            // 把采样方向转到世界空间，才能在环境图里查颜色。
            vec3 w = ts.x*right + ts.y*up + ts.z*N;

            // cos(theta) 给斜射光较小的权重，sin(theta) 算入这小块天空占的立体角。
            irr += texture(u_EnvMap, w).rgb * cos(theta) * sin(theta);
            count++;
        }
    }
    // 两个角度的取值范围相乘是 π²，再乘漫反射的 1/π。这样查图后只需乘表面颜色。
    irr = PI * irr / count;
    FragColor = vec4(irr, 1.0);
}
)";

	// ---- 不同粗糙度的镜面反射 ----
	// 假定视线与法线都沿反射方向，按 GGX 分布取样。粗糙表面收集更宽范围的天空，倒影就更模糊。
	static const char* s_PrefilterFS = R"(
#version 450 core
in vec3 v_LocalPos;
out vec4 FragColor;
uniform samplerCube u_EnvMap;
uniform float u_Roughness;
uniform float u_EnvMapSize;  // 源环境图一面的宽度，单位为像素。

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

// GGX 给出微表面法线的分布，下面用它估计每个光照样本代表多大的区域。
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

    // 立方体各处纹素覆盖的立体角略有差异，这里取整张图的平均值。
    float saTexel = 4.0 * PI / (6.0 * u_EnvMapSize * u_EnvMapSize);

    for(uint i=0u; i<SAMPLES; ++i){
        vec2 Xi = Hammersley(i, SAMPLES);
        vec3 H  = SampleGGX(Xi, N, u_Roughness /* 当前反射层的粗糙度 */);
        vec3 L  = normalize(2.0*dot(V,H)*H - V);

        float NdL = max(dot(N,L), 0.0);
        if(NdL > 0.0){
            // 样本稀疏的方向要代表更大一片天空。读取较粗的一层环境图，减轻偶然采到太阳产生的亮点。
            float NdH = max(dot(N, H), 0.0);
            float HdV = max(dot(H, V), 0.0);
            float D   = DistributionGGX(NdH, u_Roughness);
            // D·NdH 描述半程向量 H 的概率密度。除以 4(V·H)，得到反射方向 L 的概率密度。
            float pdf = (D * NdH / (4.0 * HdV)) + 0.0001;
            float saSample = 1.0 / (float(SAMPLES) * pdf + 0.0001);

            // 光滑表面的倒影清晰，直接读原图。
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

	// ---- 镜面反射查找表 ----
	// 按视角和粗糙度积分，把 Schlick 菲涅耳项拆为两个系数。主绘制取出 A、B，与反射图组合。
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

// 环境光的几何衰减取 k = rough²/2。
float G1(float NdV, float rough){
    float k = (rough*rough) / 2.0;
    return NdV / (NdV*(1.0-k) + k);
}
// 视线和入射光都有可能被微表面挡住。
float GSmith(vec3 N, vec3 V, vec3 L, float rough){
    return G1(max(dot(N,V),0.0), rough) * G1(max(dot(N,L),0.0), rough);
}

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
            // 按 GGX 采样后，投影和遮挡留下这个权重。
            float G_Vis = (G * VdH) / (NdH * NdV);
            float Fc    = pow(1.0 - VdH, 5.0);

            // R 保存 (1-Fc) 的加权和，G 保存 Fc 的加权和，最后一起取平均。
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return vec2(A, B) / float(SAMPLES);
}

void main(){
    FragColor = vec4(IntegrateBRDF(v_UV.x /* 法线与视线夹角的余弦 */, v_UV.y /* 粗糙度 */), 0.0, 1.0);
}
)";

	// ---- 天空盒 ----
	// 只跟随相机旋转，把天空放在最远处。前景模型已经写过深度的地方不会被覆盖。
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

	// 保留环境图的线性亮度。天空和模型最后一起调整曝光，转换成屏幕颜色。
	static const char* s_SkyboxFS = R"(
#version 450 core
in  vec3 v_LocalPos;
out vec4 FragColor;
uniform samplerCube u_EnvMap;
void main(){
    vec3 color = textureLod(u_EnvMap, normalize(v_LocalPos), 0.0).rgb;
    FragColor = vec4(color, 1.0);
}
)";

	// ---- 编译与离屏绘制 ----

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

	// 切换六个观察方向，把结果画到同一张立方体纹理的六个面。调用前先设置投影矩阵。
	static void RenderCubeFaces(uint32_t program, uint32_t cubeTex, int size, int mipLevel = 0)
	{
		glViewport(0, 0, size, size);
		glBindFramebuffer(GL_FRAMEBUFFER, s_CaptureFBO);
		for (uint32_t i = 0; i < 6; ++i)
		{
			glUniformMatrix4fv(
				glGetUniformLocation(program, "u_View"),
				1, GL_FALSE, &s_Views[i][0][0]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubeTex, mipLevel);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			DrawCube();
		}
	}

	// ---- 载入 HDR 全景 ----
	void IBLPrecompute::Init(const std::string& hdrPath)
	{
		// 先读取文件；失败时不分配 GPU 资源，也不改变绘制状态。
		stbi_set_flip_vertically_on_load(true);
		int w = 0, h = 0, ch = 0;
		float* pixels = stbi_loadf(hdrPath.c_str(), &w, &h, &ch, STBI_rgb);
		if (!pixels)
		{
			RF_CORE_ERROR("IBL: Failed to load HDR: {0}", hdrPath);
			return;
		}

		// 预计算从立方体内部绘制，不能继承主画面的剔除、混合或裁剪状态。
		GLint prevFBO, prevReadFBO, prevVP[4], prevDepthFunc;
		GLboolean prevDepthMask;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
		glGetIntegerv(GL_VIEWPORT, prevVP);
		glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
		const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
		const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
		const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
		const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_TRUE);
		glDisable(GL_CULL_FACE);
		glDisable(GL_BLEND);
		glDisable(GL_SCISSOR_TEST);

		MakeCubeVAO();
		MakeQuadVAO();
		constexpr int ENV_SIZE = 2048;
		glGenFramebuffers(1, &s_CaptureFBO);
		glGenRenderbuffers(1, &s_CaptureRBO);
		glBindFramebuffer(GL_FRAMEBUFFER, s_CaptureFBO);
		glBindRenderbuffer(GL_RENDERBUFFER, s_CaptureRBO);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			GL_RENDERBUFFER, s_CaptureRBO);

		RF_CORE_INFO("IBL: Loaded HDR {0}x{1} from {2}", w, h, hdrPath);

		// 非有限值会污染附近的反射。将它们清零，并把每个颜色分量限制在 0 到 50，减少孤立亮点。
		const size_t pixelCount = size_t(w) * h * 3;
		for (size_t i = 0; i < pixelCount; ++i)
		{
			float v = pixels[i];
			if (!std::isfinite(v))       v = 0.0f;
			else if (v > 50.0f)          v = 50.0f;
			else if (v < 0.0f)           v = 0.0f;
			pixels[i] = v;
		}

		uint32_t hdrTex = 0;
		glGenTextures(1, &hdrTex);
		glBindTexture(GL_TEXTURE_2D, hdrTex);
		// HDR 图里太阳等光源的亮度可以超过 1，用浮点纹理保留这些差别。
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		stbi_image_free(pixels);

		// ---- 全景转立方体 ----
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
		// 六个面画完才生成缩小层，这里先从原始尺寸取色。
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

		// 后续预滤波会按每个样本覆盖的范围选择缩小层。
		glBindTexture(GL_TEXTURE_CUBE_MAP, s_IBL.EnvCubemap);
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

		RF_CORE_INFO("IBL: Environment cubemap generated ({0}, mipmapped)", ENV_SIZE);

		// ---- 漫反射图 ----
		// 漫反射收集整片半球的光，随方向变化缓慢，每个面只保留 32×32 像素。
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

		// ---- 镜面反射图 ----
		// 每面从 128×128 缩到 8×8，五层分别对应从 0 到 1 的粗糙度。
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
		// 表面的粗糙度落在两层之间时，在这两层的颜色之间插值。
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		// 先生成各个尺寸，下面再用相应粗糙度的积分结果逐层覆盖。
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

		prog = Compile(s_CubeVS, s_PrefilterFS);
		glUseProgram(prog);
		glUniform1i(glGetUniformLocation(prog, "u_EnvMap"), 0);
		glUniformMatrix4fv(glGetUniformLocation(prog, "u_Proj"), 1, GL_FALSE, &s_Proj[0][0]);
		glUniform1f(glGetUniformLocation(prog, "u_EnvMapSize"), (float)ENV_SIZE);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_CUBE_MAP, s_IBL.EnvCubemap);

		glBindFramebuffer(GL_FRAMEBUFFER, s_CaptureFBO);
		for (int mip = 0; mip < MIP_CNT; ++mip)
		{
			int mipSize = (int)(PF_SIZE * std::pow(0.5f, (float)mip));

			glBindRenderbuffer(GL_RENDERBUFFER, s_CaptureRBO);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipSize, mipSize);

			float roughness = (float)mip / (float)(MIP_CNT - 1);
			glUniform1f(glGetUniformLocation(prog, "u_Roughness"), roughness);

			RenderCubeFaces(prog, s_IBL.PrefilterMap, mipSize, mip);
		}
		glDeleteProgram(prog);
		RF_CORE_INFO("IBL: Pre-filter map generated (128, 5 mips)");

		// ---- BRDF 查找表 ----
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

		// ---- 天空盒着色器 ----
		s_SkyboxProgram = Compile(s_SkyboxVS, s_SkyboxFS);
		s_SkyboxLocView = glGetUniformLocation(s_SkyboxProgram, "u_View");
		s_SkyboxLocProj = glGetUniformLocation(s_SkyboxProgram, "u_Projection");
		glUseProgram(s_SkyboxProgram);
		glUniform1i(glGetUniformLocation(s_SkyboxProgram, "u_EnvMap"), 0);
		glUseProgram(0);

		// ---- 释放临时绘制资源 ----
		glDeleteFramebuffers(1, &s_CaptureFBO);
		glDeleteRenderbuffers(1, &s_CaptureRBO);
		// 天空盒每帧都要画立方体，顶点数据保留到 Shutdown。
		glDeleteVertexArrays(1, &s_QuadVAO);  glDeleteBuffers(1, &s_QuadVBO);
		s_CaptureFBO = s_CaptureRBO = 0;
		s_QuadVAO = s_QuadVBO = 0;

		// ---- 恢复外部 GL 状态 ----
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevFBO);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFBO);
		glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
		glDepthFunc(prevDepthFunc);
		glDepthMask(prevDepthMask);
		if (!depthEnabled) glDisable(GL_DEPTH_TEST);
		if (cullEnabled) glEnable(GL_CULL_FACE);
		if (blendEnabled) glEnable(GL_BLEND);
		if (scissorEnabled) glEnable(GL_SCISSOR_TEST);

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

		if (s_CaptureFBO) { glDeleteFramebuffers(1, &s_CaptureFBO);   s_CaptureFBO = 0; }
		if (s_CaptureRBO) { glDeleteRenderbuffers(1, &s_CaptureRBO);  s_CaptureRBO = 0; }

		if (s_QuadVAO) { glDeleteVertexArrays(1, &s_QuadVAO); s_QuadVAO = 0; }
		if (s_QuadVBO) { glDeleteBuffers(1, &s_QuadVBO); s_QuadVBO = 0; }

		// 释放天空盒专用资源
		if (s_SkyboxProgram) { glDeleteProgram(s_SkyboxProgram); s_SkyboxProgram = 0; }
		if (s_CubeVAO)       { glDeleteVertexArrays(1, &s_CubeVAO); s_CubeVAO = 0; }
		if (s_CubeVBO)       { glDeleteBuffers(1, &s_CubeVBO);      s_CubeVBO = 0; }
		s_SkyboxLocView = s_SkyboxLocProj = -1;

		s_Ready = false;
	}

	void IBLPrecompute::Bind()
	{
		// 同一材质同时用到三张图：漫反射的颜色、模糊的倒影和调整倒影强度的系数。
		if (s_IBL.IrradianceMap) glBindTextureUnit(7, s_IBL.IrradianceMap);
		if (s_IBL.PrefilterMap)  glBindTextureUnit(8, s_IBL.PrefilterMap);
		if (s_IBL.BRDFLUT)       glBindTextureUnit(9, s_IBL.BRDFLUT);
	}

	// ---- 天空背景 ----
	void IBLPrecompute::RenderSkybox(const glm::mat4& view, const glm::mat4& projection)
	{
		if (!s_Ready || !s_SkyboxProgram || !s_CubeVAO || !s_IBL.EnvCubemap)
			return;

		// 这里只为透视相机画天空。正交投影矩阵的右下角为 1。
		if (std::abs(projection[3][3]) > 0.5f)
			return;

		// 记下后续绘制还要用的状态。
		GLboolean prevEntityMask[4], prevNormalMask[4];
		glGetBooleani_v(GL_COLOR_WRITEMASK, 1, prevEntityMask);
		glGetBooleani_v(GL_COLOR_WRITEMASK, 2, prevNormalMask);
		GLboolean prevDepthMask;        glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
		GLint     prevDepthFunc;        glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
		GLboolean prevCullEnabled   = glIsEnabled(GL_CULL_FACE);
		GLboolean prevDepthEnabled  = glIsEnabled(GL_DEPTH_TEST);

		// 只写背景色，保留鼠标拾取用的实体编号和表面法线。
		glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glColorMaski(2, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

		// 从立方体内部看天空；深度等于清屏值的背景也要通过测试。
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_FALSE);
		glDisable(GL_CULL_FACE);

		// 相机移动时天空留在远方，转头时才换方向。
		glm::mat4 viewNoTrans = glm::mat4(glm::mat3(view));

		glUseProgram(s_SkyboxProgram);
		glUniformMatrix4fv(s_SkyboxLocView, 1, GL_FALSE, &viewNoTrans[0][0]);
		glUniformMatrix4fv(s_SkyboxLocProj, 1, GL_FALSE, &projection[0][0]);

		glBindTextureUnit(0, s_IBL.EnvCubemap);

		glBindVertexArray(s_CubeVAO);
		glDrawArrays(GL_TRIANGLES, 0, 36);
		glBindVertexArray(0);
		glUseProgram(0);

		// 画完天空后恢复调用前的状态。
		glColorMaski(1, prevEntityMask[0], prevEntityMask[1], prevEntityMask[2], prevEntityMask[3]);
		glColorMaski(2, prevNormalMask[0], prevNormalMask[1], prevNormalMask[2], prevNormalMask[3]);
		glDepthMask(prevDepthMask);
		glDepthFunc(prevDepthFunc);
		if (prevCullEnabled)  glEnable(GL_CULL_FACE);   else glDisable(GL_CULL_FACE);
		if (prevDepthEnabled) glEnable(GL_DEPTH_TEST);  else glDisable(GL_DEPTH_TEST);
	}

	bool IBLPrecompute::IsReady()                     { return s_Ready; }
	const IBLTextures& IBLPrecompute::GetTextures()   { return s_IBL; }

} // namespace Redflag
