// Standalone Linux integration probe. All geometry, pointers and shaders are synthetic.
#include "khr_test_device.h"
#include "vulkan_backend.h"
#include <array>
#include <fstream>
#include <stdexcept>

using namespace leonida;
#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        if (!(expr))                                                                                         \
            throw std::runtime_error(#expr);                                                                 \
    } while (0)
#define FN(name) d.fn<PFN_##name>(#name)

constexpr auto input = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
struct Structure {
    vktest::Device &d;
    VkAccelerationStructureKHR as{};
    VkDeviceAddress address = 0, scratch = 0;
    Structure(vktest::Device &d, VkAccelerationStructureTypeKHR type,
              VkAccelerationStructureGeometryKHR geometry, uint32_t primitives, VkDeviceSize alignment)
        : d(d) {
        VkAccelerationStructureBuildGeometryInfoKHR build{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        build.type = type;
        build.geometryCount = 1;
        build.pGeometries = &geometry;
        VkAccelerationStructureBuildSizesInfoKHR sizes{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        FN(vkGetAccelerationStructureBuildSizesKHR)(d.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                    &build, &primitives, &sizes);
        VkAccelerationStructureCreateInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        info.type = type;
        info.size = sizes.accelerationStructureSize;
        info.buffer = d.buffer(info.size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, nullptr);
        CHECK(info.buffer);
        auto sb = d.buffer(sizes.buildScratchSize + alignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
        CHECK(sb);
        auto base = d.device_address(sb);
        CHECK(base);
        scratch = (base + alignment - 1) & ~(alignment - 1);
        CHECK(FN(vkCreateAccelerationStructureKHR)(d.device, &info, nullptr, &as) == VK_SUCCESS);
        VkAccelerationStructureDeviceAddressInfoKHR ai{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        ai.accelerationStructure = as;
        address = FN(vkGetAccelerationStructureDeviceAddressKHR)(d.device, &ai);
        CHECK(address);
    }
    ~Structure() { FN(vkDestroyAccelerationStructureKHR)(d.device, as, nullptr); }
};

void barrier(vktest::Device &d, VkCommandBuffer cb, VkPipelineStageFlags src, VkAccessFlags from,
             VkPipelineStageFlags dst, VkAccessFlags to) {
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    b.srcAccessMask = from;
    b.dstAccessMask = to;
    FN(vkCmdPipelineBarrier)(cb, src, dst, 0, 1, &b, 0, nullptr, 0, nullptr);
}

// Imports ordinary aligned host memory through VK_EXT_external_memory_host.
struct HostBuffer {
    vktest::Device &d;
    void *host{};
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    ~HostBuffer() {
        if (buffer)
            FN(vkDestroyBuffer)(d.device, buffer, nullptr);
        if (memory)
            FN(vkFreeMemory)(d.device, memory, nullptr);
        std::free(host);
    }
    bool create(const VkPhysicalDeviceMemoryProperties &mem) {
        if (!d.host_pointer_alignment)
            return false;
        constexpr auto handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
        VkExternalMemoryBufferCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
        external.handleTypes = handle_type;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, &external};
        info.size = 128;
        info.usage = input | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        CHECK(FN(vkCreateBuffer)(d.device, &info, nullptr, &buffer) == VK_SUCCESS);
        VkMemoryRequirements req{};
        FN(vkGetBufferMemoryRequirements)(d.device, buffer, &req);
        const auto alignment = std::max(req.alignment, d.host_pointer_alignment);
        const auto bytes = (req.size + alignment - 1) & ~(alignment - 1);
        host = std::aligned_alloc(alignment, bytes);
        CHECK(host);
        VkMemoryHostPointerPropertiesEXT props{VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
        CHECK(FN(vkGetMemoryHostPointerPropertiesEXT)(d.device, handle_type, host, &props) == VK_SUCCESS);
        VkImportMemoryHostPointerInfoEXT import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT};
        import.handleType = handle_type;
        import.pHostPointer = host;
        VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, &import};
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags};
        alloc.allocationSize = bytes;
        constexpr auto coherent = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
            if (!(req.memoryTypeBits & props.memoryTypeBits & (1u << i)) ||
                (mem.memoryTypes[i].propertyFlags & coherent) != coherent)
                continue;
            alloc.memoryTypeIndex = i;
            if (FN(vkAllocateMemory)(d.device, &alloc, nullptr, &memory) == VK_SUCCESS) {
                CHECK(FN(vkBindBufferMemory)(d.device, buffer, memory, 0) == VK_SUCCESS);
                return true;
            }
        }
        return false;
    }
};

