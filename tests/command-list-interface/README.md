# Creating a command list through Xbox device slot 73

A Wine tester reported an unknown command-list interface at startup on main. Main forwards the requested IID to the underlying PC-shaped graphics device. The console factory in PR 359 retries a rejected request with the public base graphics-list IID. I tested that change on a real Vulkan queue, found a public-interface regression, and extracted a slot-73-only patch for main. [Boot report](https://github.com/ubiquitarianism/leonida/issues/14#issuecomment-7122860410) · [andretuta's PR 359](https://github.com/ubiquitarianism/leonida/pull/359) · [Maintainer's review of the earlier approach](https://github.com/ubiquitarianism/leonida/pull/142#issuecomment-7122095028)

## Measurements

The Windows executable used the Vulkan device, allocator, command-list and queue implementations from main at `8f481cf16f261c7f37458aeb45e8f4e19994146d` on an RTX 4070 Ti under Wine. Only the Xbox proxy implementation changed between variants. Each successful base or private creation closed an empty list, submitted it and waited for fence value 1. The private IID in the fixture is synthetic.

| Xbox proxy | Base PC list | Synthetic private list | Unsupported PC GraphicsCommandList9 |
| --- | --- | --- | --- |
| Unchanged main | Fence 1 | E_NOINTERFACE, null | E_NOINTERFACE, null |
| PR 359 at `e7b32cd9` | Fence 1 | Fence 1 | S_OK, returned base list |
| PR 359 plus review patch | Fence 1 | Fence 1 | E_NOINTERFACE, null |
| Main plus standalone patch | Fence 1 | Fence 1 | E_NOINTERFACE, null |

The capability check matters because this backend implements revisions through GraphicsCommandList8. The fix preserves backend refusal for the public list revisions known to the SDK. The main patch also tests that allocation failure propagates with one backend call. Neither patch embeds a retail identifier. On the private creation path, querying the returned proxy for the same synthetic IID still gives E_NOINTERFACE. The successful close and submission used the originally returned pointer. The test does not establish whether the title ever repeats that query.

## Apply and reproduce

For main, apply [command-list-interface.mbox](../../command-list-interface.mbox) to the SHA above. A clean application produced tree `273d2704d1294fad4ee4a9164b5f220a3d0e19bd`. For PR 359, apply [pr359-review.mbox](pr359-review.mbox) to its head `e7b32cd9196611bd0eb606fe9c57e31cf9eead36`. These mailboxes are alternatives for the respective branches. The full PR mock passed with nine intentional allocation failures. The full mock also passed on unchanged main and main plus the standalone patch after correcting the local LLVM-MinGW build. The earlier exit at slot 26 came from the local SDK adaptation.

To run the independent fixture, copy [list_interface_test.cpp](list_interface_test.cpp) into the checkout's `runtime/test` directory and add these lines alongside the existing `queue_proxy_test` target in `runtime/CMakeLists.txt`. Use a Windows SDK exposing GraphicsCommandList9 and a Vulkan 1.3-capable GPU.

```cmake
add_executable(list_interface_test test/list_interface_test.cpp)
target_link_libraries(list_interface_test PRIVATE gta6rt_d3d12_com d3d12 dxgi)
target_compile_definitions(list_interface_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
```

```powershell
cmake --build runtime/build --config Release --target list_interface_test xs_proxy_mock_test
runtime/build/Release/list_interface_test.exe base
runtime/build/Release/list_interface_test.exe private
runtime/build/Release/list_interface_test.exe newer
```

The local cross-build used the existing SDK struct-return and GUID adaptations. Native MSVC and retail startup were untested. YuhiAida identified the original boot stop. Mumogg independently reported 581 retail lists created and closed with the earlier fix. This contribution uses andretuta's fallback design from PR 359 and adds the public-version control and Vulkan fixture with Codex. [Independent retail measurement](https://github.com/ubiquitarianism/leonida/pull/142#issuecomment-7121480872) · [Cross-build details](../graphics-root-layout/README.md)

## Reproducing the LLVM-MinGW tests

The previous local SDK adaptation selected MSVC's by-value virtual declaration for `GetCustomHeapProperties` under LLVM-MinGW. That changed where the compiler passed the result buffer to the inner COM device. The [build script](build_proxy_tests.py) keeps the SDK's GNU virtual declarations, which take an explicit output pointer. It adds three nonvirtual convenience overloads in a build-directory copy of the header and derives MinGW's interface ID declarations from the SDK. The proxy and its upstream mock compile directly from the chosen checkout.

Use LLVM-MinGW targeting x86-64, Microsoft DirectX-Headers v1.614.1, and a separate output directory per checkout. Run the hardware check in a Windows or Wine environment with an available D3D12 adapter. It exits 77 if device creation fails.

```sh
python3 build_proxy_tests.py /path/to/checkout /path/to/DirectX-Headers/include/directx build/main
wine build/main/xs_proxy_mock_test.exe
wine build/main/host_device.exe
```

The [hardware fixture](host_device.cpp) compares three paths on the same device: a direct backend call, a PC C++ call through the proxy, and an Xbox-shaped raw call through the proxy. The Xbox call must also return the caller's buffer address. This follows the existing device test in the restoration of Senne Cools's slot-26 fix. [Upstream PR 349](https://github.com/ubiquitarianism/leonida/pull/349)

Both main at `8f481cf1` and main plus the standalone slot-73 patch passed the full mock and the hardware fixture. The hardware runs used Wine 11.22, an RTX 4070 Ti, driver 620.70.6, and two vkd3d-proton builds, 2.11.0 (`e681135f8bb3e4`) and 3.0.0 (`212991fc2c266bc`). All four hardware runs agreed on the following values.

| Call | Result |
| --- | --- |
| Slot 26, DEFAULT | CPU page 1, pool 2, visible node mask 1 |
| Slot 26, UPLOAD | CPU page 2, pool 1, visible node mask 1 |
| Slot 26, READBACK | CPU page 3, pool 1, visible node mask 1 |
| Slot 25, 4 KiB buffer | Allocation size and alignment both 65,536 bytes |

These measurements cover device queries during setup. Whether the retail title submits a visible draw remains open.
