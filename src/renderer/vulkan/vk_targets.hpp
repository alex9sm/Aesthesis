#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include "types.hpp"

namespace vk {

	struct RenderImage {
		VkImage       image;
		VkImageView   view;
		VmaAllocation alloc;
		VkFormat      format;
		VkImageLayout layout; // tracked across passes
	};

	struct Targets {
		RenderImage albedo;     // RGBA8_UNORM
		RenderImage normal;     // RG16F (octahedral-encoded world normal)
		RenderImage material;   // RG8_UNORM
		RenderImage depth;      // D32_SFLOAT
		RenderImage scene_hdr;  // RGBA16F   (lighting output)
		VkExtent2D  extent;
	};

	bool init_targets(VkExtent2D extent);
	void shutdown_targets();
	bool resize_targets(VkExtent2D extent);

	Targets& targets();

	// shared helper: transitions an image and updates its tracked layout
	void transition_image(VkCommandBuffer cmd, RenderImage& img,
		VkImageAspectFlags aspect,
		VkImageLayout new_layout,
		VkAccessFlags src_access, VkAccessFlags dst_access,
		VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage);

}
