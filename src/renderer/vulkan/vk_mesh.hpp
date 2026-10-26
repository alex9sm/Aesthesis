#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include "types.hpp"
#include "gltf.hpp"

namespace vk {

	using MeshHandle = u32;
	static constexpr MeshHandle INVALID_MESH = 0;
	static constexpr u32 MAX_MESHES = 256;

	struct MeshGPU {
		VkBuffer position_buffer;   // vec3 position stream (binding 0)
		VmaAllocation position_alloc;
		VkBuffer attrib_buffer;     // normal/tangent/uv stream (binding 1)
		VmaAllocation attrib_alloc;
		VkBuffer index_buffer;
		VmaAllocation index_alloc;
		VkBuffer skin_buffer;       // joints/weights stream for the skinning pass, skinned meshes only
		VmaAllocation skin_alloc;
		u32 vertex_count;
		u32 index_count;
		AABB local_aabb;
	};

	bool init_meshes();
	void shutdown_meshes();

	MeshHandle create_mesh(const renderer::MeshData& data);
	// GPU copy of src's position/attrib/index streams (no skin stream), recorded on the upload batch
	MeshHandle clone_mesh(MeshHandle src);
	void destroy_mesh(MeshHandle handle);

	const MeshGPU* get_mesh(MeshHandle handle);
	// skinned meshes move, so their culling box is refreshed every frame
	void set_mesh_aabb(MeshHandle handle, const AABB& aabb);

}
