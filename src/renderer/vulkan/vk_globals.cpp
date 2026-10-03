#include "vk_pch.hpp"
#include "shared.glsl"
#include "vk_globals.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_frame.hpp"
#include "vk_texture.hpp"  // for MAX_TEXTURES (bindless array size)
#include "log.hpp"
#include "memory.hpp"

namespace vk {

	struct PerFrameUBO {
		VkBuffer       buffer;
		VmaAllocation  alloc;
		void*          mapped;
	};

	static VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
	static VkDescriptorPool      pool       = VK_NULL_HANDLE;
	static PerFrameUBO           ubos[FRAMES_IN_FLIGHT] = {};
	static VkDescriptorSet       sets[FRAMES_IN_FLIGHT] = {};

	VkDescriptorSetLayout global_set_layout() { return set_layout; }
	VkDescriptorSet current_global_set() { return sets[current_frame_index()]; }
	VkDescriptorSet global_set_for_frame(u32 frame_index) { return sets[frame_index]; }

	static bool create_layout() {
		Context& c = context();

		VkDescriptorSetLayoutBinding bindings[BIND_COUNT] = {};
		bindings[BIND_GLOBALS].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[BIND_GLOBALS].descriptorCount = 1;
		bindings[BIND_GLOBALS].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		bindings[BIND_INSTANCES].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[BIND_INSTANCES].descriptorCount = 1;
		bindings[BIND_INSTANCES].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		bindings[BIND_MATERIALS].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[BIND_MATERIALS].descriptorCount = 1;
		bindings[BIND_MATERIALS].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		bindings[BIND_TEXTURES].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[BIND_TEXTURES].descriptorCount = MAX_TEXTURES;
		bindings[BIND_TEXTURES].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		for (u32 b = BIND_IRRADIANCE; b <= BIND_BRDF_LUT; b++) {
			bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[b].descriptorCount = 1;
			bindings[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		}

		bindings[BIND_LIGHTS].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[BIND_LIGHTS].descriptorCount = 1;
		bindings[BIND_LIGHTS].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		for (u32 b = BIND_SHADOW; b <= BIND_SKY; b++) {
			bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[b].descriptorCount = 1;
			bindings[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		}

		for (u32 b = 0; b < BIND_COUNT; b++) bindings[b].binding = b;

		// only the bindless texture array is partially bound
		VkDescriptorBindingFlags binding_flags[BIND_COUNT] = {};
		binding_flags[BIND_TEXTURES] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

		VkDescriptorSetLayoutBindingFlagsCreateInfo flags_ci = {};
		flags_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		flags_ci.bindingCount = BIND_COUNT;
		flags_ci.pBindingFlags = binding_flags;

		VkDescriptorSetLayoutCreateInfo ci = {};
		ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		ci.pNext = &flags_ci;
		ci.bindingCount = BIND_COUNT;
		ci.pBindings = bindings;

		if (vkCreateDescriptorSetLayout(c.device, &ci, nullptr, &set_layout) != VK_SUCCESS) {
			logger::fatal("Failed to create global descriptor set layout");
			return false;
		}
		return true;
	}

	static bool create_pool() {
		Context& c = context();

		VkDescriptorPoolSize sizes[3] = {};
		sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		sizes[0].descriptorCount = FRAMES_IN_FLIGHT;
		// bindings 1 (instance SSBO), 2 (material SSBO) and 7 (lights SSBO).
		sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		sizes[1].descriptorCount = 3 * FRAMES_IN_FLIGHT;
		// BIND_TEXTURES + BIND_IRRADIANCE/PREFILTER/BRDF_LUT + BIND_SHADOW/SPOT_SHADOW/SKY
		sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sizes[2].descriptorCount = (MAX_TEXTURES + 6) * FRAMES_IN_FLIGHT;

		VkDescriptorPoolCreateInfo ci = {};
		ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		ci.maxSets = FRAMES_IN_FLIGHT;
		ci.poolSizeCount = 3;
		ci.pPoolSizes = sizes;

		if (vkCreateDescriptorPool(c.device, &ci, nullptr, &pool) != VK_SUCCESS) {
			logger::fatal("Failed to create global descriptor pool");
			return false;
		}
		return true;
	}

	static bool create_buffers_and_sets() {
		Context& c = context();
		VmaAllocator a = allocator();

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
			VkBufferCreateInfo bci = {};
			bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bci.size = sizeof(GlobalUBO);
			bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo aci = {};
			aci.usage = VMA_MEMORY_USAGE_AUTO;
			aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
				| VMA_ALLOCATION_CREATE_MAPPED_BIT;

			VmaAllocationInfo ainfo = {};
			if (vmaCreateBuffer(a, &bci, &aci, &ubos[i].buffer, &ubos[i].alloc, &ainfo) != VK_SUCCESS) {
				logger::fatal("Failed to create global UBO buffer");
				return false;
			}
			ubos[i].mapped = ainfo.pMappedData;
		}

		VkDescriptorSetLayout layouts[FRAMES_IN_FLIGHT];
		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) layouts[i] = set_layout;

		VkDescriptorSetAllocateInfo ai = {};
		ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		ai.descriptorPool = pool;
		ai.descriptorSetCount = FRAMES_IN_FLIGHT;
		ai.pSetLayouts = layouts;

		if (vkAllocateDescriptorSets(c.device, &ai, sets) != VK_SUCCESS) {
			logger::fatal("Failed to allocate global descriptor sets");
			return false;
		}

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
			VkDescriptorBufferInfo bi = {};
			bi.buffer = ubos[i].buffer;
			bi.offset = 0;
			bi.range = sizeof(GlobalUBO);

			VkWriteDescriptorSet w = {};
			w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet = sets[i];
			w.dstBinding = BIND_GLOBALS;
			w.descriptorCount = 1;
			w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			w.pBufferInfo = &bi;

			vkUpdateDescriptorSets(c.device, 1, &w, 0, nullptr);
		}
		return true;
	}

	bool init_globals() {
		if (!create_layout()) return false;
		if (!create_pool()) return false;
		if (!create_buffers_and_sets()) return false;
		return true;
	}

	void shutdown_globals() {
		Context& c = context();
		VmaAllocator a = allocator();

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
			if (ubos[i].buffer) vmaDestroyBuffer(a, ubos[i].buffer, ubos[i].alloc);
		}
		memory::set(ubos, 0, sizeof(ubos));
		memory::set(sets, 0, sizeof(sets));

		if (pool)       vkDestroyDescriptorPool(c.device, pool, nullptr);
		if (set_layout) vkDestroyDescriptorSetLayout(c.device, set_layout, nullptr);
		pool = VK_NULL_HANDLE;
		set_layout = VK_NULL_HANDLE;
	}

	void update_globals(const GlobalUBO& data) {
		u32 i = current_frame_index();
		memory::copy(ubos[i].mapped, &data, sizeof(GlobalUBO));
	}

	void patch_globals_misc(const vec4& misc) {
		u32 i = current_frame_index();
		GlobalUBO* ubo = (GlobalUBO*)ubos[i].mapped;
		ubo->misc = misc;
	}

	void patch_globals_spot_shadows(const mat4* view_proj, u32 count) {
		GlobalUBO* ubo = (GlobalUBO*)ubos[current_frame_index()].mapped;
		memory::copy(ubo->spot_shadow_vp, view_proj, sizeof(mat4) * count);
	}

}
