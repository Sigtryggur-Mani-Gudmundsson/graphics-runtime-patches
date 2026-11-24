#include "d3d12_xs_device.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <dxgi1_4.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

int main() {
    ComPtr<ID3D12Device> owner;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&owner));
    if (FAILED(hr)) {
        std::printf("SKIP D3D12CreateDevice: 0x%08lx\n", static_cast<unsigned long>(hr));
        return 77;
    }
    ID3D12Device* real = owner.Get();
    ComPtr<ID3D12Device> proxy;
    hr = rt::d3d12_xs::create_device_proxy(real, IID_PPV_ARGS(&proxy));
    if (FAILED(hr)) {
        std::printf("FAIL create_device_proxy: 0x%08lx\n", static_cast<unsigned long>(hr));
        return 1;
    }
    owner.Detach(); // The proxy owns the reference after creation.
    void** slots = *reinterpret_cast<void***>(proxy.Get());
    using HeapFn = D3D12_HEAP_PROPERTIES*(WINAPI*)(D3D12_HEAP_PROPERTIES*, void*, UINT, D3D12_HEAP_TYPE);
    using AllocFn = D3D12_RESOURCE_ALLOCATION_INFO*(WINAPI*)(D3D12_RESOURCE_ALLOCATION_INFO*, void*, UINT, UINT,
                                                             const D3D12_RESOURCE_DESC*);
    const auto xbox_heap = reinterpret_cast<HeapFn>(slots[26]);
    const auto xbox_alloc = reinterpret_cast<AllocFn>(slots[25]);
    bool ok = true;
    for (auto type : {D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_TYPE_READBACK}) {
        const auto reference = real->GetCustomHeapProperties(1, type);
        const auto pc = proxy->GetCustomHeapProperties(1, type);
        D3D12_HEAP_PROPERTIES xbox{};
        const auto* returned = xbox_heap(&xbox, proxy.Get(), 1, type);
        const bool matches = returned == &xbox && reference.VisibleNodeMask != 0 &&
                             std::memcmp(&pc, &reference, sizeof(pc)) == 0 &&
                             std::memcmp(&xbox, &reference, sizeof(xbox)) == 0;
        std::printf("%s slot 26 type=%u CPU page=%u pool=%u visible mask=%u\n", matches ? "PASS" : "FAIL",
                    type, reference.CPUPageProperty, reference.MemoryPoolPreference, reference.VisibleNodeMask);
        ok &= matches;
    }
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 4096;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const auto reference = real->GetResourceAllocationInfo(0, 1, &desc);
    const auto pc = proxy->GetResourceAllocationInfo(0, 1, &desc);
    D3D12_RESOURCE_ALLOCATION_INFO xbox{};
    const auto* returned = xbox_alloc(&xbox, proxy.Get(), 0, 1, &desc);
    const bool matches = returned == &xbox && reference.Alignment && reference.SizeInBytes >= desc.Width &&
                         reference.SizeInBytes != ~UINT64{0} && pc.SizeInBytes == reference.SizeInBytes &&
                         pc.Alignment == reference.Alignment && xbox.SizeInBytes == reference.SizeInBytes &&
                         xbox.Alignment == reference.Alignment;
    std::printf("%s slot 25 size=%llu alignment=%llu\n", matches ? "PASS" : "FAIL",
                reference.SizeInBytes, reference.Alignment);
    return ok && matches ? 0 : 1;
}
