#pragma once

#include "Redflag/Renderer/GraphicsContext.h"
#include <vulkan/vulkan.h>

#include <array>
#include <functional>
#include <vector>

struct GLFWwindow;

namespace Redflag {

	/* 窗口持有这个上下文。每帧先取一张可显示的图，录完场景和界面，再交给窗口。 */
	class VulkanContext final : public GraphicsContext
	{
	public:
		explicit VulkanContext(GLFWwindow* window) : m_Window(window) {}
		~VulkanContext() override;
		VulkanContext(const VulkanContext&) = delete;
		VulkanContext& operator=(const VulkanContext&) = delete;

		// ---- 初始化与窗口绘制 ----
		void Init() override;

		/// <summary>从交换链取出本帧要显示的图像。等显卡用完上次的绘制命令，才能重新记录。</summary>
		/// <returns>窗口最小化或需要重新取图时返回 false，应用跳过这一帧的绘制。</returns>
		bool BeginFrame() override;

		void BeginWindowPass();
		void EndWindowPass();
		void SwapBuffers() override;
		void SetVSync(bool enabled);

		// ---- 资源上传与离屏绘制 ----
		enum class Work { Resources, Surfaces };

		/// <summary>单独提交一段命令，等显卡执行完才返回。绘制途中上传模型时，两者各用自己的命令池。</summary>
		void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& record, Work work = Work::Resources);

		void WaitIdle() const;

		// ---- 设备与图形队列 ----
		VkInstance GetInstance() const { return m_Instance; }
		VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
		VkDevice GetDevice() const { return m_Device; }

		VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
		uint32_t GetGraphicsQueueFamily() const { return m_GraphicsFamily; }

		// ---- 当前帧的命令缓冲区和帧索引 ----
		VkCommandBuffer GetCommandBuffer() const { return m_Frames[m_FrameIndex].Commands; }
		uint32_t GetFrameIndex() const { return m_FrameIndex; }

		// ---- 编辑器绘制界面所需的窗口图像 ----
		VkRenderPass GetWindowRenderPass() const { return m_RenderPass; }
		VkFormat GetWindowFormat() const { return m_Format; }
		VkExtent2D GetExtent() const { return m_Extent; }

		uint32_t GetImageCount() const { return static_cast<uint32_t>(m_Images.size()); }
		uint32_t GetMinImageCount() const { return m_MinImageCount; }
		uint32_t GetSwapchainGeneration() const { return m_SwapchainGeneration; }

	private:
		void CreateInstance();
		void CreateDevice();
		void CreateCommands();
		void CreateSwapchain(uint32_t width, uint32_t height);
		void DestroySwapchain();
		void CreateWindowRenderPass();

		struct Frame
		{
			VkCommandPool Pool = VK_NULL_HANDLE;
			VkCommandBuffer Commands = VK_NULL_HANDLE;
			VkSemaphore ImageAvailable = VK_NULL_HANDLE;
			VkFence Finished = VK_NULL_HANDLE;
		};

		// 每张窗口图都有自己的 Ready 信号。图形队列画完就发出信号，呈现队列等到它才显示这张图。
		struct WindowImage
		{
			VkImageView View = VK_NULL_HANDLE;
			VkFramebuffer Framebuffer = VK_NULL_HANDLE;
			VkSemaphore Ready = VK_NULL_HANDLE;
			VkFence Presented = VK_NULL_HANDLE;
			bool WaitingForPresent = false;
		};

		GLFWwindow* m_Window;
		VkInstance m_Instance = VK_NULL_HANDLE;
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		uint32_t m_GraphicsFamily = 0, m_PresentFamily = 0;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE, m_PresentQueue = VK_NULL_HANDLE;
		bool m_SurfaceMaintenance = false, m_PresentFences = false;

		std::array<Frame, 2> m_Frames{};
		uint32_t m_FrameIndex = 0, m_ImageIndex = 0;
		struct CommandBatch
		{
			VkCommandPool Pool = VK_NULL_HANDLE;
			VkCommandBuffer Commands = VK_NULL_HANDLE;
			VkFence Finished = VK_NULL_HANDLE;
		};
		CommandBatch m_Resources, m_Surfaces;

		VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
		VkRenderPass m_RenderPass = VK_NULL_HANDLE;
		std::vector<WindowImage> m_Images;
		VkFormat m_Format = VK_FORMAT_UNDEFINED;
		VkExtent2D m_Extent{}, m_WindowSize{};
		uint32_t m_MinImageCount = 0, m_SwapchainGeneration = 0;
		bool m_Rebuild = true, m_VSync = true, m_FrameOpen = false, m_WindowDrawn = false;
	};
}
