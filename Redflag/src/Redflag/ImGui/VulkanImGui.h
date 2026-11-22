#pragma once

#include "Platform/Vulkan/VulkanContext.h"

namespace Redflag
{
	/* 把编辑器的文字和场景面板一起画到当前窗口图像。界面贴图的登记表在窗口关闭前释放。 */
	class VulkanImGui
	{
	public:
		explicit VulkanImGui(VulkanContext& context);
		~VulkanImGui();

		void NewFrame();
		void Render();

	private:
		void InitRenderer();
		void ShutdownRenderer();

		VulkanContext& m_Context;
		VkDescriptorPool m_Pool = VK_NULL_HANDLE;
		VkFormat m_Format = VK_FORMAT_UNDEFINED;
		uint32_t m_ImageCount = 0;
		uint32_t m_MinImageCount = 0;
	};
}
