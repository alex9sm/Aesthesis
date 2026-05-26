#include "vk_pch.hpp"
#include "vk_lights.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_frame.hpp"
#include "vk_globals.hpp"
#include "log.hpp"
#include "memory.hpp"

namespace vk {

	struct PerFrameLights {
		VkBuffer       buffer;
		VmaAllocation  alloc;
		void*          mapped;
	};

	static PerFrameLights frames[FRAMES_IN_FLIGHT] = {};
	static u32 write_cursor = 0;

	// --- creation ---

	static bool create_buffers() {
		VmaAllocator a = allocator();

		VkDeviceSize size = (VkDeviceSize)sizeof(PointLightGPU) * MAX_POINT_LIGHTS;

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
				logger::fatal("Failed to create light SSBO buffer");
				return false;
			}
			frames[i].mapped = ainfo.pMappedData;
		}
		return true;
	}

	static void write_descriptors() {
		Context& c = context();

		VkDeviceSize size = (VkDeviceSize)sizeof(PointLightGPU) * MAX_POINT_LIGHTS;

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
			VkDescriptorBufferInfo bi = {};
			bi.buffer = frames[i].buffer;
			bi.offset = 0;
			bi.range  = size;

			VkWriteDescriptorSet w = {};
			w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet = global_set_for_frame(i);
			w.dstBinding = 7;
			w.descriptorCount = 1;
			w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			w.pBufferInfo = &bi;

			vkUpdateDescriptorSets(c.device, 1, &w, 0, nullptr);
		}
	}

	bool init_lights() {
		if (!create_buffers()) return false;
		write_descriptors();
		write_cursor = 0;
		return true;
	}

	void shutdown_lights() {
		VmaAllocator a = allocator();
		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
			if (frames[i].buffer) vmaDestroyBuffer(a, frames[i].buffer, frames[i].alloc);
		}
		memory::set(frames, 0, sizeof(frames));
		write_cursor = 0;
	}

	// --- per frame ---

	void reset_lights() {
		write_cursor = 0;
	}

	u32 push_light(const PointLightGPU& data) {
		if (write_cursor >= MAX_POINT_LIGHTS) return UINT32_MAX;
		u32 i = current_frame_index();
		PointLightGPU* dst = (PointLightGPU*)frames[i].mapped + write_cursor;
		memory::copy(dst, &data, sizeof(PointLightGPU));
		return write_cursor++;
	}

	u32 light_count() {
		return write_cursor;
	}

}
