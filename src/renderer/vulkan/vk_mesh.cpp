#include "vk_pch.hpp"
#include "vk_mesh.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_upload.hpp"
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
			if (meshes[i].position_buffer) destroy_mesh(i);
		}
	}

	static MeshHandle find_free_slot() {
		for (u32 i = 1; i < MAX_MESHES; i++) {
			if (meshes[i].position_buffer == VK_NULL_HANDLE) return i;
		}
		return INVALID_MESH;
	}

	// creates the device-local destination and records a staged copy into it on
	// the open upload batch. the copy completes when that batch is submitted.
	static bool create_and_stage(const void* src, VkDeviceSize size,
		VkBufferUsageFlags usage, VkBuffer* out_buffer, VmaAllocation* out_alloc)
	{
		VmaAllocator a = allocator();

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
			return false;
		}

		if (!stage_to_buffer(src, size, *out_buffer)) {
			vmaDestroyBuffer(a, *out_buffer, *out_alloc);
			*out_buffer = VK_NULL_HANDLE;
			*out_alloc = {};
			return false;
		}
		return true;
	}

	MeshHandle create_mesh(const renderer::MeshData& data) {
		MeshHandle slot = find_free_slot();
		if (slot == INVALID_MESH) {
			logger::error("Out of mesh slots");
			return INVALID_MESH;
		}

		MeshGPU& m = meshes[slot];
		VkDeviceSize pos_size = sizeof(vec3) * data.vertex_count;
		VkDeviceSize attr_size = sizeof(renderer::VertexAttribs) * data.vertex_count;
		VkDeviceSize ib_size = sizeof(u32) * data.index_count;

		begin_upload();
		bool ok = create_and_stage(data.positions, pos_size,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &m.position_buffer, &m.position_alloc)
			&& create_and_stage(data.attribs, attr_size,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &m.attrib_buffer, &m.attrib_alloc)
			&& create_and_stage(data.indices, ib_size,
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &m.index_buffer, &m.index_alloc);
		end_upload();

		if (!ok) {
			flush_upload();
			destroy_mesh(slot);
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
		if (m.index_buffer)    vmaDestroyBuffer(a, m.index_buffer, m.index_alloc);
		if (m.attrib_buffer)   vmaDestroyBuffer(a, m.attrib_buffer, m.attrib_alloc);
		if (m.position_buffer) vmaDestroyBuffer(a, m.position_buffer, m.position_alloc);
		m = {};
	}

	const MeshGPU* get_mesh(MeshHandle handle) {
		if (handle == INVALID_MESH || handle >= MAX_MESHES) return nullptr;
		if (meshes[handle].position_buffer == VK_NULL_HANDLE) return nullptr;
		return &meshes[handle];
	}

}
