// Run against the standalone Vulkan device and the Xbox command-list proxy. Uses only SDK and synthetic IIDs.
#include "../shims/d3d12_com.h"
#include "../shims/d3d12_xs_device.h"
#include "d3d12_test_shaders.h"
#include "vk_test_device.h"
#include <cstdio>
#include <cstring>

using d3d12test::Ref;
using CreateListX = HRESULT(WINAPI*)(ID3D12Device*, const UINT*, ID3D12CommandAllocator*,
                                     ID3D12PipelineState*, REFIID, void**);
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const bool private_id = std::strcmp(argv[1], "private") == 0;
    const bool newer = std::strcmp(argv[1], "newer") == 0;
    if (!private_id && !newer && std::strcmp(argv[1], "base") != 0) return 2;
    vktest::Device vk;
    const auto status = vk.initialize();
    if (status == vktest::DeviceStatus::unavailable) return 77;
    CHECK(status == vktest::DeviceStatus::ready);
    rt::d3d12::VulkanDeviceInfo info{};
    info.get_instance_proc = vk.instance_proc;
    info.instance = vk.instance;
    info.physical = vk.physical;
    info.device = vk.device;
    info.queue = vk.queue;
    info.queue_family = vk.family;
    ID3D12Device* inner = nullptr;
    CHECK(rt::d3d12::create_vulkan_device(info, IID_PPV_ARGS(&inner)) == S_OK);
    Ref<ID3D12Device> device;
    CHECK(rt::d3d12_xs::create_device_proxy(inner, IID_PPV_ARGS(&device.p)) == S_OK);
    rt::d3d12::set_device_outer(inner, device.p);
    Ref<ID3D12CommandAllocator> allocator;
    CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator.p)) == S_OK);
    const IID synthetic{0x75aada94, 0x501f, 0x435b, {0xa9, 0xc2, 0x4e, 0x15, 0x18, 0x67, 0xe2, 0x64}};
    const IID& iid = private_id ? synthetic : newer ? __uuidof(ID3D12GraphicsCommandList9)
                                                    : __uuidof(ID3D12GraphicsCommandList);
    const UINT desc[4]{D3D12_COMMAND_LIST_TYPE_DIRECT, 0, 0, 0};
    const auto create = reinterpret_cast<CreateListX>(d3d12test::slot(device.p, 73));
    Ref<ID3D12GraphicsCommandList> list;
    const HRESULT hr = create(device.p, desc, allocator.p, nullptr, iid, reinterpret_cast<void**>(&list.p));
    std::printf("%s: CreateCommandListX 0x%08lx, object %s\n", argv[1], static_cast<unsigned long>(hr),
                list.p ? "present" : "null");
    if (newer) {
        // The backend exposes GraphicsCommandList8. A caller requesting revision 9 must see its refusal.
        CHECK(hr == E_NOINTERFACE && !list.p);
        return 0;
    }
    CHECK(hr == S_OK && list.p && rt::d3d12_xs::is_command_list_proxy(list.p));
    Ref<IUnknown> queried;
    const HRESULT qi = list->QueryInterface(iid, reinterpret_cast<void**>(&queried.p));
    std::printf("query creation IID 0x%08lx, same object %d\n", static_cast<unsigned long>(qi), queried.p == list.p);
    Ref<ID3D12CommandQueue> queue;
    Ref<ID3D12Fence> fence;
    const D3D12_COMMAND_QUEUE_DESC qdesc{};
    CHECK(device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue.p)) == S_OK);
    CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.p)) == S_OK);
    CHECK(list->Close() == S_OK);
    ID3D12CommandList* lists[] = {list.p};
    queue->ExecuteCommandLists(1, lists);
    auto* real_queue = rt::d3d12_xs::queue_proxy_real_queue(queue.p);
    CHECK(rt::d3d12::last_execute_result(real_queue) == leonida::ReplayResult::success);
    CHECK(queue->Signal(fence.p, 1) == S_OK);
    CHECK(fence->SetEventOnCompletion(1, nullptr) == S_OK);
    CHECK(fence->GetCompletedValue() == 1);
    std::puts("closed and submitted, fence reached 1");
    return 0;
}
