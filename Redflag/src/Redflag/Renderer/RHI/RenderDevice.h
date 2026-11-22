#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include "Redflag/Core/Core.h"

namespace Redflag
{
	struct MeshVertex;
	struct MeshGeometry;
	struct SampledTexture;

	/* 模型加载器把顶点、索引和像素交给当前设备。返回的资源可以在后续帧继续使用。 */
	class RenderDevice
	{
	public:
		virtual ~RenderDevice() = default;

		/// <summary>
		/// 用模型的顶点和索引创建网格，返回的几何数据可以在多帧中反复绘制。
		/// </summary>
		/// <param name="vertices">模型的顶点数组。每个顶点保存位置、法线、贴图坐标（UV）和切线。</param>
		/// <param name="indices">顶点在数组中的序号，从 0 开始。每三个序号组成一个三角形。</param>
		virtual Ref<MeshGeometry> CreateMesh(const std::vector<MeshVertex>& vertices,
			const std::vector<uint32_t>& indices) = 0;

		/// <summary>从 RGBA 像素创建着色器可以采样的贴图。返回后可释放原来的像素数组。</summary>
		/// <param name="rgba8">逐行排好的像素，每个像素依次用四个字节保存红、绿、蓝和透明度。</param>
		virtual Ref<SampledTexture> CreateTexture(uint32_t width, uint32_t height, const void* rgba8) = 0;

		// 加载模型时用 Get 取得绘制设备。SetCurrent 只借用设备指针，销毁设备前应先调用 SetCurrent(nullptr)。
		static RenderDevice& Get();

		static void SetCurrent(RenderDevice* device);
	};
}
