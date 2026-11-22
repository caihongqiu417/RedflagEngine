#include "rfpch.h"
#include "VulkanDrawData.h"
#include "Redflag/Renderer/Mesh.h"

namespace Redflag {
    using Vulkan::Check;

    void VulkanDraw::Data::CreatePass()
    {
        for (VkFormat format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM})
        {
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(Context.GetPhysicalDevice(), format, &properties);
            if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            {
                DepthFormat = format;
                break;
            }
        }
        if (DepthFormat == VK_FORMAT_UNDEFINED)
            throw std::runtime_error("The Vulkan device has no supported depth attachment format");

        const bool transparent = Mode == Output::Transparency;
        const bool readDepth = transparent || Mode == Output::Emission;
        const uint32_t colorsCount = ColorCount();
        VkAttachmentDescription images[4]{};
        for (auto& image : images)
        {
            image.samples = VK_SAMPLE_COUNT_1_BIT;
            image.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            image.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            image.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            image.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
        images[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        images[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        images[1].format = VK_FORMAT_R32_SINT;
        images[1].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        if (Mode != Output::ColorAndEntity)
        {
            images[0].format = images[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            images[0].finalLayout = images[1].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        }
        if (transparent)
        {
            images[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            images[2].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        }
        auto& z = images[colorsCount];
        z.format = DepthFormat;
        z.loadOp = readDepth ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        z.storeOp = Mode == Output::LightingSurfaces || Mode == Output::Emission ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        z.initialLayout = readDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        z.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference colors[] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                          {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                          {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        VkAttachmentReference depth{colorsCount, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = colorsCount;
        subpass.pColorAttachments = colors;
        subpass.pDepthStencilAttachment = &depth;

        // 下一帧还要在同一张图上绘制。等 GPU 画完上一帧、读完面板颜色和拾取编号，才能清图重画。
        VkSubpassDependency dependencies[2]{};
        const VkPipelineStageFlags drawStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        const VkPipelineStageFlags readStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        const VkAccessFlags writes = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        const VkAccessFlags reads = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        dependencies[0] = {VK_SUBPASS_EXTERNAL, 0, drawStages | readStages, drawStages, writes | reads,
            writes | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, 0};
        dependencies[1] = {0, VK_SUBPASS_EXTERNAL, drawStages, readStages, writes, reads, 0};
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass.attachmentCount = colorsCount + 1;
        pass.pAttachments = images;
        pass.subpassCount = 1;
        pass.pSubpasses = &subpass;
        pass.dependencyCount = 2;
        pass.pDependencies = dependencies;
        Check(vkCreateRenderPass(Device, &pass, nullptr, &Pass), "vkCreateRenderPass(scene)");
    }

    void VulkanDraw::Data::CreatePipeline()
    {
        VkDescriptorSetLayoutBinding texture{};
        texture.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texture.descriptorCount = 1;
        texture.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set.bindingCount = 1;
        set.pBindings = &texture;
        Check(vkCreateDescriptorSetLayout(Device, &set, nullptr, &TextureLayout), "vkCreateDescriptorSetLayout");

        // 普通纹理仍与 ImGui 的片元采样兼容。高度图还要交给顶点着色器移动网格。
        texture.stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
        Check(vkCreateDescriptorSetLayout(Device, &set, nullptr, &VertexTextureLayout), "vkCreateDescriptorSetLayout(height)");

        VkPushConstantRange object{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Vulkan::ObjectData)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        VkDescriptorSetLayout textureSets[] = {TextureLayout, TextureLayout, VertexTextureLayout, TextureLayout};
        layout.setLayoutCount = Mode == Output::LightingSurfaces ? 4u : 3u;
        layout.pSetLayouts = textureSets;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &object;
        Check(vkCreatePipelineLayout(Device, &layout, nullptr, &PipelineLayout), "vkCreatePipelineLayout");

        // SPIR-V 由同目录 Shaders/Compile.ps1 从 GLSL 生成，启动编辑器时无需查找外部文件。
        static const uint32_t vertexCode[] =
#include "Shaders/Mesh.vert.inc"
        ;
        static const uint32_t fragmentCode[] =
#include "Shaders/Mesh.frag.inc"
        ;
        static const uint32_t surfaceVertex[] =
#include "Shaders/Surface.vert.inc"
        ;
        static const uint32_t surfaceCode[] =
#include "Shaders/Surface.frag.inc"
        ;
        static const uint32_t blendFragment[] =
#include "Shaders/Transparency.frag.inc"
        ;
        const bool transparent = Mode == Output::Transparency;
        static const uint32_t emissionFragment[] =
#include "Shaders/Emission.frag.inc"
        ;
        static const uint32_t emissionVertex[] =
#include "Shaders/Emission.vert.inc"
        ;
        static const uint32_t blendVertex[] =
#include "Shaders/Transparency.vert.inc"
        ;
        struct Modules
        {
            VkDevice Device;
            VkShaderModule Handles[2]{};
            ~Modules() { for (auto shader : Handles) vkDestroyShaderModule(Device, shader, nullptr); }
        } modules{Device};
        VkPipelineShaderStageCreateInfo stages[2]{};
        auto loadStage = [&](uint32_t i, const auto& code)
        {
            VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            module.codeSize = sizeof(code);
            module.pCode = code;
            Check(vkCreateShaderModule(Device, &module, nullptr, &modules.Handles[i]), "vkCreateShaderModule");
            stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[i].module = modules.Handles[i];
            stages[i].pName = "main";
        };
        switch (Mode)
        {
        case Output::ColorAndEntity: loadStage(0, vertexCode); loadStage(1, fragmentCode); break;
        case Output::LightingSurfaces: loadStage(0, surfaceVertex); loadStage(1, surfaceCode); break;
        case Output::Transparency: loadStage(0, blendVertex); loadStage(1, blendFragment); break;
        case Output::Emission: loadStage(0, emissionVertex); loadStage(1, emissionFragment); break;
        }

        VkVertexInputBindingDescription bindings[] = {
            {0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX},
            {1, sizeof(glm::mat4), VK_VERTEX_INPUT_RATE_INSTANCE}
        };
        VkVertexInputAttributeDescription attributes[8] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, Position)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, TexCoord)},
            {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, Normal)},
            {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, EmissiveUVOffset)}
        };
        VkPipelineVertexInputStateCreateInfo vertices{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertices.vertexBindingDescriptionCount = 2;
        vertices.pVertexBindingDescriptions = bindings;
        vertices.vertexAttributeDescriptionCount = transparent || Mode == Output::Emission ? 4 : 3;
        for (uint32_t col = 0; col < 4; ++col)
            attributes[vertices.vertexAttributeDescriptionCount++] = {4 + col, 1, VK_FORMAT_R32G32B32A32_SFLOAT, col * 16};
        vertices.pVertexAttributeDescriptions = attributes;
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = VK_TRUE;
        const bool emission = Mode == Output::Emission;
        depth.depthWriteEnable = !transparent && !emission;
        depth.depthCompareOp = emission ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState outputs[3]{};
        for (auto& output : outputs)
        {
            output.blendEnable = transparent || emission;
            output.srcColorBlendFactor = output.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            output.srcAlphaBlendFactor = output.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            output.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            // 两块灯牌完全共面时，主绘制保留先画的那块。它写入 A=1，后画的灯牌就不会覆盖它。
            if (emission) output.srcColorBlendFactor = output.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        }
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = ColorCount();
        blend.pAttachments = outputs;
        VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.stageCount = 2;
        pipeline.pStages = stages;
        pipeline.pVertexInputState = &vertices;
        pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport;
        pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &samples;
        pipeline.pDepthStencilState = &depth;
        pipeline.pColorBlendState = &blend;
        pipeline.pDynamicState = &dynamic;
        pipeline.layout = PipelineLayout;
        pipeline.renderPass = Pass;
        Check(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &Pipeline),
            "vkCreateGraphicsPipelines(scene)");
    }
}
