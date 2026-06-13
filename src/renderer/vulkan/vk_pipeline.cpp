#include "vk_pch.hpp"
#include "vk_pipeline.hpp"
#include "vk_init.hpp"
#include "vk_shader.hpp"
#include "log.hpp"

namespace vk {

	static void fill_blend_attachment(VkPipelineColorBlendAttachmentState& a, BlendMode mode) {
		a.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
			| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		if (mode == BlendMode::AlphaBlend) {
			a.blendEnable = VK_TRUE;
			a.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			a.colorBlendOp = VK_BLEND_OP_ADD;
			a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			a.alphaBlendOp = VK_BLEND_OP_ADD;
		}
	}

	static VkPipelineLayout create_layout(VkDevice device,
		const VkDescriptorSetLayout* set_layouts, u32 set_layout_count,
		const VkPushConstantRange* push_constant)
	{
		VkPipelineLayoutCreateInfo lci = {};
		lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		lci.setLayoutCount = set_layout_count;
		lci.pSetLayouts = set_layouts;
		if (push_constant) {
			lci.pushConstantRangeCount = 1;
			lci.pPushConstantRanges = push_constant;
		}
		VkPipelineLayout layout = VK_NULL_HANDLE;
		if (vkCreatePipelineLayout(device, &lci, nullptr, &layout) != VK_SUCCESS)
			return VK_NULL_HANDLE;
		return layout;
	}

	bool create_graphics_pipeline(const GraphicsPipelineSpec& spec,
		VkPipeline* out_pipeline, VkPipelineLayout* out_layout)
	{
		Context& c = context();

		VkShaderModule vs = load_shader_module(spec.vs_path);
		if (!vs) {
			logger::fatal("create_graphics_pipeline: failed to load %s", spec.vs_path);
			return false;
		}
		VkShaderModule fs = VK_NULL_HANDLE;
		if (spec.fs_path) {
			fs = load_shader_module(spec.fs_path);
			if (!fs) {
				vkDestroyShaderModule(c.device, vs, nullptr);
				logger::fatal("create_graphics_pipeline: failed to load %s", spec.fs_path);
				return false;
			}
		}

		VkPipelineShaderStageCreateInfo stages[2] = {};
		u32 stage_count = 0;
		stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[stage_count].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[stage_count].module = vs;
		stages[stage_count].pName = "main";
		stage_count++;
		if (fs) {
			stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[stage_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[stage_count].module = fs;
			stages[stage_count].pName = "main";
			stage_count++;
		}

		VkPipelineVertexInputStateCreateInfo vi = {};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		if (spec.vertex_binding) {
			vi.vertexBindingDescriptionCount = 1;
			vi.pVertexBindingDescriptions = spec.vertex_binding;
			vi.vertexAttributeDescriptionCount = spec.vertex_attr_count;
			vi.pVertexAttributeDescriptions = spec.vertex_attrs;
		}

		VkPipelineInputAssemblyStateCreateInfo ia = {};
		ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo vp = {};
		vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rs = {};
		rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = spec.cull;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms = {};
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo ds = {};
		ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable = spec.depth_test;
		ds.depthWriteEnable = spec.depth_write;
		ds.depthCompareOp = spec.depth_compare;

		VkPipelineColorBlendAttachmentState cba[8] = {};
		for (u32 i = 0; i < spec.color_count; i++)
			fill_blend_attachment(cba[i], spec.blend);

		VkPipelineColorBlendStateCreateInfo cb = {};
		cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = spec.color_count;
		cb.pAttachments = spec.color_count ? cba : nullptr;

		VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dyn = {};
		dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dyn.dynamicStateCount = 2;
		dyn.pDynamicStates = dyn_states;

		VkPipelineLayout layout = create_layout(c.device,
			spec.set_layouts, spec.set_layout_count, spec.push_constant);
		if (!layout) {
			if (vs) vkDestroyShaderModule(c.device, vs, nullptr);
			if (fs) vkDestroyShaderModule(c.device, fs, nullptr);
			logger::fatal("create_graphics_pipeline: failed to create pipeline layout");
			return false;
		}

		VkPipelineRenderingCreateInfo rendering_ci = {};
		rendering_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		rendering_ci.colorAttachmentCount = spec.color_count;
		rendering_ci.pColorAttachmentFormats = spec.color_count ? spec.color_formats : nullptr;
		rendering_ci.depthAttachmentFormat = spec.depth_format;

		VkGraphicsPipelineCreateInfo pci = {};
		pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pci.pNext = &rendering_ci;
		pci.stageCount = stage_count;
		pci.pStages = stages;
		pci.pVertexInputState = &vi;
		pci.pInputAssemblyState = &ia;
		pci.pViewportState = &vp;
		pci.pRasterizationState = &rs;
		pci.pMultisampleState = &ms;
		pci.pDepthStencilState = (spec.depth_test || spec.depth_write) ? &ds : nullptr;
		pci.pColorBlendState = &cb;
		pci.pDynamicState = &dyn;
		pci.layout = layout;

		VkResult r = vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &pci, nullptr, out_pipeline);

		if (vs) vkDestroyShaderModule(c.device, vs, nullptr);
		if (fs) vkDestroyShaderModule(c.device, fs, nullptr);

		if (r != VK_SUCCESS) {
			vkDestroyPipelineLayout(c.device, layout, nullptr);
			logger::fatal("create_graphics_pipeline: vkCreateGraphicsPipelines failed");
			return false;
		}

		*out_layout = layout;
		return true;
	}

	bool create_compute_pipeline(const ComputePipelineSpec& spec,
		VkPipeline* out_pipeline, VkPipelineLayout* out_layout)
	{
		Context& c = context();

		VkShaderModule sm = load_shader_module(spec.cs_path);
		if (!sm) {
			logger::fatal("create_compute_pipeline: failed to load %s", spec.cs_path);
			return false;
		}

		VkPipelineLayout layout = create_layout(c.device,
			spec.set_layouts, spec.set_layout_count, spec.push_constant);
		if (!layout) {
			vkDestroyShaderModule(c.device, sm, nullptr);
			logger::fatal("create_compute_pipeline: failed to create pipeline layout");
			return false;
		}

		VkPipelineShaderStageCreateInfo stage = {};
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stage.module = sm;
		stage.pName = "main";

		VkComputePipelineCreateInfo cpi = {};
		cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		cpi.stage = stage;
		cpi.layout = layout;

		VkResult r = vkCreateComputePipelines(c.device, VK_NULL_HANDLE, 1, &cpi, nullptr, out_pipeline);

		vkDestroyShaderModule(c.device, sm, nullptr);

		if (r != VK_SUCCESS) {
			vkDestroyPipelineLayout(c.device, layout, nullptr);
			logger::fatal("create_compute_pipeline: vkCreateComputePipelines failed");
			return false;
		}

		*out_layout = layout;
		return true;
	}

}
