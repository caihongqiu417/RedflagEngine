#pragma once

#include"string.h"
#include"Redflag/Core/Core.h"
#include <memory>

namespace Redflag {

	/* 材质保留这个引用，绘制时让着色器读取对应的显卡贴图。 */
	struct SampledTexture { virtual ~SampledTexture() = default; };

	class Texture
	{
	public:
		virtual ~Texture() = default;

		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual uint32_t GetRendererID() const = 0;

		virtual void SetData(void* data, uint32_t size) = 0;

		virtual void Bind(uint32_t slot=0) const = 0;

		virtual bool operator==(const Texture& other) const = 0;

		virtual const std::string& GetPath() const = 0;

	};

	class Texture2D : public Texture, public SampledTexture, public std::enable_shared_from_this<Texture2D>
	{
	public:
		virtual Ref<SampledTexture> GetSampledTexture() { return shared_from_this(); }
		virtual bool IsLoaded() const { return GetRendererID() != 0; }
		static Ref<Texture2D> Create(uint32_t width, uint32_t height);
		static Ref<Texture2D> Create(const std::string& path);

	};
}
