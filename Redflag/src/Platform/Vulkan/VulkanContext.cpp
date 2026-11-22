#include "rfpch.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "VulkanResult.h"

#include <GLFW/glfw3.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace Redflag {

	using Vulkan::Check;

	namespace {
		bool HasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
		{
			return std::any_of(extensions.begin(), extensions.end(), [name](const auto& e) {
				return std::strcmp(e.extensionName, name) == 0;
			});
		}

		VKAPI_ATTR VkBool32 VKAPI_CALL ReportMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* message, void*)
		{
			if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
				RF_CORE_ERROR("Vulkan: {0}", message->pMessage);
			else
				RF_CORE_WARN("Vulkan: {0}", message->pMessage);
			return VK_FALSE;
		}

		VkFence CreateFence(VkDevice device, bool signaled)
		{
			VkFenceCreateInfo info{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			info.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
			VkFence fence;
			Check(vkCreateFence(device, &info, nullptr, &fence), "vkCreateFence");
			return fence;
		}

		VkSemaphore CreateSemaphore(VkDevice device)
		{
			VkSemaphoreCreateInfo info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			VkSemaphore semaphore;
			Check(vkCreateSemaphore(device, &info, nullptr, &semaphore), "vkCreateSemaphore");
			return semaphore;
		}

		void BeginCommands(VkCommandBuffer commands)
		{
			VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			Check(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer");
		}
	}

	// ---- 实例与设备 ----
	void VulkanContext::Init()
	{
		CreateInstance();
		Check(glfwCreateWindowSurface(m_Instance, m_Window, nullptr, &m_Surface), "glfwCreateWindowSurface");
		CreateDevice();
		CreateCommands();

		int width, height;
		glfwGetFramebufferSize(m_Window, &width, &height);
		if (width && height)
			CreateSwapchain(width, height);
	}

	void VulkanContext::CreateInstance()
	{
		uint32_t count = 0;
		const char** required = glfwGetRequiredInstanceExtensions(&count);
		if (!required)
			throw std::runtime_error("GLFW could not find a Vulkan loader for this window");
		std::vector<const char*> extensions(required, required + count);

		// GLFW 列出窗口所需的扩展。若驱动还支持呈现完成通知，稍后销毁旧交换链时就能等窗口用完图像。
		Check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr), "enumerate Vulkan extensions");
		std::vector<VkExtensionProperties> available(count);
		Check(vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data()), "enumerate Vulkan extensions");
		m_SurfaceMaintenance = HasExtension(available, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME)
			&& HasExtension(available, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
		if (m_SurfaceMaintenance)
		{
			extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
			extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
		}

		const char* layer = "VK_LAYER_KHRONOS_validation";
		bool validate = std::getenv("RF_VULKAN_VALIDATION") != nullptr;
#ifdef RF_DEBUG
		validate = true;
#endif
		if (validate)
		{
			Check(vkEnumerateInstanceLayerProperties(&count, nullptr), "enumerate Vulkan layers");
			std::vector<VkLayerProperties> layers(count);
			Check(vkEnumerateInstanceLayerProperties(&count, layers.data()), "enumerate Vulkan layers");
			validate = std::any_of(layers.begin(), layers.end(), [layer](const auto& item) {
				return std::strcmp(item.layerName, layer) == 0;
			});
			if (!validate)
				RF_CORE_WARN("Vulkan validation layer is not installed");
		}

		bool report = validate && HasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		VkDebugUtilsMessengerCreateInfoEXT debug{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		if (report)
		{
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			debug.pfnUserCallback = ReportMessage;
		}

		VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app.pApplicationName = "Redflag";
		app.apiVersion = VK_API_VERSION_1_3;
		VkInstanceCreateInfo info{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		info.pApplicationInfo = &app;
		info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		info.ppEnabledExtensionNames = extensions.data();
		info.enabledLayerCount = validate ? 1 : 0;
		info.ppEnabledLayerNames = validate ? &layer : nullptr;
		info.pNext = report ? &debug : nullptr;
		Check(vkCreateInstance(&info, nullptr, &m_Instance), "vkCreateInstance");

		if (report)
		{
			auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT"));
			Check(create(m_Instance, &debug, nullptr, &m_DebugMessenger), "vkCreateDebugUtilsMessengerEXT");
		}
	}

	void VulkanContext::CreateDevice()
	{
		uint32_t count = 0;
		Check(vkEnumeratePhysicalDevices(m_Instance, &count, nullptr), "enumerate Vulkan devices");
		std::vector<VkPhysicalDevice> devices(count);
		Check(vkEnumeratePhysicalDevices(m_Instance, &count, devices.data()), "enumerate Vulkan devices");

		// 优先选独立显卡；同一条队列能绘制并显示时，就无需跨队列交接图像。
		for (auto candidate : devices)
		{
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(candidate, &properties);
			if (properties.apiVersion < VK_API_VERSION_1_3)
				continue;

			Check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, nullptr), "enumerate device extensions");
			std::vector<VkExtensionProperties> extensions(count);
			Check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, extensions.data()), "enumerate device extensions");
			if (!HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
				continue;

			vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
			std::vector<VkQueueFamilyProperties> families(count);
			vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
			uint32_t graphics = UINT32_MAX, present = UINT32_MAX;
			for (uint32_t i = 0; i < count; i++)
			{
				VkBool32 supported;
				Check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, m_Surface, &supported), "query present support");
				const auto drawing = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
				if ((families[i].queueFlags & drawing) == drawing)
				{
					graphics = i;
					if (supported) { present = i; break; }
				}
				if (supported)
					present = i;
			}
			if (graphics == UINT32_MAX || present == UINT32_MAX)
				continue;

			if (m_PhysicalDevice && properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				continue;
			m_PhysicalDevice = candidate;
			m_GraphicsFamily = graphics;
			m_PresentFamily = present;
			m_PresentFences = m_SurfaceMaintenance && HasExtension(extensions, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
			if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				break;
		}
		if (!m_PhysicalDevice)
			throw std::runtime_error("No Vulkan 1.3 device supports graphics and presentation for this window");

		// 驱动提供呈现围栏时，窗口释放图像后会给 CPU 一个可以等待的信号。
		std::vector<const char*> extensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
		VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT };
		if (m_PresentFences)
		{
			VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			features.pNext = &maintenance;
			vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &features);
			m_PresentFences = maintenance.swapchainMaintenance1 == VK_TRUE;
			if (m_PresentFences)
				extensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
		}

		float priority = 1.0f;
		VkDeviceQueueCreateInfo queues[2]{};
		queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queues[0].queueFamilyIndex = m_GraphicsFamily;
		queues[0].queueCount = 1;
		queues[0].pQueuePriorities = &priority;
		queues[1] = queues[0];
		queues[1].queueFamilyIndex = m_PresentFamily;

		VkDeviceCreateInfo info{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		info.queueCreateInfoCount = m_GraphicsFamily == m_PresentFamily ? 1 : 2;
		info.pQueueCreateInfos = queues;
		info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		info.ppEnabledExtensionNames = extensions.data();
		info.pNext = m_PresentFences ? &maintenance : nullptr;
		Check(vkCreateDevice(m_PhysicalDevice, &info, nullptr, &m_Device), "vkCreateDevice");
		vkGetDeviceQueue(m_Device, m_GraphicsFamily, 0, &m_GraphicsQueue);
		vkGetDeviceQueue(m_Device, m_PresentFamily, 0, &m_PresentQueue);

		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(m_PhysicalDevice, &properties);
		RF_CORE_INFO("Vulkan device: {0}", properties.deviceName);
	}

	void VulkanContext::CreateCommands()
	{
		auto allocate = [this](VkCommandPool& pool, VkCommandBuffer& commands) {
			VkCommandPoolCreateInfo info{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
			info.queueFamilyIndex = m_GraphicsFamily;
			Check(vkCreateCommandPool(m_Device, &info, nullptr, &pool), "vkCreateCommandPool");

			VkCommandBufferAllocateInfo allocation{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			allocation.commandPool = pool;
			allocation.commandBufferCount = 1;
			allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			Check(vkAllocateCommandBuffers(m_Device, &allocation, &commands), "vkAllocateCommandBuffers");
		};

		for (auto& frame : m_Frames)
		{
			allocate(frame.Pool, frame.Commands);
			frame.ImageAvailable = CreateSemaphore(m_Device);
			frame.Finished = CreateFence(m_Device, true);
		}

		for (auto batch : { &m_Resources, &m_Surfaces })
		{
			allocate(batch->Pool, batch->Commands);
			batch->Finished = CreateFence(m_Device, false);
		}
	}

	// ---- 窗口图像 ----
	void VulkanContext::CreateSwapchain(uint32_t width, uint32_t height)
	{
		VkSurfaceCapabilitiesKHR capabilities;
		Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities), "query window capabilities");

		// 面板已经调过显示亮度。优先选择能直接写入这些颜色值的窗口格式。
		uint32_t count = 0;
		Check(vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &count, nullptr), "query window formats");
		if (!count) throw std::runtime_error("Vulkan surface has no color formats");
		std::vector<VkSurfaceFormatKHR> formats(count);
		Check(vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &count, formats.data()), "query window formats");
		auto format = formats.front();
		for (const auto& candidate : formats)
		{
			if ((candidate.format == VK_FORMAT_B8G8R8A8_UNORM || candidate.format == VK_FORMAT_R8G8B8A8_UNORM)
				&& candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				format = candidate;
				break;
			}
		}
		if (format.format == VK_FORMAT_UNDEFINED)
			format.format = VK_FORMAT_B8G8R8A8_UNORM;

		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
		if (!m_VSync)
		{
			Check(vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &count, nullptr), "query present modes");
			std::vector<VkPresentModeKHR> modes(count);
			Check(vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &count, modes.data()), "query present modes");
			if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end())
				presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
			else if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end())
				presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		}

		VkExtent2D extent = capabilities.currentExtent;
		if (extent.width == UINT32_MAX)
		{
			extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
		}
		if (!extent.width || !extent.height)
		{
			m_Rebuild = true;
			return;
		}
		if (m_Swapchain)
		{
			// 窗口换尺寸或显示模式时，先等显卡画完旧图，再销毁旧交换链。
			WaitIdle();
			DestroySwapchain();
		}

		m_WindowSize = { width, height };
		m_Extent = extent;
		m_Format = format.format;
		m_MinImageCount = (std::max)(2u, capabilities.minImageCount);
		if (capabilities.maxImageCount && m_MinImageCount > capabilities.maxImageCount)
			throw std::runtime_error("The editor requires a swapchain with at least two images");

		VkSwapchainCreateInfoKHR info{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		info.surface = m_Surface;
		info.minImageCount = m_MinImageCount;
		info.imageFormat = m_Format;
		info.imageColorSpace = format.colorSpace;
		info.imageExtent = m_Extent;
		info.imageArrayLayers = 1;
		info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		uint32_t families[] = { m_GraphicsFamily, m_PresentFamily };
		if (m_GraphicsFamily != m_PresentFamily)
		{
			info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			info.queueFamilyIndexCount = 2;
			info.pQueueFamilyIndices = families;
		}
		info.preTransform = capabilities.currentTransform;
		const VkCompositeAlphaFlagBitsKHR alphaModes[] = { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR };
		for (auto mode : alphaModes)
		{
			if (capabilities.supportedCompositeAlpha & mode) { info.compositeAlpha = mode; break; }
		}
		info.presentMode = presentMode;
		info.clipped = VK_TRUE;
		Check(vkCreateSwapchainKHR(m_Device, &info, nullptr, &m_Swapchain), "vkCreateSwapchainKHR");

		CreateWindowRenderPass();
		Check(vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &count, nullptr), "get swapchain images");
		std::vector<VkImage> images(count);
		Check(vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &count, images.data()), "get swapchain images");

		// 图像由交换链提供。编辑器为每张图创建绘制用的视图、帧缓冲，以及通知呈现队列的信号。
		m_Images.resize(count);
		for (uint32_t i = 0; i < count; i++)
		{
			auto& image = m_Images[i];
			VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			view.image = images[i];
			view.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view.format = m_Format;
			view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			Check(vkCreateImageView(m_Device, &view, nullptr, &image.View), "vkCreateImageView");

			VkFramebufferCreateInfo target{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
			target.renderPass = m_RenderPass;
			target.attachmentCount = 1;
			target.pAttachments = &image.View;
			target.width = m_Extent.width;
			target.height = m_Extent.height;
			target.layers = 1;
			Check(vkCreateFramebuffer(m_Device, &target, nullptr, &image.Framebuffer), "vkCreateFramebuffer");

			image.Ready = CreateSemaphore(m_Device);
			if (m_PresentFences)
				image.Presented = CreateFence(m_Device, false);
		}
		m_Rebuild = false;
		++m_SwapchainGeneration;
	}

	void VulkanContext::CreateWindowRenderPass()
	{
		VkAttachmentDescription color{};
		color.format = m_Format;
		color.samples = VK_SAMPLE_COUNT_1_BIT;
		color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference target{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkSubpassDescription pass{};
		pass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		pass.colorAttachmentCount = 1;
		pass.pColorAttachments = &target;

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
		info.attachmentCount = 1;
		info.pAttachments = &color;
		info.subpassCount = 1;
		info.pSubpasses = &pass;
		info.dependencyCount = 1;
		info.pDependencies = &dependency;
		Check(vkCreateRenderPass(m_Device, &info, nullptr, &m_RenderPass), "create window render pass");
	}

	void VulkanContext::DestroySwapchain()
	{
		for (const auto& image : m_Images)
		{
			// 支持呈现围栏时，等呈现队列用完这张图的 Ready 信号，再销毁该信号。
			if (image.WaitingForPresent)
				vkWaitForFences(m_Device, 1, &image.Presented, VK_TRUE, UINT64_MAX);
			vkDestroyFence(m_Device, image.Presented, nullptr);
			vkDestroySemaphore(m_Device, image.Ready, nullptr);
			vkDestroyFramebuffer(m_Device, image.Framebuffer, nullptr);
			vkDestroyImageView(m_Device, image.View, nullptr);
		}
		m_Images.clear();
		vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
		vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
		m_RenderPass = VK_NULL_HANDLE;
		m_Swapchain = VK_NULL_HANDLE;
	}

	// ---- 录制与提交 ----
	bool VulkanContext::BeginFrame()
	{
		int width, height;
		glfwGetFramebufferSize(m_Window, &width, &height);
		if (!width || !height || glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED))
			return false;
		if (m_Rebuild || m_WindowSize.width != width || m_WindowSize.height != height)
		{
			CreateSwapchain(width, height);
			if (m_Rebuild) return false;
		}

		Frame& frame = m_Frames[m_FrameIndex];
		Check(vkWaitForFences(m_Device, 1, &frame.Finished, VK_TRUE, UINT64_MAX), "wait for working frame");
		VkResult result = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, frame.ImageAvailable, VK_NULL_HANDLE, &m_ImageIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			m_Rebuild = true;
			return false;
		}
		if (result == VK_SUBOPTIMAL_KHR) m_Rebuild = true;
		else Check(result, "vkAcquireNextImageKHR");

		WindowImage& image = m_Images[m_ImageIndex];
		if (image.WaitingForPresent)
		{
			Check(vkWaitForFences(m_Device, 1, &image.Presented, VK_TRUE, UINT64_MAX), "wait for window presentation");
			Check(vkResetFences(m_Device, 1, &image.Presented), "reset presentation fence");
			image.WaitingForPresent = false;
		}
		Check(vkResetCommandPool(m_Device, frame.Pool, 0), "reset working frame");
		BeginCommands(frame.Commands);
		m_FrameOpen = true;
		m_WindowDrawn = false;
		return true;
	}

	void VulkanContext::BeginWindowPass()
	{
		VkClearValue clear{};
		clear.color = { { 0.08f, 0.08f, 0.08f, 1.0f } };
		VkRenderPassBeginInfo begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		begin.renderPass = m_RenderPass;
		begin.framebuffer = m_Images[m_ImageIndex].Framebuffer;
		begin.renderArea.extent = m_Extent;
		begin.clearValueCount = 1;
		begin.pClearValues = &clear;
		vkCmdBeginRenderPass(GetCommandBuffer(), &begin, VK_SUBPASS_CONTENTS_INLINE);
	}

	void VulkanContext::EndWindowPass()
	{
		vkCmdEndRenderPass(GetCommandBuffer());
		m_WindowDrawn = true;
	}

	void VulkanContext::SwapBuffers()
	{
		if (!m_FrameOpen) return;
		if (!m_WindowDrawn)
		{
			BeginWindowPass();
			EndWindowPass();
		}
		auto& frame = m_Frames[m_FrameIndex];
		auto& image = m_Images[m_ImageIndex];
		Check(vkEndCommandBuffer(frame.Commands), "vkEndCommandBuffer");

		// 这里提交面板拾取和界面绘制。给交换链图像写颜色前，先等窗口交出这张图。
		VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &frame.ImageAvailable;
		submit.pWaitDstStageMask = &waitStage;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &frame.Commands;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &image.Ready;
		Check(vkResetFences(m_Device, 1, &frame.Finished), "reset working frame fence");
		Check(vkQueueSubmit(m_GraphicsQueue, 1, &submit, frame.Finished), "vkQueueSubmit");

		VkSwapchainPresentFenceInfoEXT completion{ VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT };
		completion.swapchainCount = 1;
		completion.pFences = &image.Presented;
		VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		present.pNext = m_PresentFences ? &completion : nullptr;
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = &image.Ready;
		present.swapchainCount = 1;
		present.pSwapchains = &m_Swapchain;
		present.pImageIndices = &m_ImageIndex;
		VkResult result = vkQueuePresentKHR(m_PresentQueue, &present);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) m_Rebuild = true;
		else Check(result, "vkQueuePresentKHR");

		image.WaitingForPresent = m_PresentFences;
		m_FrameOpen = false;
		m_FrameIndex = (m_FrameIndex + 1) % static_cast<uint32_t>(m_Frames.size());
	}

	void VulkanContext::SetVSync(bool enabled)
	{
		if (enabled != m_VSync)
		{
			m_VSync = enabled;
			m_Rebuild = true;
		}
	}

	void VulkanContext::ImmediateSubmit(const std::function<void(VkCommandBuffer)>& record, Work work)
	{
		// 第一次画模型时可能要临时上传贴图。上传单独用 m_Resources，这里重置命令池时就不会清掉 m_Surfaces 正在录的绘制。
		auto& batch = work == Work::Surfaces ? m_Surfaces : m_Resources;
		Check(vkResetCommandPool(m_Device, batch.Pool, 0), "reset immediate commands");
		BeginCommands(batch.Commands);
		record(batch.Commands);
		Check(vkEndCommandBuffer(batch.Commands), "finish immediate commands");

		VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &batch.Commands;
		Check(vkQueueSubmit(m_GraphicsQueue, 1, &submit, batch.Finished), "submit immediate commands");
		Check(vkWaitForFences(m_Device, 1, &batch.Finished, VK_TRUE, UINT64_MAX), "wait for immediate commands");
		Check(vkResetFences(m_Device, 1, &batch.Finished), "reset immediate fence");
	}

	void VulkanContext::WaitIdle() const
	{
		Check(vkDeviceWaitIdle(m_Device), "vkDeviceWaitIdle");
	}

	VulkanContext::~VulkanContext()
	{
		if (m_Device)
		{
			vkDeviceWaitIdle(m_Device);
			DestroySwapchain();
			for (auto batch : { &m_Resources, &m_Surfaces })
			{
				vkDestroyFence(m_Device, batch->Finished, nullptr);
				vkDestroyCommandPool(m_Device, batch->Pool, nullptr);
			}
			for (auto& frame : m_Frames)
			{
				vkDestroyFence(m_Device, frame.Finished, nullptr);
				vkDestroySemaphore(m_Device, frame.ImageAvailable, nullptr);
				vkDestroyCommandPool(m_Device, frame.Pool, nullptr);
			}
			vkDestroyDevice(m_Device, nullptr);
		}
		if (m_Surface) vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
		if (m_DebugMessenger)
		{
			auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT"));
			destroy(m_Instance, m_DebugMessenger, nullptr);
		}
		if (m_Instance) vkDestroyInstance(m_Instance, nullptr);
	}
}
