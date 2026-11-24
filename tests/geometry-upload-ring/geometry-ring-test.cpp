// Uses the integration branch's CPU-authored shader fixtures, graphics rig and pixel reader.
#define main upstream_bundle_test_main
#include "d3d12_bundle_test.cpp"
#undef main

int run_device() {
    vktest::Device vk;
    const auto device_status = vk.initialize();
    if (device_status == vktest::DeviceStatus::unavailable) {
        std::fprintf(stderr, "SKIP: no usable Vulkan 1.3 device\n");
        return 77;
    }
    REQUIRE(device_status == vktest::DeviceStatus::ready);

    // Layer shaders: FSSM-shaped containers found by hash in a translator-style index.
    const auto vs_program = fssm_program({0x56530101, 0xbf810000});
    const auto ps_program = fssm_program({0x50530101, 0xbf810000});
    const auto vs_container = shader_container(vs_program), ps_container = shader_container(ps_program);
    const testfiles::ScratchDir scratch("geometry_ring_test");
    const auto& library = scratch.path();
    REQUIRE(testfiles::copy_file(LEONIDA_TEST_VS_SPV, library / "vs_draw.spv"));
    REQUIRE(testfiles::copy_file(LEONIDA_TEST_PS_SPV, library / "ps_draw.spv"));
    {
        std::ofstream index(library / "index.json", std::ios::binary);
        index << "{\"vs_draw\": {\"stage\": \"vs\", \"fssm_sha256\": \"" << leonida::sha256_hex(vs_program)
              << "\", \"bindings\": [], \"push_constant_bytes\": 144, \"input_signature\": ["
                 "{\"semantic\": \"COLOR\", \"semantic_index\": 0, \"register\": 0, \"mask\": 15, \"rw_mask\": 15, "
                 "\"system_value\": 0, \"component_type\": \"float\"}, "
                 "{\"semantic\": \"POSITION\", \"semantic_index\": 0, \"register\": 1, \"mask\": 3, \"rw_mask\": 3, "
                 "\"system_value\": 0, \"component_type\": \"float\"}]},\n"
                 " \"ps_draw\": {\"stage\": \"ps\", \"fssm_sha256\": \"" << leonida::sha256_hex(ps_program)
              << "\", \"bindings\": [], \"push_constant_bytes\": 144, \"outputs\": [0]}}";
    }
    const unsigned errors_before = vk.validation_errors();
    {
        rt::d3d12::VulkanDeviceInfo info{};
        info.get_instance_proc = vk.instance_proc;
        info.instance = vk.instance;
        info.physical = vk.physical;
        info.device = vk.device;
        info.queue = vk.queue;
        info.queue_family = vk.family;
        info.buffer_device_address = true;
        info.shader_dir = library.string();
        Ref<ID3D12Device> ours;
        REQUIRE(SUCCEEDED(rt::d3d12::create_vulkan_device(info, IID_PPV_ARGS(&ours.p))));
        Rig rig;
        REQUIRE(rig.init(ours.p, vs_container, ps_container));
        const Vertex vertices[] = {{-1, 1, 255, 0, 0, 255}, {0, 1, 255, 0, 0, 255},
                                   {-1, 0, 255, 0, 0, 255}, {0, 0, 255, 0, 0, 255}};
        const std::uint16_t indices[] = {0, 1, 2, 2, 1, 3};
        std::string grid;
        HRESULT close = E_FAIL;
        REQUIRE(rig.render([&](Rig& r, ID3D12GraphicsCommandList* l) {
            r.targets(l);
            l->SetGraphicsRootSignature(r.root1.p);
            l->SetPipelineState(r.pso_a.p);
            l->IASetPrimitiveTopology(tlist);
            r.offset(l, 0, 0);
            r.tint(l, tint_red);
            D3D12_VERTEX_BUFFER_VIEW vb{reinterpret_cast<UINT64>(vertices), sizeof(vertices), sizeof(Vertex)};
            D3D12_INDEX_BUFFER_VIEW ib{reinterpret_cast<UINT64>(indices), sizeof(indices), DXGI_FORMAT_R16_UINT};
            l->IASetVertexBuffers(0, 1, &vb);
            l->IASetIndexBuffer(&ib);
            l->DrawIndexedInstanced(6, 1, 0, 0, 0);
        }, grid, close));
        REQUIRE(close == S_OK);
        std::printf("Geometry readback %s\n", grid.c_str());
        REQUIRE(grid == ROWS4("RRRRBBBB") " " BLANK4);
    }
    REQUIRE(vk.validation_errors() == errors_before);
    return 0;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const bool single = argc == 2 && std::strcmp(argv[1], "--single") == 0;
    if (argc != 1 && !single) return 2;
    const int first = run_device();
    if (first != 0 || single) return first;
    return run_device();
}
