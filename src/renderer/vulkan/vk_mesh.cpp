#include "vk_pch.hpp"
#include "vk_mesh.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "log.hpp"
#include "memory.hpp"

namespace vk {

	static MeshGPU meshes[MAX_MESHES] = {};

	bool init_meshes() {
		memory::set(meshes, 0, sizeof(meshes));
		// slot 0 reserved as INVALID_MESH
		return true;
	}

	void shutdown_meshes() {
		for (u32 i = 1; i < MAX_MESHES; i++) {
			if (meshes[i].vertex_buffer) destroy_mesh(i);
		}
	}

	static MeshHandle find_free_slot() {
		for (u32 i = 1; i < MAX_MESHES; i++) {
			if (meshes[i].vertex_buffer == VK_NULL_HANDLE) return i;
		}
		return INVALID_MESH;
	}

	// creates a host-visible staging buffer, copies into it, then uses a transient
	// command buffer to copy into the device-local destination.
	static bool upload_buffer(const void* src, VkDeviceSize size,
		VkBufferUsageFlags usage, VkBuffer* out_buffer, VmaAllocation* out_alloc)
	{
		Context& c = context();
		VmaAllocator a = allocator();

		// staging
		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = {};
		VmaAllocationInfo staging_info = {};

		VkBufferCreateInfo staging_ci = {};
		staging_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		staging_ci.size = size;
		staging_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		staging_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo staging_alloc_ci = {};
		staging_alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;
		staging_alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
			| VMA_ALLOCATION_CREATE_MAPPED_BIT;

		if (vmaCreateBuffer(a, &staging_ci, &staging_alloc_ci,
			&staging, &staging_alloc, &staging_info) != VK_SUCCESS) {
			logger::error("vmaCreateBuffer (staging) failed");
			return false;
		}

		memory::copy(staging_info.pMappedData, src, (usize)size);

		// destination
		VkBufferCreateInfo dst_ci = {};
		dst_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		dst_ci.size = size;
		dst_ci.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		dst_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo dst_alloc_ci = {};
		dst_alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;

		if (vmaCreateBuffer(a, &dst_ci, &dst_alloc_ci,
			out_buffer, out_alloc, nullptr) != VK_SUCCESS) {
			logger::error("vmaCreateBuffer (device) failed");
			vmaDestroyBuffer(a, staging, staging_alloc);
			return false;
		}

		// transient command buffer for the copy
		VkCommandPool pool = VK_NULL_HANDLE;
		VkCommandPoolCreateInfo pool_ci = {};
		pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		pool_ci.queueFamilyIndex = c.graphics_queue_index;
		vkCreateCommandPool(c.device, &pool_ci, nullptr, &pool);

		VkCommandBuffer cmd = VK_NULL_HANDLE;
		VkCommandBufferAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc_info.commandPool = pool;
		alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc_info.commandBufferCount = 1;
		vkAllocateCommandBuffers(c.device, &alloc_info, &cmd);

		VkCommandBufferBeginInfo begin = {};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmd, &begin);

		VkBufferCopy copy = {};
		copy.size = size;
		vkCmdCopyBuffer(cmd, staging, *out_buffer, 1, &copy);

		vkEndCommandBuffer(cmd);

		VkSubmitInfo submit = {};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;

		vkQueueSubmit(c.graphics_queue, 1, &submit, VK_NULL_HANDLE);
		vkQueueWaitIdle(c.graphics_queue);

		vkDestroyCommandPool(c.device, pool, nullptr);
		vmaDestroyBuffer(a, staging, staging_alloc);
		return true;
	}

	MeshHandle create_mesh(const renderer::MeshData& data) {
		MeshHandle slot = find_free_slot();
		if (slot == INVALID_MESH) {
			logger::error("Out of mesh slots");
			return INVALID_MESH;
		}

		MeshGPU& m = meshes[slot];
		VkDeviceSize vb_size = sizeof(renderer::Vertex) * data.vertex_count;
		VkDeviceSize ib_size = sizeof(u32) * data.index_count;

		if (!upload_buffer(data.vertices, vb_size,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &m.vertex_buffer, &m.vertex_alloc)) {
			return INVALID_MESH;
		}

		if (!upload_buffer(data.indices, ib_size,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &m.index_buffer, &m.index_alloc)) {
			vmaDestroyBuffer(allocator(), m.vertex_buffer, m.vertex_alloc);
			m = {};
			return INVALID_MESH;
		}

		m.vertex_count = data.vertex_count;
		m.index_count = data.index_count;
		m.local_aabb = { data.aabb_min, data.aabb_max };
		return slot;
	}

	void destroy_mesh(MeshHandle handle) {
		if (handle == INVALID_MESH || handle >= MAX_MESHES) return;
		MeshGPU& m = meshes[handle];
		VmaAllocator a = allocator();
		if (m.index_buffer)  vmaDestroyBuffer(a, m.index_buffer, m.index_alloc);
		if (m.vertex_buffer) vmaDestroyBuffer(a, m.vertex_buffer, m.vertex_alloc);
		m = {};
	}

	const MeshGPU* get_mesh(MeshHandle handle) {
		if (handle == INVALID_MESH || handle >= MAX_MESHES) return nullptr;
		if (meshes[handle].vertex_buffer == VK_NULL_HANDLE) return nullptr;
		return &meshes[handle];
	}

}
