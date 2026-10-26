#include "vk_pch.hpp"
#include "vk_skinning.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_frame.hpp"
#include "vk_pipeline.hpp"
#include "memory.hpp"

namespace vk {

	// src positions, src attribs, skin stream, dst positions, dst attribs, joint matrices
	static constexpr u32 BINDING_COUNT = 6;

	struct SkinnedMesh {
		MeshHandle      src;
		MeshHandle      dst;   // INVALID_MESH = free slot
		VkDescriptorSet sets[FRAMES_IN_FLIGHT];
	};

	struct Job {
		u32 slot;
		u32 joint_offset;
	};

	// must match skin.comp
	struct SkinPC {
		u32 vertex_count;
		u32 joint_offset;
	};

	static SkinnedMesh slots[MAX_SKINNED_MESHES] = {};
	static Job         jobs[MAX_SKINNED_MESHES];
	static u32         job_count = 0;

	static VkBuffer      joint_buffers[FRAMES_IN_FLIGHT] = {};
	static VmaAllocation joint_allocs[FRAMES_IN_FLIGHT] = {};
	static mat4*         joint_mapped[FRAMES_IN_FLIGHT] = {};
	static u32           joint_cursor = 0;

	static VkDescriptorSetLayout set_layout      = VK_NULL_HANDLE;
	static VkDescriptorPool      pool            = VK_NULL_HANDLE;
	static VkPipelineLayout      pipeline_layout = VK_NULL_HANDLE;
	static VkPipeline            pipeline        = VK_NULL_HANDLE;

	bool init_skinning() {
		Context& c = context();

		VkDescriptorSetLayoutBinding bindings[BINDING_COUNT] = {};
		for (u32 i = 0; i < BINDING_COUNT; i++) {
			bindings[i].binding = i;
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		}
		VkDescriptorSetLayoutCreateInfo lci = {};
		lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		lci.bindingCount = BINDING_COUNT;
		lci.pBindings = bindings;
		if (vkCreateDescriptorSetLayout(c.device, &lci, nullptr, &set_layout) != VK_SUCCESS) {
			logger::fatal("Failed to create skinning descriptor set layout");
			return false;
		}

		VkDescriptorPoolSize size = {};
		size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		size.descriptorCount = MAX_SKINNED_MESHES * FRAMES_IN_FLIGHT * BINDING_COUNT;
		VkDescriptorPoolCreateInfo pci = {};
		pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pci.maxSets = MAX_SKINNED_MESHES * FRAMES_IN_FLIGHT;
		pci.poolSizeCount = 1;
		pci.pPoolSizes = &size;
		if (vkCreateDescriptorPool(c.device, &pci, nullptr, &pool) != VK_SUCCESS) {
			logger::fatal("Failed to create skinning descriptor pool");
			return false;
		}

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
			VkBufferCreateInfo bci = {};
			bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bci.size = sizeof(mat4) * MAX_SKIN_JOINTS_PER_FRAME;
			bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo aci = {};
			aci.usage = VMA_MEMORY_USAGE_AUTO;
			aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

			VmaAllocationInfo info = {};
			if (vmaCreateBuffer(allocator(), &bci, &aci, &joint_buffers[i], &joint_allocs[i], &info) != VK_SUCCESS) {
				logger::fatal("Failed to create joint matrix buffer");
				return false;
			}
			joint_mapped[i] = (mat4*)info.pMappedData;
		}

		VkPushConstantRange pc = {};
		pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pc.size = sizeof(SkinPC);

