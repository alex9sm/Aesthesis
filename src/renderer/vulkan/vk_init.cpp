#include "vk_init.hpp"
#include "vk_swapchain.hpp"
#include "vk_memory.hpp"
#include "vk_frame.hpp"
#include "vk_mesh.hpp"
#include "vk_targets.hpp"
#include "vk_globals.hpp"
#include "vk_instance.hpp"
#include "vk_texture.hpp"
#include "vk_cubemap.hpp"
#include "vk_material.hpp"
#include "vk_ibl.hpp"
#include "vk_gbuffer.hpp"
#include "vk_lighting.hpp"
#include "vk_debug.hpp"
#include "platform.hpp"
#include "log.hpp"
#include "memory.hpp"

namespace vk {

	static Context ctx = {};

	Context& context() { return ctx; }

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

		// enable Vulkan 1.3 dynamic rendering (no VkRenderPass / VkFramebuffer objects)
		VkPhysicalDeviceVulkan13Features features13 = {};
		features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		features13.dynamicRendering = VK_TRUE;

		// Vulkan 1.2 descriptor indexing for the bindless texture array
		VkPhysicalDeviceVulkan12Features features12 = {};
		features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
		features12.runtimeDescriptorArray = VK_TRUE;
		features12.descriptorBindingPartiallyBound = VK_TRUE;
		features12.pNext = &features13;

		VkPhysicalDeviceFeatures2 features2 = {};
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.pNext = &features12;

		VkDeviceCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		create_info.pNext = &features2;
		create_info.queueCreateInfoCount = queue_count;
		create_info.pQueueCreateInfos = queue_infos;
		create_info.enabledExtensionCount = 1;
		create_info.ppEnabledExtensionNames = device_extensions;
		create_info.pEnabledFeatures = nullptr;  // using pNext->VkPhysicalDeviceFeatures2 instead

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

	// --- public ---

	bool init() {
		if (!create_instance()) return false;
		if (!create_surface()) return false;
		if (!pick_physical_device()) return false;
		if (!create_device()) return false;
		if (!init_memory()) return false;
		if (!create_swapchain()) return false;
		if (!init_frames()) return false;
		if (!init_meshes()) return false;
		if (!init_targets(swapchain().extent)) return false;
		if (!init_globals()) return false;
		if (!init_instances()) return false;
		if (!init_textures()) return false;
		if (!init_cubemaps()) return false;
		if (!init_materials()) return false;
		if (!init_ibl()) return false;
		if (!init_gbuffer()) return false;
		if (!init_lighting()) return false;
		if (!init_debug()) return false;
		lighting_refresh_descriptors();
		debug_refresh_descriptors();

		logger::info("Vulkan initialized");
		return true;
	}

	void shutdown() {
		if (ctx.device) {
			vkDeviceWaitIdle(ctx.device);
			shutdown_debug();
			shutdown_lighting();
			shutdown_gbuffer();
			shutdown_ibl();
			shutdown_materials();
			shutdown_cubemaps();
			shutdown_textures();
			shutdown_instances();
			shutdown_globals();
			shutdown_targets();
			shutdown_meshes();
			shutdown_frames();
			destroy_swapchain();
			shutdown_memory();
			vkDestroyDevice(ctx.device, nullptr);
		}
		if (ctx.surface) vkDestroySurfaceKHR(ctx.instance, ctx.surface, nullptr);
		if (ctx.instance) vkDestroyInstance(ctx.instance, nullptr);

		memory::set(&ctx, 0, sizeof(ctx));
		logger::info("Vulkan shutdown");
	}

}