// Revisions predating the direct-input fix lack this binding field. Keeping the fixture identical
// lets the baseline exercise the same valid memory, triangle and rays.
template <class Binding> void set_import_address(Binding &binding, VkDeviceAddress address) {
    if constexpr (requires { binding.imported_address; })
        binding.imported_address = address;
}

bool check_xdxr_vertices(vktest::Device &d, const ResourceBinding &binding, VkDeviceAddress heap,
                         BuildAccelerationStructure build) {
    VulkanContext context(d.device_proc, d.device, true, false, false, true);
    CHECK(context.xdxr_acceleration_structures());
    context.set_heap_base(heap);
    context.add_resource(10, binding);
    VulkanQueue queue(context, d.queue, d.family);
    CHECK(queue.ok());
    XdxrLayout layout;
    CHECK(layout.compute(1, false));
    void *mapped = nullptr;
    auto dst = d.buffer(layout.size + 255, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mapped);
    auto scratch = d.buffer(layout.scratch + 255, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    CHECK(dst && scratch);
    const auto base = d.device_address(dst);
    const auto aligned = (base + 255) & ~uint64_t(255);
    build.dst_va = aligned - heap;
    build.scratch_va = ((d.device_address(scratch) + 255) & ~uint64_t(255)) - heap;
    CommandList list;
    CHECK(list.build_acceleration_structure(build) && list.close());
    const CommandList *lists[]{&list};
    CHECK(queue.execute(lists) == ReplayResult::success && queue.idle());
    CHECK(d.run([&](auto cb) {
        barrier(d, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
        return true;
    }));
    std::array<float, 6> bounds{};
    std::memcpy(bounds.data(), static_cast<char *>(mapped) + (aligned - base) + 16, sizeof(bounds));
    const std::array<float, 6> expected{0, 0, 0, 1, 1, 0};
    std::printf("XDXR imported vertices: bounds=(%g,%g,%g)-(%g,%g,%g)\n", bounds[0], bounds[1], bounds[2],
                bounds[3], bounds[4], bounds[5]);
    return bounds == expected;
}

int run(vktest::Device &d, const char *shader) {
    VkPhysicalDeviceAccelerationStructurePropertiesKHR caps{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &caps};
    d.instance_fn<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2")(d.physical, &props);
    std::printf("Device: %s\n", props.properties.deviceName);
    auto alignment = std::max(256u, caps.minAccelerationStructureScratchOffsetAlignment);
    VkAccelerationStructureGeometryKHR geom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto &tri = geom.geometry.triangles;
    tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    tri.vertexStride = 12;
    tri.maxVertex = 2;
    tri.indexType = VK_INDEX_TYPE_NONE_KHR;
    Structure blas(d, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geom, 1, alignment);
    geom = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geom.geometry.instances.arrayOfPointers = VK_TRUE;
    Structure tlas(d, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, geom, 2, alignment);

    VulkanContext ctx(d.device_proc, d.device, true, false, true);
    VkPhysicalDeviceMemoryProperties mem{};
    d.instance_fn<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(d.physical,
                                                                                                  &mem);
    ctx.set_occlusion_resolve_memory(mem);
    ctx.register_acceleration_structure(0x10000, blas.as);
    ctx.register_acceleration_structure(0x20000, tlas.as);
    VulkanQueue queue(ctx, d.queue, d.family);
    CHECK(queue.ok());
    auto submit = [&](BuildAccelerationStructure b) {
        CommandList list;
        CHECK(list.build_acceleration_structure(b) && list.close());
        const CommandList *lists[] = {&list};
        auto result = queue.execute(lists);
        CHECK(queue.idle());
        return result;
    };
    std::array<float, 9> coords{0, 0, 0, 1, 0, 0, 0, 1, 0};
    void *verts = nullptr;
    auto vertex = d.buffer(sizeof(coords), input, &verts);
    CHECK(vertex);
    std::memcpy(verts, coords.data(), sizeof(coords));
    BuildAccelerationStructure b{};
    b.dst_va = 0x10000;
    b.scratch_va = blas.scratch;
    auto &faces = b.triangles.emplace_back();
    faces.vertex_va = d.device_address(vertex);
    faces.vertex_count = 3;
    faces.vertex_format = VK_FORMAT_R32G32B32_SFLOAT;
    faces.vertex_stride = 12;
    CHECK(submit(b) == ReplayResult::success);
    CHECK(d.run([&](auto cb) {
        barrier(d, cb, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
        return true;
    }));

    // Both candidate translations lead to valid GPU memory. The decoy contains two masked instances.
    void *real = nullptr, *dummy = nullptr, *plist = nullptr, *output = nullptr;
    auto descriptors = d.buffer(128, input, &real);
    auto decoy = d.buffer(128, input, &dummy);
    auto pointers = d.buffer(32, input, &plist);
    auto result = d.buffer(8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &output);
    CHECK(descriptors && decoy && pointers && result);
    VkAccelerationStructureInstanceKHR instance{};
    instance.mask = 255;
    for (unsigned i = 0; i < 3; ++i)
        instance.transform.matrix[i][i] = 1;
    instance.accelerationStructureReference = blas.address;
    for (unsigned i = 0; i < 2; ++i) {
        instance.transform.matrix[0][3] = float(i) * 3;
        std::memcpy(static_cast<char *>(real) + i * 64, &instance, 64);
        auto masked = instance;
        masked.mask = 0;
        std::memcpy(static_cast<char *>(dummy) + i * 64, &masked, 64);
    }
    ResourceBinding desc{};
    desc.buffer = descriptors;
    desc.va = d.device_address(decoy);
    desc.size = 128;
    CHECK(desc.va && desc.va != d.device_address(descriptors));
    ctx.add_resource(1, desc);
    ResourceBinding p{};
    p.buffer = pointers;
    p.host = static_cast<const uint8_t *>(plist);
    p.va = 0x30000;
    p.size = 32;
    ctx.add_resource(2, p);
    auto entries = static_cast<uint64_t *>(plist) + 2;
    entries[0] = desc.va;
    entries[1] = desc.va + 64;

    std::ifstream file(shader, std::ios::binary | std::ios::ate);
    auto bytes = static_cast<std::streamoff>(file.tellg());
    CHECK(bytes > 0 && bytes % 4 == 0);
    std::vector<uint32_t> code(bytes / 4);
    file.seekg(0);
    CHECK(file.read(reinterpret_cast<char *>(code.data()), bytes));
    VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sm.codeSize = bytes;
    sm.pCode = code.data();
    VkShaderModule module{};
    CHECK(FN(vkCreateShaderModule)(d.device, &sm, nullptr, &module) == VK_SUCCESS);
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
    VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lc.pushConstantRangeCount = 1;
    lc.pPushConstantRanges = &range;
    VkPipelineLayout layout{};
    CHECK(FN(vkCreatePipelineLayout)(d.device, &lc, nullptr, &layout) == VK_SUCCESS);
    VkComputePipelineCreateInfo pc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pc.layout = layout;
    pc.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pc.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.stage.module = module;
    pc.stage.pName = "main";
    VkPipeline pipeline{};
    CHECK(FN(vkCreateComputePipelines)(d.device, VK_NULL_HANDLE, 1, &pc, nullptr, &pipeline) == VK_SUCCESS);
    BuildAccelerationStructure top{};
    top.is_tlas = true;
    top.dst_va = 0x20000;
    top.scratch_va = tlas.scratch;
    top.instance_pointers = true;
    top.instances_va = p.va + 16;
    top.instance_count = 2;
    auto check = [&](const char *label) {
        const auto built = submit(top);
        if (built != ReplayResult::success) {
            std::printf("%s: queue result=%d\n", label, static_cast<int>(built));
            return false;
        }
        std::memset(output, 0xff, 8);
        CHECK(d.run([&](auto cb) {
            barrier(d, cb, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
            std::array<uint64_t, 2> params{tlas.address, d.device_address(result)};
            FN(vkCmdBindPipeline)(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            FN(vkCmdPushConstants)(cb, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, params.data());
            FN(vkCmdDispatch)(cb, 1, 1, 1);
            barrier(d, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
            return true;
        }));
        auto hits = static_cast<const uint32_t *>(output);
        std::printf("%s: hit=%u miss=%u\n", label, hits[0], hits[1]);
        return hits[0] == 1 && hits[1] == 0;
    };
    const auto guest_base = desc.va;
    desc.va = d.device_address(descriptors);
    ctx.add_resource(1, desc);
    entries[0] = desc.va;
    entries[1] = desc.va + 64;
    bool control = check("matching device address control");
    desc.va = guest_base;
    ctx.add_resource(1, desc);
    entries[0] = desc.va;
    entries[1] = desc.va + 64;
    bool first = check("placed descriptor VA");
    p.va += 16;
    p.buffer_offset = 16;
    p.host += 16;
    p.size = 8;
    ctx.add_resource(3, p);
    bool second = check("overlapping pointer list");
    // Rebase the simulated arena so even the old translation selects the valid masked buffer.
    HostBuffer imported{d};
    bool external_pass = true;
    if (imported.create(mem)) {
        std::memcpy(imported.host, real, 128);
        ctx.remove_resource(3);
        desc.buffer = imported.buffer;
        desc.va = reinterpret_cast<uintptr_t>(imported.host);
        desc.host = static_cast<const uint8_t *>(imported.host);
        set_import_address(desc, d.device_address(imported.buffer));
        ctx.add_resource(1, desc);
        entries[0] = desc.va;
        entries[1] = desc.va + 64;
        const auto heap_base = d.device_address(decoy) - desc.va;
        ctx.set_heap_base(heap_base);
        top.scratch_va = tlas.scratch - heap_base;
        external_pass = check("EXT host allocation, host-pointer VA");
        top.instance_pointers = false;
        top.instances_va = desc.va;
        const bool contiguous = check("KHR contiguous imported descriptors");
        top.instance_pointers = true;
        top.instances_va = 0x30010;

        HostBuffer vertices{d};
        CHECK(vertices.create(mem));
        void *shifted = nullptr;
        auto alternate = d.buffer(128, input | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &shifted);
        CHECK(alternate);
        auto moved = coords;
        for (size_t i = 0; i < moved.size(); i += 3)
            moved[i] += 4;
        std::memcpy(static_cast<char *>(vertices.host) + 16, coords.data(), sizeof(coords));
        std::memcpy(static_cast<char *>(shifted) + 16, moved.data(), sizeof(moved));
        ResourceBinding vb{};
        vb.buffer = vertices.buffer;
        vb.va = reinterpret_cast<uintptr_t>(vertices.host);
        vb.size = 128;
        set_import_address(vb, d.device_address(vb.buffer));
        ctx.add_resource(4, vb);
        const auto geometry_heap = d.device_address(alternate) - vb.va;
        ctx.set_heap_base(geometry_heap);
        auto short_alias = vb;
        short_alias.buffer = alternate;
        short_alias.va += 16;
        short_alias.size = 8;
        short_alias.buffer_offset = 16;
        set_import_address(short_alias, d.device_address(alternate) + 16);
        ctx.add_resource(5, short_alias);
        b.triangles.front().vertex_va = vb.va + 16;
        b.scratch_va = blas.scratch - geometry_heap;
        top.scratch_va = tlas.scratch - geometry_heap;
        CHECK(submit(b) == ReplayResult::success);
        CHECK(d.run([&](auto cb) {
            barrier(d, cb, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
            return true;
        }));
        const bool khr_vertices = check("KHR imported triangle vertices with short alias");
        const bool xdxr_vertices = check_xdxr_vertices(d, vb, geometry_heap, b);
        external_pass = external_pass && contiguous && khr_vertices && xdxr_vertices;
    } else {
        std::puts("EXT host allocation: skipped (coherent addressable import unavailable)");
    }
    FN(vkDestroyPipeline)(d.device, pipeline, nullptr);
    FN(vkDestroyPipelineLayout)(d.device, layout, nullptr);
    FN(vkDestroyShaderModule)(d.device, module, nullptr);
    return control && first && second && external_pass && !d.validation_errors() ? 0 : 1;
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    try {
        vktest::Device d;
        d.request_buffer_device_address = true;
        auto s = d.initialize();
        if (s == vktest::DeviceStatus::unavailable)
            return 77;
        CHECK(s == vktest::DeviceStatus::ready);
        if (!d.acceleration_structure)
            return 77;
        return run(d, argv[1]);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Failed: %s\n", e.what());
        return 1;
    }
}
