#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace Redflag {

	/* IBLPrecompute 持有天空图、两张材质环境光图和一张与场景无关的镜面查找表。 */
	struct IBLTextures
	{
		uint32_t EnvCubemap    = 0;  // 天空盒按观察方向读取原始环境。
		uint32_t IrradianceMap = 0;  // 漫反射表面按法线方向读取环境光。
		uint32_t PrefilterMap  = 0;  // 镜面反射，表面越粗糙就读取越模糊的一层。
		uint32_t BRDFLUT       = 0;  // 以视角与粗糙度查得两项镜面系数。
	};

	/* 载入环境图时做积分，画模型时可以直接查图取光。 */
	class IBLPrecompute
	{
	public:
		// 从 HDR 全景生成天空盒和材质光照要用的贴图。
		static void Init(const std::string& hdrPath);

		// 在有效图形上下文中释放输出纹理和辅助绘制资源
		static void Shutdown();

		// 给下一次模型绘制选好漫反射图、粗糙度分层的反射图和 BRDF 查找表。
		static void Bind();

		// 模型画完后补天空背景，让前景物体保留自己的颜色。
		static void RenderSkybox(const glm::mat4& view, const glm::mat4& projection);

		// 在编辑器换环境图时丢弃旧结果，按新图重新计算。
		static void Reload(const std::string& hdrPath);

		// 主着色器等贴图准备好之后才使用环境光。
		static bool IsReady();

		static const IBLTextures& GetTextures();
	};

} // namespace Redflag
