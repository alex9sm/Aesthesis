#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "vk_backend.hpp"
#include "platform.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "string.hpp"

namespace vk {

	static constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;

	struct Context {
		VkInstance instance;
		VkSurfaceKHR surface;
		VkPhysicalDevice physical_device;
		VkDevice device;

		u32 graphics_queue_index;
		u32 present_queue_index;
		VkQueue graphics_queue;
		VkQueue present_queue;

		VkSwapchainKHR swapchain;
		VkFormat swapchain_format;
		VkExtent2D swapchain_extent;
		VkImage swapchain_images[8];
		VkImageView swapchain_image_views[8];
		u32 swapchain_image_count;

		VkCommandPool command_pool;
		VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];

		VkSemaphore image_available[MAX_FRAMES_IN_FLIGHT];
		VkSemaphore render_finished[MAX_FRAMES_IN_FLIGHT];
		VkFence in_flight[MAX_FRAMES_IN_FLIGHT];

		u32 current_frame;
		u32 image_index;
		bool framebuffer_resized;
	};

	static Context ctx = {};

	// --- instance ---

	static bool create_instance() {
		VkApplicationInfo app_info = {};
		app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app_info.pApplicationName = "Aesthesis";
		app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
		app_info.pEngineName = "Aesthesis";
		app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
		app_info.apiVersion = VK_API_VERSION_1_3;

		const char* extensions[] = {
			VK_KHR_SURFACE_EXTENSION_NAME,
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
		};

		VkInstanceCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		create_info.pApplicationInfo = &app_info;
		create_info.enabledExtensionCount = 2;
		create_info.ppEnabledExtensionNames = extensions;
		create_info.enabledLayerCount = 0;

		VkResult result = vkCreateInstance(&create_info, nullptr, &ctx.instance);
		if (result != VK_SUCCESS) {
			logger::fatal("Failed to create Vulkan instance");
			return false;
		}

		logger::info("Vulkan instance created");
		return true;
	}

	// --- surface ---

	static bool create_surface() {
		VkWin32SurfaceCreateInfoKHR create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		create_info.hinstance = (HINSTANCE)platform::native_instance();
		create_info.hwnd = (HWND)platform::native_window();

		VkResult result = vkCreateWin32SurfaceKHR(ctx.instance, &create_info, nullptr, &ctx.surface);
		if (result != VK_SUCCESS) {
			logger::fatal("Failed to create Vulkan surface");
			return false;
		}

		logger::info("Vulkan surface created");
		return true;
	}

	// --- physical device ---

	static bool pick_physical_device() {
		u32 count = 0;
		vkEnumeratePhysicalDevices(ctx.instance, &count, nullptr);
		if (count == 0) {
			logger::fatal("No Vulkan-capable GPUs found");
			return false;
		}

		VkPhysicalDevice devices[16];
		if (count > 16) count = 16;
		vkEnumeratePhysicalDevices(ctx.instance, &count, devices);

		// prefer discrete GPU
		for (u32 i = 0; i < count; i++) {
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(devices[i], &props);
			if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
				ctx.physical_device = devices[i];
				logger::info("Selected GPU: %s", props.deviceName);
				return true;
			}
		}

		// fallback to first device
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(devices[0], &props);
		ctx.physical_device = devices[0];
		logger::info("Selected GPU (fallback): %s", props.deviceName);
		return true;
	}

	// --- logical device ---

	static bool find_queue_families() {
		u32 count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(ctx.physical_device, &count, nullptr);

		VkQueueFamilyProperties families[64];
		if (count > 64) count = 64;
		vkGetPhysicalDeviceQueueFamilyProperties(ctx.physical_device, &count, families);

		bool found_graphics = false;
		bool found_present = false;

		for (u32 i = 0; i < count; i++) {
			if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				ctx.graphics_queue_index = i;
				found_graphics = true;
			}

			VkBool32 present_support = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(ctx.physical_device, i, ctx.surface, &present_support);
			if (present_support) {
				ctx.present_queue_index = i;
				found_present = true;
			}

			if (found_graphics && found_present) break;
		}

		return found_graphics && found_present;
	}

	static bool create_device() {
		if (!find_queue_families()) {
			logger::fatal("Failed to find required queue families");
			return false;
		}

		f32 priority = 1.0f;

		// handle case where graphics and present are the same queue
		VkDeviceQueueCreateInfo queue_infos[2] = {};
		u32 queue_count = 1;

		queue_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_infos[0].queueFamilyIndex = ctx.graphics_queue_index;
		queue_infos[0].queueCount = 1;
		queue_infos[0].pQueuePriorities = &priority;

		if (ctx.graphics_queue_index != ctx.present_queue_index) {
			queue_infos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_infos[1].queueFamilyIndex = ctx.present_queue_index;
			queue_infos[1].queueCount = 1;
			queue_infos[1].pQueuePriorities = &priority;
			queue_count = 2;
		}

		const char* device_extensions[] = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		};

		VkPhysicalDeviceFeatures features = {};

		VkDeviceCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		create_info.queueCreateInfoCount = queue_count;
		create_info.pQueueCreateInfos = queue_infos;
		create_info.enabledExtensionCount = 1;
		create_info.ppEnabledExtensionNames = device_extensions;
		create_info.pEnabledFeatures = &features;

		VkResult result = vkCreateDevice(ctx.physical_device, &create_info, nullptr, &ctx.device);
		if (result != VK_SUCCESS) {
			logger::fatal("Failed to create Vulkan logical device");
			return false;
		}

		vkGetDeviceQueue(ctx.device, ctx.graphics_queue_index, 0, &ctx.graphics_queue);
		vkGetDeviceQueue(ctx.device, ctx.present_queue_index, 0, &ctx.present_queue);

		logger::info("Vulkan device created");
		return true;
	}

	// --- swapchain ---

	static VkSurfaceFormatKHR choose_surface_format() {
		u32 count = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physical_device, ctx.surface, &count, nullptr);

		VkSurfaceFormatKHR formats[32];
		if (count > 32) count = 32;
		vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physical_device, ctx.surface, &count, formats);

		for (u32 i = 0; i < count; i++) {
			if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
				formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				return formats[i];
			}
		}

		return formats[0];
	}

	static VkPresentModeKHR choose_present_mode() {
		u32 count = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physical_device, ctx.surface, &count, nullptr);

		VkPresentModeKHR modes[16];
		if (count > 16) count = 16;
		vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physical_device, ctx.surface, &count, modes);

		// prefer mailbox (triple buffering, no tearing, low latency)
		for (u32 i = 0; i < count; i++) {
			if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) return modes[i];
		}

		return VK_PRESENT_MODE_FIFO_KHR;
	}

	static bool create_swapchain() {
		VkSurfaceCapabilitiesKHR caps;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physical_device, ctx.surface, &caps);

		VkSurfaceFormatKHR format = choose_surface_format();
		VkPresentModeKHR present_mode = choose_present_mode();

		VkExtent2D extent;
		if (caps.currentExtent.width != 0xFFFFFFFF) {
			extent = caps.currentExtent;
		} else {
			extent.width = (u32)platform::window_width();
			extent.height = (u32)platform::window_height();
		}

		u32 image_count = caps.minImageCount + 1;
		if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
			image_count = caps.maxImageCount;
		}

		VkSwapchainCreateInfoKHR create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		create_info.surface = ctx.surface;
		create_info.minImageCount = image_count;
		create_info.imageFormat = format.format;
		create_info.imageColorSpace = format.colorSpace;
		create_info.imageExtent = extent;
		create_info.imageArrayLayers = 1;
		create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		if (ctx.graphics_queue_index != ctx.present_queue_index) {
			u32 indices[] = { ctx.graphics_queue_index, ctx.present_queue_index };
			create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			create_info.queueFamilyIndexCount = 2;
			create_info.pQueueFamilyIndices = indices;
		} else {
			create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}

		create_info.preTransform = caps.currentTransform;
		create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		create_info.presentMode = present_mode;
		create_info.clipped = VK_TRUE;
		create_info.oldSwapchain = VK_NULL_HANDLE;

		VkResult result = vkCreateSwapchainKHR(ctx.device, &create_info, nullptr, &ctx.swapchain);
		if (result != VK_SUCCESS) {
			logger::fatal("Failed to create swapchain");
			return false;
		}

		ctx.swapchain_format = format.format;
		ctx.swapchain_extent = extent;

		vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &ctx.swapchain_image_count, nullptr);
		if (ctx.swapchain_image_count > 8) ctx.swapchain_image_count = 8;
		vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &ctx.swapchain_image_count, ctx.swapchain_images);

		for (u32 i = 0; i < ctx.swapchain_image_count; i++) {
			VkImageViewCreateInfo view_info = {};
			view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			view_info.image = ctx.swapchain_images[i];
			view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view_info.format = ctx.swapchain_format;
			view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			view_info.subresourceRange.baseMipLevel = 0;
			view_info.subresourceRange.levelCount = 1;
			view_info.subresourceRange.baseArrayLayer = 0;
			view_info.subresourceRange.layerCount = 1;

			result = vkCreateImageView(ctx.device, &view_info, nullptr, &ctx.swapchain_image_views[i]);
			if (result != VK_SUCCESS) {
				logger::fatal("Failed to create swapchain image view");
				return false;
			}
		}

		logger::info("Swapchain created (%dx%d, %d images)", extent.width, extent.height, ctx.swapchain_image_count);
		return true;
	}

	static void destroy_swapchain() {
		for (u32 i = 0; i < ctx.swapchain_image_count; i++) {
			if (ctx.swapchain_image_views[i]) {
				vkDestroyImageView(ctx.device, ctx.swapchain_image_views[i], nullptr);
			}
		}
		if (ctx.swapchain) {
			vkDestroySwapchainKHR(ctx.device, ctx.swapchain, nullptr);
		}
	}

	// --- command buffers ---

	static bool create_command_pool() {
		VkCommandPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		pool_info.queueFamilyIndex = ctx.graphics_queue_index;

		VkResult result = vkCreateCommandPool(ctx.device, &pool_info, nullptr, &ctx.command_pool);
		if (result != VK_SUCCESS) {
			logger::fatal("Failed to create command pool");
			return false;
		}

		VkCommandBufferAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.commandPool = ctx.command_pool;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

		result = vkAllocateCommandBuffers(ctx.device, &alloc_info, ctx.command_buffers);
		if (result != VK_SUCCESS) {
			logger::fatal("Failed to allocate command buffers");
			return false;
		}

		return true;
	}

	// --- sync ---

	static bool create_sync_objects() {
		VkSemaphoreCreateInfo sem_info = {};
		sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fence_info = {};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			if (vkCreateSemaphore(ctx.device, &sem_info, nullptr, &ctx.image_available[i]) != VK_SUCCESS ||
				vkCreateSemaphore(ctx.device, &sem_info, nullptr, &ctx.render_finished[i]) != VK_SUCCESS ||
				vkCreateFence(ctx.device, &fence_info, nullptr, &ctx.in_flight[i]) != VK_SUCCESS) {
				logger::fatal("Failed to create sync objects");
				return false;
			}
		}

		return true;
	}

	// --- public ---

	bool init() {
		if (!create_instance()) return false;
		if (!create_surface()) return false;
		if (!pick_physical_device()) return false;
		if (!create_device()) return false;
		if (!create_swapchain()) return false;
		if (!create_command_pool()) return false;
		if (!create_sync_objects()) return false;

		logger::info("Vulkan backend initialized");
		return true;
	}

	void shutdown() {
		vkDeviceWaitIdle(ctx.device);

		for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			vkDestroySemaphore(ctx.device, ctx.image_available[i], nullptr);
			vkDestroySemaphore(ctx.device, ctx.render_finished[i], nullptr);
			vkDestroyFence(ctx.device, ctx.in_flight[i], nullptr);
		}

		vkDestroyCommandPool(ctx.device, ctx.command_pool, nullptr);
		destroy_swapchain();
		vkDestroyDevice(ctx.device, nullptr);
		vkDestroySurfaceKHR(ctx.instance, ctx.surface, nullptr);
		vkDestroyInstance(ctx.instance, nullptr);

		memory::set(&ctx, 0, sizeof(ctx));
		logger::info("Vulkan backend shutdown");
	}

	void begin_frame() {
		vkWaitForFences(ctx.device, 1, &ctx.in_flight[ctx.current_frame], VK_TRUE, UINT64_MAX);

		VkResult result = vkAcquireNextImageKHR(
			ctx.device, ctx.swapchain, UINT64_MAX,
			ctx.image_available[ctx.current_frame], VK_NULL_HANDLE, &ctx.image_index
		);

		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			// TODO: recreate swapchain
			return;
		}

		vkResetFences(ctx.device, 1, &ctx.in_flight[ctx.current_frame]);
		vkResetCommandBuffer(ctx.command_buffers[ctx.current_frame], 0);

		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer(ctx.command_buffers[ctx.current_frame], &begin_info);

		// transition swapchain image to color attachment
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = ctx.swapchain_images[ctx.image_index];
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		vkCmdPipelineBarrier(
			ctx.command_buffers[ctx.current_frame],
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier
		);

		// begin dynamic rendering with a clear color
		VkRenderingAttachmentInfo color_attachment = {};
		color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachment.imageView = ctx.swapchain_image_views[ctx.image_index];
		color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.clearValue.color = { { 0.1f, 0.1f, 0.1f, 1.0f } };

		VkRenderingInfo rendering_info = {};
		rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		rendering_info.renderArea.offset = { 0, 0 };
		rendering_info.renderArea.extent = ctx.swapchain_extent;
		rendering_info.layerCount = 1;
		rendering_info.colorAttachmentCount = 1;
		rendering_info.pColorAttachments = &color_attachment;

		vkCmdBeginRendering(ctx.command_buffers[ctx.current_frame], &rendering_info);
	}

	void end_frame() {
		VkCommandBuffer cmd = ctx.command_buffers[ctx.current_frame];

		vkCmdEndRendering(cmd);

		// transition swapchain image to present
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = ctx.swapchain_images[ctx.image_index];
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = 0;

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier
		);

		vkEndCommandBuffer(cmd);

		// submit
		VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &ctx.image_available[ctx.current_frame];
		submit_info.pWaitDstStageMask = &wait_stage;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &cmd;
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &ctx.render_finished[ctx.current_frame];

		vkQueueSubmit(ctx.graphics_queue, 1, &submit_info, ctx.in_flight[ctx.current_frame]);

		// present
		VkPresentInfoKHR present_info = {};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = &ctx.render_finished[ctx.current_frame];
		present_info.swapchainCount = 1;
		present_info.pSwapchains = &ctx.swapchain;
		present_info.pImageIndices = &ctx.image_index;

		vkQueuePresentKHR(ctx.present_queue, &present_info);

		ctx.current_frame = (ctx.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

}
