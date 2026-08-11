#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"

namespace vk {

	// Shared one-shot GPU work batch.
	// Owns a single transient command pool, command buffer and fence for the
	// process lifetime. Callers open a batch, record any commands they like into
	// the returned command buffer, and close it; the batch submits and waits
	// exactly once at close.
	struct StagingBlock {
		void*    mapped;
		VkBuffer buffer;
	};

	bool init_upload();
	void shutdown_upload();

	// Opens or joins the batch and returns the command buffer to record into.
	VkCommandBuffer begin_upload();

	// Closes one nesting level. Submits and waits when the outermost level closes.
	void end_upload();

	// Submits and waits without closing the batch, retiring all staging blocks.
	void flush_upload();

	// Reserves `size` bytes of staging memory for the caller to fill directly.
	bool stage_alloc(VkDeviceSize size, StagingBlock* out);

	// Copies `size` bytes into staging and records staging -> dst at the current point in the recording.
	bool stage_to_buffer(const void* src, VkDeviceSize size, VkBuffer dst);

	// copies into an image the caller has already transitioned to TRANSFER_DST.
	bool stage_to_image(const void* src, VkDeviceSize size, VkImage dst,
		const VkBufferImageCopy* regions, u32 region_count);

}
