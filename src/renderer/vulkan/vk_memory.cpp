#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

#include "vk_memory.hpp"
#include "vk_init.hpp"
#include "log.hpp"

namespace vk {

	static VmaAllocator vma = nullptr;

	VmaAllocator allocator() { return vma; }

	bool init_memory() {
		Context& c = context();

		VmaAllocatorCreateInfo info = {};
		info.vulkanApiVersion = VK_API_VERSION_1_3;
		info.physicalDevice = c.physical_device;
		info.device = c.device;
		info.instance = c.instance;

		VkResult result = vmaCreateAllocator(&info, &vma);
		if (result != VK_SUCCESS) {
			logger::fatal("Failed to create VMA allocator");
			return false;
		}

		logger::info("VMA allocator created");
		return true;
	}

	void shutdown_memory() {
		if (vma) {
			vmaDestroyAllocator(vma);
			vma = nullptr;
		}
	}

}
