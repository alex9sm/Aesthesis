#include "vk_pch.hpp"
#include "vk_targets.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "log.hpp"

namespace vk {

	static Targets t = {};

	Targets& targets() { return t; }

	// --- helpers ---

	static bool create_image(RenderImage* out, VkFormat format, VkExtent2D extent,
		VkImageUsageFlags usage, VkImageAspectFlags aspect)
	{
		Context& c = context();
		VmaAllocator a = allocator();

		VkImageCreateInfo image_ci = {};
		image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_ci.imageType = VK_IMAGE_TYPE_2D;
		image_ci.format = format;
		image_ci.extent = { extent.width, extent.height, 1 };
		image_ci.mipLevels = 1;
		image_ci.arrayLayers = 1;
		image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
		image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_ci.usage = usage;
		image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo alloc_ci = {};
		alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;

		if (vmaCreateImage(a, &image_ci, &alloc_ci, &out->image, &out->alloc, nullptr) != VK_SUCCESS) {
			logger::error("vmaCreateImage failed");
			return false;
		}

		VkImageViewCreateInfo view_ci = {};
		view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_ci.image = out->image;
		view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_ci.format = format;
		view_ci.subresourceRange.aspectMask = aspect;
		view_ci.subresourceRange.baseMipLevel = 0;
		view_ci.subresourceRange.levelCount = 1;
		view_ci.subresourceRange.baseArrayLayer = 0;
		view_ci.subresourceRange.layerCount = 1;

		if (vkCreateImageView(c.device, &view_ci, nullptr, &out->view) != VK_SUCCESS) {
			logger::error("vkCreateImageView failed");
			return false;
		}

		out->format = format;
		out->state = ResState::Undefined;
		return true;
	}

	static void destroy_image(RenderImage* img) {
		Context& c = context();
		if (img->view)  vkDestroyImageView(c.device, img->view, nullptr);
		if (img->image) vmaDestroyImage(allocator(), img->image, img->alloc);
		*img = {};
	}

	static bool create_all(VkExtent2D extent) {
		VkImageUsageFlags color_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
			| VK_IMAGE_USAGE_SAMPLED_BIT
			| VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		VkImageUsageFlags depth_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
			| VK_IMAGE_USAGE_SAMPLED_BIT;
		VkImageUsageFlags hdr_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
			| VK_IMAGE_USAGE_SAMPLED_BIT;

		if (!create_image(&t.albedo,    VK_FORMAT_R8G8B8A8_UNORM,      extent, color_usage, VK_IMAGE_ASPECT_COLOR_BIT)) return false;
		if (!create_image(&t.normal,    VK_FORMAT_R16G16_SFLOAT,       extent, color_usage, VK_IMAGE_ASPECT_COLOR_BIT)) return false;
		if (!create_image(&t.material,  VK_FORMAT_R8G8_UNORM,          extent, color_usage, VK_IMAGE_ASPECT_COLOR_BIT)) return false;
		if (!create_image(&t.depth,     VK_FORMAT_D32_SFLOAT,          extent, depth_usage, VK_IMAGE_ASPECT_DEPTH_BIT)) return false;
		if (!create_image(&t.scene_hdr, VK_FORMAT_R16G16B16A16_SFLOAT, extent, hdr_usage,   VK_IMAGE_ASPECT_COLOR_BIT)) return false;

		t.extent = extent;
		return true;
	}

	static void destroy_all() {
		destroy_image(&t.albedo);
		destroy_image(&t.normal);
		destroy_image(&t.material);
		destroy_image(&t.depth);
		destroy_image(&t.scene_hdr);
		t.extent = {};
	}

	// --- public ---

	bool init_targets(VkExtent2D extent) {
		return create_all(extent);
	}

	void shutdown_targets() {
		destroy_all();
	}

	bool resize_targets(VkExtent2D extent) {
		Context& c = context();
		vkDeviceWaitIdle(c.device);
		destroy_all();
		return create_all(extent);
	}

	// --- resize-callback registry ---

	static constexpr u32 MAX_TARGET_CONSUMERS = 16;
	static void(*consumers[MAX_TARGET_CONSUMERS])() = {};
	static u32 consumer_count = 0;

	void register_target_consumer(void(*refresh)()) {
		if (consumer_count >= MAX_TARGET_CONSUMERS) {
			logger::error("register_target_consumer: registry full");
			return;
		}
		consumers[consumer_count++] = refresh;
	}

	void refresh_target_consumers() {
		for (u32 i = 0; i < consumer_count; i++) consumers[i]();
	}

	// --- barrier state machine ---

	struct StateInfo {
		VkImageLayout        layout;
		VkAccessFlags        access;
		VkPipelineStageFlags stage;
	};

	// Single source of truth: maps a semantic state to the layout it lives in
	// plus the access/stage scope to synchronize against. Depth states use the
	// EARLY|LATE fragment-test superset so each is correct both as a barrier
	// source and destination.
	static StateInfo state_info(ResState s) {
		switch (s) {
		case ResState::ColorWrite:
			return { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		case ResState::DepthWrite:
			return { VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT };
		case ResState::DepthRead:
			return { VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT };
		case ResState::ShaderRead:
			return { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_ACCESS_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT };
		case ResState::Present:
			return { VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT };
		case ResState::Undefined:
		default:
			return { VK_IMAGE_LAYOUT_UNDEFINED, 0,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
		}
	}

	static VkImageAspectFlags aspect_for_format(VkFormat f) {
		return (f == VK_FORMAT_D32_SFLOAT) ? VK_IMAGE_ASPECT_DEPTH_BIT
			: VK_IMAGE_ASPECT_COLOR_BIT;
	}

	static void emit_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
		StateInfo from, StateInfo to)
	{
		VkImageMemoryBarrier b = {};
		b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		b.oldLayout = from.layout;
		b.newLayout = to.layout;
		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = image;
		b.subresourceRange.aspectMask = aspect;
		b.subresourceRange.levelCount = 1;
		b.subresourceRange.layerCount = 1;
		b.srcAccessMask = from.access;
		b.dstAccessMask = to.access;
		vkCmdPipelineBarrier(cmd, from.stage, to.stage, 0, 0, nullptr, 0, nullptr, 1, &b);
	}

	void transition(VkCommandBuffer cmd, RenderImage& img, ResState new_state) {
		emit_barrier(cmd, img.image, aspect_for_format(img.format),
			state_info(img.state), state_info(new_state));
		img.state = new_state;
	}

	void transition_discard(VkCommandBuffer cmd, RenderImage& img, ResState new_state) {
		emit_barrier(cmd, img.image, aspect_for_format(img.format),
			state_info(ResState::Undefined), state_info(new_state));
		img.state = new_state;
	}

	void transition_raw(VkCommandBuffer cmd, VkImage image, ResState from, ResState to) {
		emit_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
			state_info(from), state_info(to));
	}

}
