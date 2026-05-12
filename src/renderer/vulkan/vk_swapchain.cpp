#include "vk_swapchain.hpp"
#include "vk_init.hpp"
#include "platform.hpp"
#include "log.hpp"
#include "memory.hpp"

namespace vk {

	static Swapchain sc = {};

	Swapchain& swapchain() { return sc; }

	// --- selection ---

	static VkSurfaceFormatKHR choose_surface_format() {
		Context& c = context();

		u32 count = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(c.physical_device, c.surface, &count, nullptr);

		VkSurfaceFormatKHR formats[64];
		if (count > 64) count = 64;
		vkGetPhysicalDeviceSurfaceFormatsKHR(c.physical_device, c.surface, &count, formats);

		// prefer BGRA8 SRGB
		for (u32 i = 0; i < count; i++) {
			if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
				formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				return formats[i];
			}
		}

		return formats[0];
	}

	static VkPresentModeKHR choose_present_mode() {
		Context& c = context();

		u32 count = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(c.physical_device, c.surface, &count, nullptr);

		VkPresentModeKHR modes[16];
		if (count > 16) count = 16;
		vkGetPhysicalDeviceSurfacePresentModesKHR(c.physical_device, c.surface, &count, modes);

		// prefer mailbox for unthrottled benchmarking
		for (u32 i = 0; i < count; i++) {
			if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
				return VK_PRESENT_MODE_MAILBOX_KHR;
			}
		}

		// FIFO is guaranteed to be available
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	static VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& caps) {
		// 0xFFFFFFFF is a sentinel meaning the surface size is determined by the swapchain
		if (caps.currentExtent.width != 0xFFFFFFFF) {
			return caps.currentExtent;
		}

		VkExtent2D extent = {
			(u32)platform::window_width(),
			(u32)platform::window_height()
		};

		if (extent.width < caps.minImageExtent.width)  extent.width = caps.minImageExtent.width;
		if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
		if (extent.width > caps.maxImageExtent.width)  extent.width = caps.maxImageExtent.width;
		if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;

		return extent;
	}

	// --- public ---

	bool create_swapchain() {
		Context& c = context();

		VkSurfaceCapabilitiesKHR caps;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c.physical_device, c.surface, &caps);

		VkSurfaceFormatKHR surface_format = choose_surface_format();
		VkPresentModeKHR present_mode = choose_present_mode();
		VkExtent2D extent = choose_extent(caps);

		if (extent.width == 0 || extent.height == 0) {
			// minimized window; defer creation until restored
			logger::warn("Swapchain creation skipped: window has zero extent");
			return false;
		}

		// request 3 images (triple buffering), clamped to driver limits
		u32 image_count = 3;
		if (image_count < caps.minImageCount) image_count = caps.minImageCount;
		if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
			image_count = caps.maxImageCount;
		}

		VkSwapchainCreateInfoKHR info = {};
		info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		info.surface = c.surface;
		info.minImageCount = image_count;
		info.imageFormat = surface_format.format;
		info.imageColorSpace = surface_format.colorSpace;
		info.imageExtent = extent;
		info.imageArrayLayers = 1;
		// COLOR_ATTACHMENT for direct rendering, TRANSFER_DST so the tonemap pass can blit if needed
		info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		u32 queue_indices[2] = { c.graphics_queue_index, c.present_queue_index };
		if (c.graphics_queue_index != c.present_queue_index) {
			info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			info.queueFamilyIndexCount = 2;
			info.pQueueFamilyIndices = queue_indices;
		} else {
			info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}

		info.preTransform = caps.currentTransform;
		info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		info.presentMode = present_mode;
		info.clipped = VK_TRUE;
		info.oldSwapchain = VK_NULL_HANDLE;

		VkResult result = vkCreateSwapchainKHR(c.device, &info, nullptr, &sc.handle);
		if (result != VK_SUCCESS) {
			logger::fatal("Failed to create swapchain");
			return false;
		}

		// retrieve the images the driver actually allocated
		u32 actual_count = 0;
		vkGetSwapchainImagesKHR(c.device, sc.handle, &actual_count, nullptr);
		if (actual_count > MAX_SWAPCHAIN_IMAGES) actual_count = MAX_SWAPCHAIN_IMAGES;
		vkGetSwapchainImagesKHR(c.device, sc.handle, &actual_count, sc.images);
		sc.image_count = actual_count;

		// create a view for each image
		for (u32 i = 0; i < sc.image_count; i++) {
			VkImageViewCreateInfo view_info = {};
			view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			view_info.image = sc.images[i];
			view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view_info.format = surface_format.format;
			view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			view_info.subresourceRange.baseMipLevel = 0;
			view_info.subresourceRange.levelCount = 1;
			view_info.subresourceRange.baseArrayLayer = 0;
			view_info.subresourceRange.layerCount = 1;

			result = vkCreateImageView(c.device, &view_info, nullptr, &sc.image_views[i]);
			if (result != VK_SUCCESS) {
				logger::fatal("Failed to create swapchain image view");
				return false;
			}
		}

		sc.format = surface_format.format;
		sc.color_space = surface_format.colorSpace;
		sc.extent = extent;
		sc.present_mode = present_mode;

		const char* mode_str = (present_mode == VK_PRESENT_MODE_MAILBOX_KHR) ? "MAILBOX" : "FIFO";

		return true;
	}

	void destroy_swapchain() {
		Context& c = context();

		for (u32 i = 0; i < sc.image_count; i++) {
			if (sc.image_views[i]) {
				vkDestroyImageView(c.device, sc.image_views[i], nullptr);
			}
		}
		if (sc.handle) {
			vkDestroySwapchainKHR(c.device, sc.handle, nullptr);
		}

		memory::set(&sc, 0, sizeof(sc));
	}

	bool recreate_swapchain() {
		Context& c = context();
		vkDeviceWaitIdle(c.device);
		destroy_swapchain();
		return create_swapchain();
	}

}
