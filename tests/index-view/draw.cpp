// Each fetched index lights one distinct pixel. Run against either revision of vulkan_replay.h.
#include <vulkan/vulkan.h>
#include "vk_test_device.h"
#include "vulkan_replay.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using namespace leonida;

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

struct Pipeline {
    VkDevice device;
    GraphicsPipeline graphics;
    VkImageView view{};
    std::array<VkShaderModule, 2> modules{};

    explicit Pipeline(VkDevice device) : device(device) {}
    ~Pipeline() {
        vkDestroyPipeline(device, graphics.pipeline, nullptr);
        vkDestroyPipelineLayout(device, graphics.layout, nullptr);
        for (auto module : modules) vkDestroyShaderModule(device, module, nullptr);
        vkDestroyImageView(device, view, nullptr);
    }

    void create(VkImage image, const char* vertex, const char* fragment) {
        constexpr VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(device, &vi, nullptr, &view) == VK_SUCCESS, "image view");

        const std::array<const char*, 2> files{vertex, fragment};
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        for (size_t i = 0; i < files.size(); ++i) {
            std::ifstream stream(files[i], std::ios::binary | std::ios::ate);
            const std::streamoff size = stream.tellg();
            check(size > 0 && size % 4 == 0, "SPIR-V file size");
            std::vector<uint32_t> words(size / 4);
            check(bool(stream.seekg(0).read(reinterpret_cast<char*>(words.data()), size)), "read SPIR-V");
            VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            shader.codeSize = size;
            shader.pCode = words.data();
            check(vkCreateShaderModule(device, &shader, nullptr, &modules[i]) == VK_SUCCESS, "shader module");
            stages[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            stages[i].module = modules[i];
            stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[i].pName = "main";
        }
        graphics.push_stages = VK_SHADER_STAGE_VERTEX_BIT;
        VkPushConstantRange push{graphics.push_stages, 0, sizeof(ShaderUserData)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device, &layout, nullptr, &graphics.layout) == VK_SUCCESS, "pipeline layout");

        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState mask{};
        mask.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &mask;
        const VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_BLEND_CONSTANTS};
        VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = std::size(states);
        dyn.pDynamicStates = states;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &format;
        VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.pNext = &rendering;
        pipeline.layout = graphics.layout;
        pipeline.stageCount = stages.size();
        pipeline.pStages = stages.data();
        pipeline.pVertexInputState = &input;
        pipeline.pInputAssemblyState = &ia;
        pipeline.pViewportState = &vp;
        pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &ms;
        pipeline.pColorBlendState = &blend;
        pipeline.pDynamicState = &dyn;
        check(vkCreateGraphicsPipelines(device, {}, 1, &pipeline, nullptr, &graphics.pipeline) == VK_SUCCESS, "graphics pipeline");
        graphics.color_formats = {format};
    }
};

