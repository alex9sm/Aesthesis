#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include "types.hpp"
#include "math.hpp"
#include "vk_mesh.hpp"

namespace vk {

	struct GBufferImage {
		VkImage image;
		VkImageView view;
		VmaAllocation alloc;
		VkFormat format;
	};

	struct GBuffer {
		GBufferImage albedo;    // RGBA8_UNORM
		GBufferImage normal;    // RGBA16F
		GBufferImage material;  // RG8_UNORM
		GBufferImage depth;     // D32_SFLOAT
		VkExtent2D extent;
	};

	struct GBufferDraw {
		MeshHandle mesh;
		mat4 model;
		vec4 color;
	};

	bool init_gbuffer();
	void shutdown_gbuffer();
	bool resize_gbuffer(VkExtent2D extent);

	void execute_gbuffer_pass(VkCommandBuffer cmd,
		const mat4& view, const mat4& projection,
		const GBufferDraw* draws, u32 draw_count);

	GBuffer& gbuffer();

}
