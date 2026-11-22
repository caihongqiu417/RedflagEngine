#include "rfpch.h"
#include "VulkanImGui.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include <stdexcept>

namespace Redflag
{
	static void CheckImGuiResult(VkResult result)
	{
		if (result < 0)
			throw std::runtime_error("ImGui Vulkan error " + std::to_string(result));
	}

	// ---- 界面贴图与字体 ----
	VulkanImGui::VulkanImGui(VulkanContext& context)
		: m_Context(context)
	{
		// 字形、场景底色和计算光照结果各有一张供 ImGui 采样的图。
		VkDescriptorPoolSize textures{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 };
		VkDescriptorPoolCreateInfo pool{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool.maxSets = textures.descriptorCount;
		pool.poolSizeCount = 1;
		pool.pPoolSizes = &textures;
		CheckImGuiResult(vkCreateDescriptorPool(context.GetDevice(), &pool, nullptr, &m_Pool));

		try
		{
			InitRenderer();
		}
		catch (...)
		{
			if (ImGui::GetIO().BackendRendererUserData)
				ImGui_ImplVulkan_Shutdown();
			vkDestroyDescriptorPool(context.GetDevice(), m_Pool, nullptr);
			throw;
		}
	}

	VulkanImGui::~VulkanImGui()
	{
		m_Context.WaitIdle();
		ShutdownRenderer();
		vkDestroyDescriptorPool(m_Context.GetDevice(), m_Pool, nullptr);
	}

	void VulkanImGui::InitRenderer()
	{
		m_Format = m_Context.GetWindowFormat();
		m_ImageCount = m_Context.GetImageCount();
		m_MinImageCount = m_Context.GetMinImageCount();

		// 告诉 ImGui 向哪个窗口提交绘制，并为轮流显示的窗口图像准备空间。
		ImGui_ImplVulkan_InitInfo info{};
		info.Instance = m_Context.GetInstance();
		info.PhysicalDevice = m_Context.GetPhysicalDevice();
		info.Device = m_Context.GetDevice();
		info.QueueFamily = m_Context.GetGraphicsQueueFamily();
		info.Queue = m_Context.GetGraphicsQueue();
		info.DescriptorPool = m_Pool;
		info.ImageCount = m_ImageCount;
		info.MinImageCount = m_MinImageCount;
		info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		info.CheckVkResultFn = CheckImGuiResult;
		ImGui_ImplVulkan_Init(&info, m_Context.GetWindowRenderPass());

		// 先把字形画册传给显卡。等这次复制结束，ImGui 才能释放上传用的临时内存。
		m_Context.ImmediateSubmit([](VkCommandBuffer cmd) { ImGui_ImplVulkan_CreateFontsTexture(cmd); });
		ImGui_ImplVulkan_DestroyFontUploadObjects();
	}

	void VulkanImGui::ShutdownRenderer()
	{
		auto* fonts = ImGui::GetIO().Fonts;
		// 这个版本的 ImGui 销毁字形图时，把对应的登记项留给调用者释放。
		ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)fonts->TexID);
		fonts->SetTexID(nullptr);
		ImGui_ImplVulkan_Shutdown();
	}

	// ---- 每帧界面 ----
	void VulkanImGui::NewFrame()
	{
		// 只拖大窗口时可以继续用原来的界面管线。图像格式或数量改变才重建。
		if (m_Format != m_Context.GetWindowFormat() || m_ImageCount != m_Context.GetImageCount() ||
			m_MinImageCount != m_Context.GetMinImageCount())
		{
			m_Context.WaitIdle();
			ShutdownRenderer();
			InitRenderer();
		}

		ImGui_ImplVulkan_NewFrame();
	}

	void VulkanImGui::Render()
	{
		m_Context.BeginWindowPass();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_Context.GetCommandBuffer());
		m_Context.EndWindowPass();
	}
}
