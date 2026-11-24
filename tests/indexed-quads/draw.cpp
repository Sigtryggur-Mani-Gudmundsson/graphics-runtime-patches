// Compare Xbox quad replay with its explicit two-triangle equivalent (PR 207).
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
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
    constexpr uint32_t width = 64, height = 64;
    constexpr auto aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    ResourceBinding target{};
    target.format = VK_FORMAT_R8G8B8A8_UNORM;
    target.width = width;
    target.height = height;
    target.simultaneous_access = true;
    target.image = d.image(target.format, width, height, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    check(target.image != VK_NULL_HANDLE, "target allocation");
    Pipeline pipeline(d.device);
    pipeline.create(target.image, vertex, fragment);

    void *shared_data{}, *pixels{};
    constexpr VkDeviceSize allocation = 8192, input_offset = 16, scratch_offset = 256;
    const auto source = d.buffer(allocation, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr);
    const auto scratch = d.buffer(allocation, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr);
    const auto shared = d.buffer(64, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &shared_data);
    const auto readback = d.buffer(width * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &pixels);
    check(source && shared && scratch && readback, "buffer allocation");
    constexpr std::array<uint32_t, 12> triangles{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    std::memcpy(shared_data, triangles.data(), sizeof(triangles));

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
    vk.quad_indices = shared;
    vk.quad_index_count = triangles.size();
    vk.image_view = [&](const auto&, const auto&) { return pipeline.view; };
    vk.graphics_pipeline = [&](uintptr_t) { return &pipeline.graphics; };
    vk.buffer_at = [&](uint64_t addr, uint64_t& bytes, VkBuffer& buffer, VkDeviceSize& offset) {
        if (addr != 0x10000) return false;
        buffer = source;
        offset = input_offset;
        bytes = std::min<uint64_t>(bytes, allocation - input_offset);
        return true;
    };

    vk.copy_scratch = [&](VkDeviceSize size, VkBuffer& buffer, VkDeviceSize& offset) {
        if (size > allocation - scratch_offset) return false;
        buffer = scratch;
        offset = scratch_offset;
        return true;
    };
    void* zero_data{};
    vk.zero_buffer = d.buffer(64, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &zero_data);
    check(vk.zero_buffer != VK_NULL_HANDLE, "zero page");
    std::memset(zero_data, 0, 64);
    vk.zero_size = 64;

    auto views = std::make_shared<DrawViews>();
    views->render_target_count = 1;
    views->render_targets[0] = {1, 0, 0, 1};
    enum class Path { triangles, shared_quads, indexed_quads };
    struct Case {
        const char* name;
        bool overlap;
        uint32_t count, valid, unit, first;
        int32_t base;
        bool bound = true;
    };
    const auto render = [&](Path path, const Case& c) {
        const bool reference = path == Path::triangles;
        std::vector<uint32_t> indices(c.first, 3); // traps ignoring firstIndex
        const auto value = [&](uint32_t i) {
            const auto vertex = c.count > 256 && i < 256 ? 0 : i % 8;
            return uint32_t(int32_t(vertex) - c.base);
        };
        if (reference) {
            for (uint32_t q = 0; q < c.count / 4; ++q)
                for (auto corner : quad_triangle_indices) {
                    const auto i = 4 * q + corner;
                    indices.push_back(c.bound && i < c.valid ? value(i) : 0);
                }
        } else {
            for (uint32_t i = 0; i < c.count; ++i) indices.push_back(value(i));
        }
        const uint32_t size = indices.size() * c.unit;
        std::vector<uint32_t> upload((size + 3) / 4, 0);
        if (c.unit == 4) upload = indices;
        else {
            for (size_t i = 0; i < indices.size(); ++i) upload[i / 2] |= (indices[i] & 0xffffu) << (16 * (i % 2));
        }
        Draw draw{};
        draw.views = views;
        draw.pipeline = 1;
        draw.topology = reference ? 4 : topology_quad_list;
        draw.viewport = {0, 0, width, height, 0, 1};
        draw.scissor = {0, 0, width, height};
        const uint32_t view_bytes = reference ? size : (c.first + c.valid) * c.unit + (c.unit - 1);
        draw.index = {reference || c.bound ? uint64_t{0x10000} : 0, view_bytes, c.unit == 2 ? 57u : 42u};
        draw.indexed = path != Path::shared_quads;
        draw.instances = c.overlap ? 2 : 1;
        draw.count = reference ? c.count / 4 * 6 : c.count;
        draw.first = c.first;
        draw.base_vertex = c.base;
        draw.first_instance = 3;
        draw.user_data[0] = c.overlap;
        CommandList list;
        check(list.draw(draw) && list.close(), "record draw");
        check(d.run([&](VkCommandBuffer cb) {
            // Poison scratch so that missing input can only pass via an explicit zero fill.
            vkCmdFillBuffer(cb, scratch, 0, allocation, 0x00030003);
            if (!upload.empty()) vkCmdUpdateBuffer(cb, source, input_offset, upload.size() * 4, upload.data());
            detail::memory_barrier(vk, cb, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_ACCESS_INDEX_READ_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
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
            d.read_back(cb, target.image, VK_IMAGE_LAYOUT_GENERAL, aspect, width, height, 1, readback, 0);
            return true;
        }), "submit");
        const auto* rgba = static_cast<const uint32_t*>(pixels);
        return std::vector<uint32_t>(rgba, rgba + width * height);
    };
    int failures = 0;
    const Case cases[]{
        {"flat colour", false, 4, 4, 4, 0, 0},
        {"instance order", true, 8, 8, 4, 0, 0},
        {"16-bit offsets and base", false, 4, 4, 2, 3, -9},
        {"16-bit partial view", false, 4, 3, 2, 1, 0},
        {"32-bit partial view", false, 4, 2, 4, 2, 0},
        {"cross copy batch", true, 261, 260, 2, 3, -2},
        {"null view", false, 4, 0, 2, 0, 0, false},
        {"empty final quad", false, 3, 3, 2, 0, 0},
    };
    for (const auto& c : cases) {
        const auto reference = render(Path::triangles, c);
        if (c.first == 0 && c.bound && c.count >= 4) {
            const auto expected = c.overlap ? 0xff00ff00u : 0xff0000ffu;
            check(std::count(reference.begin(), reference.end(), expected) == width * height, "reference colour");
            check(render(Path::shared_quads, c) == reference, "shared quad control");
        }
        const auto frame = render(Path::indexed_quads, c);
        size_t correct = 0;
        for (size_t i = 0; i < frame.size(); ++i) correct += frame[i] == reference[i];
        if (c.count > 256) check(std::count(reference.begin(), reference.end(), 0xff00ff00u) == width * height, "last quad visible");
        std::printf("%s: %zu/%zu match; first pixel %08x, reference %08x\n",
                    c.name, correct, frame.size(), frame[0], reference[0]);
        failures += correct != frame.size();
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