int run(vktest::Device& d, const char* vertex, const char* fragment) {
    constexpr uint32_t width = 8;
    constexpr auto aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    ResourceBinding target{};
    target.format = VK_FORMAT_R8G8B8A8_UNORM;
    target.width = width;
    target.simultaneous_access = true;
    target.image = d.image(target.format, width, 1, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    check(target.image != VK_NULL_HANDLE, "target allocation");
    Pipeline pipeline(d.device);
    pipeline.create(target.image, vertex, fragment);

    constexpr auto flags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    void *indices{}, *zeros{}, *pixels{};
    const auto source = d.buffer(256, flags, &indices);
    const auto zero = d.buffer(256, flags, &zeros);
    const auto scratch = d.buffer(256, flags, nullptr);
    const auto readback = d.buffer(width * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &pixels);
    check(source && zero && scratch && readback, "buffer allocation");
    // The declared zero page is eight bytes. Its neighbours contain valid, visible indices.
    std::memset(zeros, 0, 256);
    auto* tail = static_cast<uint16_t*>(zeros);
    std::fill(tail + 4, tail + 128, uint16_t{7});

    ReplayFunctions vk{};
    vk.pipeline_barrier = vkCmdPipelineBarrier;
    vk.fill_buffer = vkCmdFillBuffer;
    vk.copy_buffer = vkCmdCopyBuffer;
    vk.begin_rendering = vkCmdBeginRendering;
    vk.end_rendering = vkCmdEndRendering;
    vk.bind_pipeline = vkCmdBindPipeline;
    vk.push_constants = vkCmdPushConstants;
    vk.bind_vertex_buffers2 = vkCmdBindVertexBuffers2;
    vk.bind_index_buffer = vkCmdBindIndexBuffer;
    vk.set_viewport = vkCmdSetViewport;
    vk.set_scissor = vkCmdSetScissor;
    vk.set_primitive_topology = vkCmdSetPrimitiveTopology;
    vk.set_primitive_restart_enable = vkCmdSetPrimitiveRestartEnable;
    vk.set_stencil_reference = vkCmdSetStencilReference;
    vk.set_blend_constants = vkCmdSetBlendConstants;
    vk.draw = vkCmdDraw;
    vk.draw_indexed = vkCmdDrawIndexed;
    vk.zero_buffer = zero;
    vk.zero_size = 8;
    vk.image_view = [&](const auto&, const auto&) { return pipeline.view; };
    vk.graphics_pipeline = [&](uintptr_t) { return &pipeline.graphics; };
    vk.buffer_at = [&](uint64_t addr, uint64_t& bytes, VkBuffer& buffer, VkDeviceSize& offset) {
        if (addr != 0x10000) return false;
        buffer = source;
        offset = 8;
        bytes = std::min<uint64_t>(bytes, 32);
        return true;
    };
    vk.copy_scratch = [&](VkDeviceSize size, VkBuffer& buffer, VkDeviceSize& offset) {
        if (size > 248) return false;
        buffer = scratch;
        offset = 8;
        return true;
    };

    auto views = std::make_shared<DrawViews>();
    views->render_target_count = 1;
    views->render_targets[0] = {1, 0, 0, 1};
    struct Case { const char* name; uint32_t format, size, first, count; bool bound; uint8_t expected; };
    const Case cases[]{
        {"inside 16-bit", 57, 8, 1, 2, true, 0x0c},
        {"inside 32-bit", 42, 20, 2, 3, true, 0x38},
        {"outside", 57, 4, 2, 2, true, 0x01},
        {"odd 16-bit prefix", 57, 7, 2, 3, true, 0x09},
        {"32-bit prefix", 42, 12, 1, 4, true, 0x0d},
        {"clipped to resource end", 42, 64, 7, 2, true, 0x03},
        {"outside, exceeds zero page", 57, 4, 3, 6, true, 0x01},
        {"empty view", 42, 0, 0, 2, true, 0x01},
        {"null, nonzero first", 57, 0, 4, 3, false, 0x01},
        {"null, exceeds zero page", 57, 0, 0, 9, false, 0x01},
    };
    int failures = 0;
    for (const auto& c : cases) {
        std::memset(indices, 0, 256);
        for (unsigned i = 0; i < 16; ++i) {
            auto* address = static_cast<uint8_t*>(indices) + 8;
            if (c.format == 57) reinterpret_cast<uint16_t*>(address)[i] = (i % 7) + 1;
            else reinterpret_cast<uint32_t*>(address)[i] = (i % 7) + 1;
        }
        Draw draw{};
        draw.views = views;
        draw.pipeline = 1;
        draw.topology = 1;
        draw.viewport = {0, 0, width, 1, 0, 1};
        draw.scissor = {0, 0, width, 1};
        draw.index = {c.bound ? uint64_t{0x10000} : 0, c.size, c.format};
        draw.indexed = true;
        draw.count = c.count;
        draw.first = c.first;
        draw.instances = 1;
        CommandList list;
        check(list.draw(draw) && list.close(), "record draw");
        check(d.run([&](VkCommandBuffer cb) {
            d.initial_layout(cb, target.image, aspect, 1, VK_IMAGE_LAYOUT_GENERAL);
            VkClearColorValue clear{};
            VkImageSubresourceRange range{aspect, 0, 1, 0, 1};
            vkCmdClearColorImage(cb, target.image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
            detail::memory_barrier(vk, cb, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            const auto resolve = [&](ResourceId id) { return id == 1 ? &target : nullptr; };
            check(replay(list, cb, resolve, vk) == ReplayResult::success, "indexed draw replay");
            detail::memory_barrier(vk, cb, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            d.read_back(cb, target.image, VK_IMAGE_LAYOUT_GENERAL, aspect, width, 1, 1, readback, 0);
            return true;
        }), "submit");
        uint8_t lit = 0;
        for (unsigned i = 0; i < width; ++i) {
            const auto pixel = static_cast<const uint32_t*>(pixels)[i];
            check(pixel == 0 || pixel == UINT32_MAX, "unexpected pixel");
            if (pixel) lit |= 1u << i;
        }
        std::printf("%-24s observed %02x  expected %02x\n", c.name, unsigned(lit), unsigned(c.expected));
        failures += lit != c.expected;
    }
    return failures ? 1 : 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: draw vertex.spv fragment.spv\n"); return 2; }
    try {
        vktest::Device d;
        const auto status = d.initialize();
        if (status == vktest::DeviceStatus::unavailable) return 77;
        check(status == vktest::DeviceStatus::ready, "Vulkan initialization");
        const int result = run(d, argv[1], argv[2]);
        return d.validation_errors() ? 1 : result;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
