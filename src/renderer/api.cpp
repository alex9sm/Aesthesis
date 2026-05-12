#include "api.hpp"
#include "vk_backend.hpp"
#include "log.hpp"

namespace renderer {

	bool init() {
		if (!vk::init()) {
			logger::fatal("Failed to initialize Vulkan backend");
			return false;
		}
		logger::info("Renderer initialized");
		return true;
	}

	void shutdown() {
		vk::shutdown();
		logger::info("Renderer shutdown");
	}

	void begin_frame(const mat4& view, const mat4& projection) {
		vk::begin_frame();
	}

	void end_frame() {
		vk::end_frame();
	}

}
