#include "vk_debug_blit.hpp"
#include "vk_init.hpp"
#include "vk_swapchain.hpp"
#include "vk_gbuffer.hpp"

namespace vk {

	static void transition(VkCommandBuffer cmd, VkImage image,
		VkImageLayout from, VkImageLayout to,
		VkAccessFlags src_access, VkAccessFlags dst_access,
		VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
	{
		VkImageMemoryBarrier b = {};
		b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		b.oldLayout = from;
		b.newLayout = to;
		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = image;
		b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		b.subresourceRange.levelCount = 1;
		b.subresourceRange.layerCount = 1;
		b.srcAccessMask = src_access;
		b.dstAccessMask = dst_access;
		vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
	}

	void execute_debug_blit(VkCommandBuffer cmd, u32 swapchain_image_index) {
		Swapchain& sc = swapchain();
		GBuffer& gb = gbuffer();
		VkImage dst = sc.images[swapchain_image_index];

		// swapchain image: UNDEFINED -> TRANSFER_DST
		transition(cmd, dst,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			0, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

		// blit (handles UNORM->SRGB conversion automatically)
		VkImageBlit blit = {};
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.layerCount = 1;
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { (i32)gb.extent.width, (i32)gb.extent.height, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.layerCount = 1;
		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { (i32)sc.extent.width, (i32)sc.extent.height, 1 };

		vkCmdBlitImage(cmd,
			gb.albedo.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit, VK_FILTER_LINEAR);

		// swapchain image: TRANSFER_DST -> PRESENT
		transition(cmd, dst,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_ACCESS_TRANSFER_WRITE_BIT, 0,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
	}

}
