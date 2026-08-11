#include "vk_pch.hpp"
#include "vk_upload.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "log.hpp"
#include "memory.hpp"

namespace vk {

	// Staging blocks are retired together when the batch flushes. Batching a
	// whole model means every texture and vertex stream is live in host memory
	// at once, so a batch that hits either limit flushes early and keeps the
	// peak bounded. The common case still costs one submit.
	static constexpr u32          MAX_STAGING_BLOCKS = 256;
	static constexpr VkDeviceSize STAGING_BUDGET     = 128ull * 1024 * 1024;

	struct Staging {
		VkBuffer      buffer;
		VmaAllocation alloc;
	};

	static VkCommandPool   upload_pool  = VK_NULL_HANDLE;
	static VkCommandBuffer upload_cmd   = VK_NULL_HANDLE;
	static VkFence         upload_fence = VK_NULL_HANDLE;

	static Staging      staging[MAX_STAGING_BLOCKS] = {};
	static u32          staging_count = 0;
	static VkDeviceSize staging_bytes = 0;

	static u32  depth     = 0;      // begin/end nesting level
	static bool recording = false;  // upload_cmd is between begin/end

	// --- internals ---

	static void begin_recording() {
		VkCommandBufferBeginInfo bi = {};
		bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(upload_cmd, &bi);
		recording = true;
	}

	static void retire_staging() {
		VmaAllocator a = allocator();
		for (u32 i = 0; i < staging_count; i++) {
			vmaDestroyBuffer(a, staging[i].buffer, staging[i].alloc);
		}
		staging_count = 0;
		staging_bytes = 0;
	}

	// --- init/shutdown ---

	bool init_upload() {
		Context& c = context();

		VkCommandPoolCreateInfo pci = {};
		pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		pci.queueFamilyIndex = c.graphics_queue_index;
		if (vkCreateCommandPool(c.device, &pci, nullptr, &upload_pool) != VK_SUCCESS) {
			logger::fatal("Failed to create upload command pool");
			return false;
		}

		VkCommandBufferAllocateInfo ai = {};
		ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		ai.commandPool = upload_pool;
		ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		ai.commandBufferCount = 1;
		if (vkAllocateCommandBuffers(c.device, &ai, &upload_cmd) != VK_SUCCESS) {
			logger::fatal("Failed to allocate upload command buffer");
			return false;
		}

		VkFenceCreateInfo fci = {};
		fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		if (vkCreateFence(c.device, &fci, nullptr, &upload_fence) != VK_SUCCESS) {
			logger::fatal("Failed to create upload fence");
			return false;
		}

		staging_count = 0;
		staging_bytes = 0;
		depth = 0;
		recording = false;
		return true;
	}

	void shutdown_upload() {
		Context& c = context();

		if (depth != 0) {
			logger::error("shutdown_upload with %u open upload batches", depth);
			depth = 0;
		}
		flush_upload();
		retire_staging();

		if (upload_fence) {
			vkDestroyFence(c.device, upload_fence, nullptr);
			upload_fence = VK_NULL_HANDLE;
		}
		if (upload_pool) {
			vkDestroyCommandPool(c.device, upload_pool, nullptr);
			upload_pool = VK_NULL_HANDLE;
		}
		upload_cmd = VK_NULL_HANDLE;
	}

	// --- batch ---

	VkCommandBuffer begin_upload() {
		if (depth == 0) begin_recording();
		depth++;
		return upload_cmd;
	}

	void flush_upload() {
		if (!recording) return;
		Context& c = context();

		// AUTO + HOST_ACCESS_SEQUENTIAL_WRITE prefers coherent memory but is not
		// guaranteed it; flushing is a no-op when it is coherent. Done here rather
		// than per-stage because stage_alloc callers fill `mapped` after it returns.
		VmaAllocator a = allocator();
		for (u32 i = 0; i < staging_count; i++) {
			vmaFlushAllocation(a, staging[i].alloc, 0, VK_WHOLE_SIZE);
		}

		vkEndCommandBuffer(upload_cmd);
		recording = false;

		VkSubmitInfo si = {};
		si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		si.commandBufferCount = 1;
		si.pCommandBuffers = &upload_cmd;

		VkResult r = vkQueueSubmit(c.graphics_queue, 1, &si, upload_fence);
		if (r != VK_SUCCESS) {
			logger::error("upload submit failed: %d", (int)r);
		} else {
			vkWaitForFences(c.device, 1, &upload_fence, VK_TRUE, UINT64_MAX);
		}

		vkResetFences(c.device, 1, &upload_fence);
		// resetting the pool keeps upload_cmd's handle valid, so callers holding
		// it across a stage_* call never see it go stale.
		vkResetCommandPool(c.device, upload_pool, 0);

		// safe now: the wait above guarantees every recorded copy has completed.
		retire_staging();

		// the batch is still open, so keep recording for whatever follows
		if (depth > 0) begin_recording();
	}

	void end_upload() {
		if (depth == 0) {
			logger::error("end_upload without a matching begin_upload");
			return;
		}
		depth--;
		if (depth == 0) flush_upload();
	}

	// --- staging ---

	bool stage_alloc(VkDeviceSize size, StagingBlock* out) {
		if (depth == 0) {
			logger::error("stage_alloc outside of a begin_upload/end_upload batch");
			return false;
		}

		// the budget is soft: a single allocation larger than the whole budget
		// still goes through rather than flushing forever.
		if (staging_count == MAX_STAGING_BLOCKS ||
			(staging_count > 0 && staging_bytes + size > STAGING_BUDGET)) {
			flush_upload();
		}

		VkBufferCreateInfo ci = {};
		ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		ci.size = size;
		ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo aci = {};
		aci.usage = VMA_MEMORY_USAGE_AUTO;
		aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
			| VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VkBuffer          buffer = VK_NULL_HANDLE;
		VmaAllocation     alloc  = {};
		VmaAllocationInfo info   = {};
		if (vmaCreateBuffer(allocator(), &ci, &aci, &buffer, &alloc, &info) != VK_SUCCESS) {
			logger::error("staging allocation failed (%llu bytes)", (unsigned long long)size);
			return false;
		}

		staging[staging_count].buffer = buffer;
		staging[staging_count].alloc  = alloc;
		staging_count++;
		staging_bytes += size;

		out->mapped = info.pMappedData;
		out->buffer = buffer;
		return true;
	}

	bool stage_to_buffer(const void* src, VkDeviceSize size, VkBuffer dst) {
		StagingBlock block = {};
		if (!stage_alloc(size, &block)) return false;
		memory::copy(block.mapped, src, (usize)size);

		VkBufferCopy copy = {};
		copy.size = size;
		vkCmdCopyBuffer(upload_cmd, block.buffer, dst, 1, &copy);
		return true;
	}

	bool stage_to_image(const void* src, VkDeviceSize size, VkImage dst,
		const VkBufferImageCopy* regions, u32 region_count)
	{
		StagingBlock block = {};
		if (!stage_alloc(size, &block)) return false;
		memory::copy(block.mapped, src, (usize)size);

		vkCmdCopyBufferToImage(upload_cmd, block.buffer, dst,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, region_count, regions);
		return true;
	}

}
