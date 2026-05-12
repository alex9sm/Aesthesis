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
		VkBuffer vertex_buffer;
		VmaAllocation vertex_alloc;
		VkBuffer index_buffer;
		VmaAllocation index_alloc;
		u32 vertex_count;
		u32 index_count;
	};

	bool init_meshes();
	void shutdown_meshes();

	MeshHandle create_mesh(const renderer::MeshData& data);
	void destroy_mesh(MeshHandle handle);

	const MeshGPU* get_mesh(MeshHandle handle);

}
