#include "api.hpp"
#include "vk_init.hpp"
#include "log.hpp"

namespace renderer {

	bool init() {
		if (!vk::init()) {
			logger::fatal("Failed to initialize Vulkan");
			return false;
		}
		logger::info("Renderer initialized");
		return true;
	}

	void shutdown() {
		vk::shutdown();
		logger::info("Renderer shutdown");
	}

}
