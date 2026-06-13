#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"

namespace vk {

	// shared pipeline builders. each pass fills only the fields that vary; all
	// the constant scaffolding (input assembly, viewport, multisample, dynamic
	// state, dynamic-rendering wiring, shader load/destroy) lives in one place.

	enum class BlendMode {
		Opaque,     // no blending
		AlphaBlend, // src_alpha / one_minus_src_alpha, premultiplied-style alpha
	};

	struct GraphicsPipelineSpec {
		const char* vs_path;
		const char* fs_path; // null => depth-only (no fragment stage)

		// vertex input. leave vertex_binding null for fullscreen passes (the
		// vertex shader generates positions from gl_VertexIndex).
		const VkVertexInputBindingDescription*   vertex_binding;
		const VkVertexInputAttributeDescription* vertex_attrs;
		u32 vertex_attr_count;

		VkCullModeFlags cull;          // 0 == VK_CULL_MODE_NONE
		VkBool32        depth_test;
		VkBool32        depth_write;
		VkCompareOp     depth_compare; // only consulted when depth_test

		// dynamic-rendering attachment formats
		const VkFormat* color_formats;
		u32             color_count;
		VkFormat        depth_format;  // VK_FORMAT_UNDEFINED == no depth attachment
		BlendMode       blend;         // applied to every color attachment

		// pipeline layout
		const VkDescriptorSetLayout* set_layouts;
		u32                          set_layout_count;
		const VkPushConstantRange*   push_constant; // null => none
	};

	bool create_graphics_pipeline(const GraphicsPipelineSpec& spec,
		VkPipeline* out_pipeline, VkPipelineLayout* out_layout);

	struct ComputePipelineSpec {
		const char* cs_path;
		const VkDescriptorSetLayout* set_layouts;
		u32                          set_layout_count;
		const VkPushConstantRange*   push_constant; // null => none
	};

	bool create_compute_pipeline(const ComputePipelineSpec& spec,
		VkPipeline* out_pipeline, VkPipelineLayout* out_layout);

}
