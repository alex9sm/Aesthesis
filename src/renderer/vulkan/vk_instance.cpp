#include "vk_pch.hpp"
#include "vk_instance.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_frame.hpp"
#include "vk_globals.hpp"
#include "log.hpp"
#include "memory.hpp"

namespace vk {

	struct PerFrameInstances {
		VkBuffer       buffer;
		VmaAllocation  alloc;
		void*          mapped;
	};

	static PerFrameInstances frames[FRAMES_IN_FLIGHT] = {};
	static u32 write_cursor = 0;

	// --- creation ---

	static bool create_buffers() {
		VmaAllocator a = allocator();

		VkDeviceSize size = (VkDeviceSize)sizeof(InstanceData) * MAX_DRAWS_PER_FRAME;

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
			VkBufferCreateInfo bci = {};
			bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bci.size = size;
			bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo aci = {};
			aci.usage = VMA_MEMORY_USAGE_AUTO;
			aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
				| VMA_ALLOCATION_CREATE_MAPPED_BIT;

			VmaAllocationInfo ainfo = {};
			if (vmaCreateBuffer(a, &bci, &aci, &frames[i].buffer, &frames[i].alloc, &ainfo) != VK_SUCCESS) {
				logger::fatal("Failed to create instance SSBO buffer");
				return false;
			}
			frames[i].mapped = ainfo.pMappedData;
		}
		return true;
	}

	static void write_descriptors() {
		Context& c = context();

		VkDeviceSize size = (VkDeviceSize)sizeof(InstanceData) * MAX_DRAWS_PER_FRAME;

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
			VkDescriptorBufferInfo bi = {};
			bi.buffer = frames[i].buffer;
			bi.offset = 0;
			bi.range  = size;

			VkWriteDescriptorSet w = {};
			w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet = global_set_for_frame(i);
			w.dstBinding = 1;
			w.descriptorCount = 1;
			w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			w.pBufferInfo = &bi;

			vkUpdateDescriptorSets(c.device, 1, &w, 0, nullptr);
		}
	}

	bool init_instances() {
		if (!create_buffers()) return false;
		write_descriptors();
		write_cursor = 0;
		return true;
	}

	void shutdown_instances() {
		VmaAllocator a = allocator();
		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
			if (frames[i].buffer) vmaDestroyBuffer(a, frames[i].buffer, frames[i].alloc);
		}
		memory::set(frames, 0, sizeof(frames));
		write_cursor = 0;
	}

	// --- per frame ---

	void reset_instances() {
		write_cursor = 0;
	}

	u32 push_instance(const InstanceData& data) {
		if (write_cursor >= MAX_DRAWS_PER_FRAME) return UINT32_MAX;
		u32 i = current_frame_index();
		InstanceData* dst = (InstanceData*)frames[i].mapped + write_cursor;
		memory::copy(dst, &data, sizeof(InstanceData));
		return write_cursor++;
	}

	// --- helpers ---

	// build a "normal matrix" from the upper-left 3x3 of model, packed into a mat4
	// for std430 layout simplicity. shader uses mat3(normal_matrix).
	// (column-major: m.col[c][r] = column c, row r)
	mat4 compute_normal_matrix(const mat4& model) {
		f32 a00 = model.col[0][0], a01 = model.col[0][1], a02 = model.col[0][2];
		f32 a10 = model.col[1][0], a11 = model.col[1][1], a12 = model.col[1][2];
		f32 a20 = model.col[2][0], a21 = model.col[2][1], a22 = model.col[2][2];

		f32 c00 =  (a11 * a22 - a12 * a21);
		f32 c01 = -(a10 * a22 - a12 * a20);
		f32 c02 =  (a10 * a21 - a11 * a20);
		f32 c10 = -(a01 * a22 - a02 * a21);
		f32 c11 =  (a00 * a22 - a02 * a20);
		f32 c12 = -(a00 * a21 - a01 * a20);
		f32 c20 =  (a01 * a12 - a02 * a11);
		f32 c21 = -(a00 * a12 - a02 * a10);
		f32 c22 =  (a00 * a11 - a01 * a10);

		f32 det = a00 * c00 + a01 * c01 + a02 * c02;
		f32 inv_det = (det != 0.0f) ? (1.0f / det) : 0.0f;

		// normal_matrix = transpose(inverse(M3)) = cofactor / det (no transpose needed)
		mat4 n = {};
		n.col[0][0] = c00 * inv_det; n.col[0][1] = c01 * inv_det; n.col[0][2] = c02 * inv_det;
		n.col[1][0] = c10 * inv_det; n.col[1][1] = c11 * inv_det; n.col[1][2] = c12 * inv_det;
		n.col[2][0] = c20 * inv_det; n.col[2][1] = c21 * inv_det; n.col[2][2] = c22 * inv_det;
		n.col[3][3] = 1.0f;
		return n;
	}

}
