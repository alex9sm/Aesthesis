#include "vk_shader.hpp"
#include "vk_init.hpp"
#include "file.hpp"
#include "memory.hpp"
#include "log.hpp"

namespace vk {

	VkShaderModule load_shader_module(const char* spv_path) {
		u64 size = 0;
		if (!file::get_size(spv_path, &size)) {
			logger::error("Shader file not found: %s", spv_path);
			return VK_NULL_HANDLE;
		}
		if (size == 0 || (size % 4) != 0) {
			logger::error("Shader file invalid size: %s (%llu bytes)", spv_path, size);
			return VK_NULL_HANDLE;
		}

		void* buffer = memory::malloc((u32)size);
		u64 read = file::read_file(spv_path, buffer, size);
		if (read != size) {
			memory::free(buffer);
			logger::error("Shader file read failed: %s", spv_path);
			return VK_NULL_HANDLE;
		}

		VkShaderModuleCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		info.codeSize = (size_t)size;
		info.pCode = (const u32*)buffer;

		VkShaderModule module = VK_NULL_HANDLE;
		VkResult result = vkCreateShaderModule(context().device, &info, nullptr, &module);
		memory::free(buffer);

		if (result != VK_SUCCESS) {
			logger::error("vkCreateShaderModule failed: %s", spv_path);
			return VK_NULL_HANDLE;
		}
		return module;
	}

	void destroy_shader_module(VkShaderModule module) {
		if (module) vkDestroyShaderModule(context().device, module, nullptr);
	}

}
