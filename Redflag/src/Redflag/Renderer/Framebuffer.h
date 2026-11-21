#pragma once

#include"Redflag/Core/Core.h"

namespace Redflag {
	
	enum class FramebufferTextureFormat
	{
		None = 0,

		// 颜色格式

		// 8-bit RGBA，每通道 0~255，适合 LDR 颜色（Albedo、UI 等）
		RGBA8,

		// 16-bit 浮点 RGBA，每通道 half-float，适合 HDR 场景颜色、
		// GBuffer 中的法线（需要负值和高精度）等
		RGBA16F,

		// 16-bit 浮点 RG，两通道，适合压缩法线、运动向量等
		RG16F,

		// 32-bit 整数单通道，用于存储 Entity ID（编辑器拾取）
		RED_INTEGER,

		// 深度格式

		// 24-bit 深度 + 8-bit 模板，最常用的深度附件格式
		DEPTH24STENCIL8,

		// 32-bit 浮点深度，无模板。精度更高，且可以直接作为纹理被
		// shader 采样（用于 Shadow Map、SSAO 等需要读取深度的场景）
		DEPTH32F,

		// 别名
		Depth = DEPTH24STENCIL8
	};

	//单个附件的规格
	struct FramebufferTextureSpecification
	{
		FramebufferTextureSpecification() = default;
		FramebufferTextureSpecification(FramebufferTextureFormat format)
			:TextureFormat(format){ }
		
		FramebufferTextureFormat TextureFormat=FramebufferTextureFormat::None;
		//TODO:filtering,wrap
	};

	// 多个附件的规格
	struct FramebufferAttachmentSpecification
	{
		FramebufferAttachmentSpecification() = default;
		FramebufferAttachmentSpecification(std::initializer_list<FramebufferTextureSpecification> attachments)
			:Attachments(attachments){ }

		std::vector<FramebufferTextureSpecification> Attachments;

	};

	//整个缓冲区的规格
	struct FramebufferSpecification
	{
		uint32_t Width, Height;

		FramebufferAttachmentSpecification Attachments;

		uint32_t Samples = 1; //不开启MSAA
		
		bool SwapChainTarget = false; // 当前 OpenGLFramebuffer 未使用此标志，仍创建离屏 FBO。
	};

	/*
	 * 离屏渲染目标，按规格创建并持有颜色/深度附件。Bind 同时设置目标像素尺寸的视口。
	 * 当前接口返回 OpenGL 对象名；这些 ID 只供借用，Resize 重建附件后须重新查询。
	 */
	class Framebuffer
	{
	public:
		virtual ~Framebuffer() = default;

		// ---- 目标与尺寸 ----
		virtual void Bind() = 0;
		virtual void Unbind() = 0;

		virtual void Resize(uint32_t width,uint32_t height)=0;
		// ---- CPU 读写 ----
		// 仅支持 RED_INTEGER；调用前绑定此 FBO 为读取目标，x/y 以左下角为原点且须在附件范围内。
		virtual int ReadPixel(uint32_t attachmentIndex, int x, int y) = 0;

		// 整数附件写入 value，浮点/归一化附件把 value 数值转换为 float 并填充各通道。
		virtual void ClearAttachment(uint32_t attachmentIndex,int value)=0;
		
		// ---- 后端资源查询 ----
		virtual uint32_t GetColorAttachmentRendererID(uint32_t index=0) const = 0;

		// 返回 FBO 自身的 GL 对象 ID（不是纹理 ID）
		// 用途：glBlitFramebuffer 需要直接指定源/目标 FBO
		virtual uint32_t GetFramebufferID() const = 0;

		// 获取深度附件的 OpenGL 纹理 ID，用于在 shader 中采样深度
		// （Shadow Map、SSAO 等场景需要把深度当纹理读取）
		virtual uint32_t GetDepthAttachmentRendererID() const = 0;

		virtual const FramebufferSpecification& GetSpecifictaion() const = 0;

		static Ref<Framebuffer> Create(const FramebufferSpecification& spec);
	};
}