		ComputePipelineSpec spec = {};
		spec.cs_path = "shaders/spv/skin.comp.spv";
		spec.set_layouts = &set_layout;
		spec.set_layout_count = 1;
		spec.push_constant = &pc;
		return create_compute_pipeline(spec, &pipeline, &pipeline_layout);
	}

	void shutdown_skinning() {
		Context& c = context();
		for (u32 i = 0; i < MAX_SKINNED_MESHES; i++) {
			if (slots[i].dst != INVALID_MESH) destroy_mesh(slots[i].dst);
		}
		memory::set(slots, 0, sizeof(slots));
		job_count = 0;

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
			if (joint_buffers[i]) vmaDestroyBuffer(allocator(), joint_buffers[i], joint_allocs[i]);
			joint_buffers[i] = VK_NULL_HANDLE;
			joint_mapped[i] = nullptr;
		}
		if (pipeline)        vkDestroyPipeline(c.device, pipeline, nullptr);
		if (pipeline_layout) vkDestroyPipelineLayout(c.device, pipeline_layout, nullptr);
		if (pool)            vkDestroyDescriptorPool(c.device, pool, nullptr);
		if (set_layout)      vkDestroyDescriptorSetLayout(c.device, set_layout, nullptr);
		pipeline = VK_NULL_HANDLE;
		pipeline_layout = VK_NULL_HANDLE;
		pool = VK_NULL_HANDLE;
		set_layout = VK_NULL_HANDLE;
	}

	MeshHandle create_skinned_mesh(MeshHandle src) {
		Context& c = context();
		const MeshGPU* s = get_mesh(src);
		if (!s || !s->skin_buffer) {
			logger::error("create_skinned_mesh: mesh %u has no skin stream", src);
			return INVALID_MESH;
		}
		u32 slot = 0;
		while (slot < MAX_SKINNED_MESHES && slots[slot].dst != INVALID_MESH) slot++;
		if (slot == MAX_SKINNED_MESHES) {
			logger::error("Out of skinned mesh slots (%u)", MAX_SKINNED_MESHES);
			return INVALID_MESH;
		}

		MeshHandle dst = clone_mesh(src);
		if (dst == INVALID_MESH) return INVALID_MESH;
		const MeshGPU* d = get_mesh(dst);

		SkinnedMesh& sm = slots[slot];
		VkDescriptorSetLayout layouts[FRAMES_IN_FLIGHT];
		for (u32 f = 0; f < FRAMES_IN_FLIGHT; f++) layouts[f] = set_layout;
		VkDescriptorSetAllocateInfo ai = {};
		ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		ai.descriptorPool = pool;
		ai.descriptorSetCount = FRAMES_IN_FLIGHT;
		ai.pSetLayouts = layouts;
		if (vkAllocateDescriptorSets(c.device, &ai, sm.sets) != VK_SUCCESS) {
			logger::error("Failed to allocate skinning descriptor sets");
			destroy_mesh(dst);
			return INVALID_MESH;
		}

		for (u32 f = 0; f < FRAMES_IN_FLIGHT; f++) {
			VkBuffer buffers[BINDING_COUNT] = {
				s->position_buffer, s->attrib_buffer, s->skin_buffer,
				d->position_buffer, d->attrib_buffer, joint_buffers[f],
			};
			VkDescriptorBufferInfo infos[BINDING_COUNT] = {};
			VkWriteDescriptorSet writes[BINDING_COUNT] = {};
			for (u32 b = 0; b < BINDING_COUNT; b++) {
				infos[b] = { buffers[b], 0, VK_WHOLE_SIZE };
				writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[b].dstSet = sm.sets[f];
				writes[b].dstBinding = b;
				writes[b].descriptorCount = 1;
				writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				writes[b].pBufferInfo = &infos[b];
			}
			vkUpdateDescriptorSets(c.device, BINDING_COUNT, writes, 0, nullptr);
		}

		sm.src = src;
		sm.dst = dst;
		return dst;
	}

	void destroy_skinned_mesh(MeshHandle mesh) {
		if (mesh == INVALID_MESH) return;
		for (u32 i = 0; i < MAX_SKINNED_MESHES; i++) {
			if (slots[i].dst != mesh) continue;
			vkFreeDescriptorSets(context().device, pool, FRAMES_IN_FLIGHT, slots[i].sets);
			destroy_mesh(mesh);
			slots[i] = {};
			return;
		}
	}

	u32 push_joints(const mat4* joints, u32 count) {
		if (joint_cursor + count > MAX_SKIN_JOINTS_PER_FRAME) {
			static bool warned = false;
			if (!warned) logger::error("Joint buffer full (%u); skinned meshes hold their last pose", MAX_SKIN_JOINTS_PER_FRAME);
			warned = true;
			return UINT32_MAX;
		}
		memory::copy(joint_mapped[current_frame_index()] + joint_cursor, joints, sizeof(mat4) * count);
		u32 offset = joint_cursor;
		joint_cursor += count;
		return offset;
	}

	void queue_skinning(MeshHandle mesh, u32 joint_offset) {
		if (job_count >= MAX_SKINNED_MESHES) return;
		for (u32 i = 0; i < MAX_SKINNED_MESHES; i++) {
			if (slots[i].dst == mesh) {
				jobs[job_count++] = { i, joint_offset };
				return;
			}
		}
	}

	void execute_skinning(VkCommandBuffer cmd) {
		if (job_count > 0) {
			// the previous frame's draws may still read the outputs: write-after-read needs ordering only
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 0, nullptr);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			u32 frame = current_frame_index();
			for (u32 j = 0; j < job_count; j++) {
				const SkinnedMesh& sm = slots[jobs[j].slot];
				const MeshGPU* m = get_mesh(sm.dst);
				if (!m) continue;
				SkinPC pc = { m->vertex_count, jobs[j].joint_offset };
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout,
					0, 1, &sm.sets[frame], 0, nullptr);
				vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
				vkCmdDispatch(cmd, (m->vertex_count + 63) / 64, 1, 1);
			}

			VkMemoryBarrier mb = {};
			mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
				0, 1, &mb, 0, nullptr, 0, nullptr);
		}
		job_count = 0;
		joint_cursor = 0;
	}

}
