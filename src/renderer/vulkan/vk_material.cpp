#include "vk_material.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_frame.hpp"
#include "vk_globals.hpp"
#include "vk_texture.hpp"
#include "log.hpp"
#include "memory.hpp"

namespace vk {

	// single immutable SSBO; both frames-in-flight descriptor sets point here.
	static VkBuffer       material_buffer = VK_NULL_HANDLE;
	static VmaAllocation  material_alloc  = {};
	static MaterialGPU*   material_mapped = nullptr;
	// occupancy bitmap; slots are reused on unload.
	static bool           slot_used[MAX_MATERIALS] = {};

	// --- internal ---

	static MaterialHandle find_free_slot() {
		// slot 0 reserved for default material; allocate user materials from 1+.
		for (u32 i = 1; i < MAX_MATERIALS; i++) {
			if (!slot_used[i]) return i;
		}
		return INVALID_MATERIAL;
	}

	static void write_descriptors() {
		Context& c = context();

		VkDescriptorBufferInfo bi = {};
		bi.buffer = material_buffer;
		bi.offset = 0;
		bi.range  = (VkDeviceSize)sizeof(MaterialGPU) * MAX_MATERIALS;

		for (u32 fi = 0; fi < FRAMES_IN_FLIGHT; fi++) {
			VkWriteDescriptorSet w = {};
			w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet = global_set_for_frame(fi);
			w.dstBinding = 2;
			w.descriptorCount = 1;
			w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			w.pBufferInfo = &bi;
			vkUpdateDescriptorSets(c.device, 1, &w, 0, nullptr);
		}
	}

	static bool create_buffer() {
		VmaAllocator a = allocator();

		VkBufferCreateInfo bci = {};
		bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bci.size = (VkDeviceSize)sizeof(MaterialGPU) * MAX_MATERIALS;
		bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo aci = {};
		aci.usage = VMA_MEMORY_USAGE_AUTO;
		aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
			| VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VmaAllocationInfo ainfo = {};
		if (vmaCreateBuffer(a, &bci, &aci, &material_buffer, &material_alloc, &ainfo) != VK_SUCCESS) {
			logger::fatal("Failed to create material SSBO buffer");
			return false;
		}
		material_mapped = (MaterialGPU*)ainfo.pMappedData;
		memory::set(material_mapped, 0, (usize)bci.size);
		return true;
	}

	static void write_slot(MaterialHandle slot, const MaterialDescGPU& desc) {
		MaterialGPU* dst = &material_mapped[slot];
		dst->base_color_factor = desc.base_color_factor;
		dst->mr_factors = { desc.metallic_factor, desc.roughness_factor, 0.0f, 0.0f };
		dst->albedo_idx = desc.albedo_idx;
		dst->normal_idx = desc.normal_idx;
		dst->orm_idx    = desc.orm_idx;
		dst->_pad = 0;
	}

	// --- init/shutdown ---

	bool init_materials() {
		memory::set(slot_used, 0, sizeof(slot_used));
		if (!create_buffer()) return false;
		write_descriptors();

		// slot 0: engine default material — neutral PBR factors, points at reserved
		// texture slots (white albedo, flat normal, ORM neutral).
		MaterialDescGPU def = {};
		def.albedo_idx = TEX_DEFAULT_ALBEDO;
		def.normal_idx = TEX_DEFAULT_NORMAL;
		def.orm_idx    = TEX_DEFAULT_ORM;
		def.base_color_factor = { 0.5f, 0.5f, 0.5f, 1.0f };
		def.metallic_factor   = 0.0f;
		def.roughness_factor  = 1.0f;
		write_slot(DEFAULT_MATERIAL, def);
		slot_used[DEFAULT_MATERIAL] = true;

		return true;
	}

	void shutdown_materials() {
		VmaAllocator a = allocator();
		if (material_buffer) vmaDestroyBuffer(a, material_buffer, material_alloc);
		material_buffer = VK_NULL_HANDLE;
		material_alloc  = {};
		material_mapped = nullptr;
		memory::set(slot_used, 0, sizeof(slot_used));
	}

	// --- public ---

	MaterialHandle create_material(const MaterialDescGPU& desc) {
		MaterialHandle slot = find_free_slot();
		if (slot == INVALID_MATERIAL) {
			logger::error("Out of material slots");
			return INVALID_MATERIAL;
		}
		write_slot(slot, desc);
		slot_used[slot] = true;
		return slot;
	}

	void unload_material(MaterialHandle handle) {
		if (handle == INVALID_MATERIAL || handle >= MAX_MATERIALS) return;
		if (handle == DEFAULT_MATERIAL) {
			logger::error("Attempt to unload default material slot");
			return;
		}
		slot_used[handle] = false;
	}

}
