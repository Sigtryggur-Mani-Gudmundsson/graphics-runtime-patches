# Host Vulkan validation in Wine

On a CachyOS host with an RTX 4070 Ti, Wine 11.22 and the Linux Khronos validation layer 1.4.363, both presenter tests completed with host synchronization validation enabled. Both emitted a device-extension dependency warning. A Vulkan control built from the same source for Windows and Linux isolates that warning to the Wine path on this machine.

| Program | Exit | Host validation errors |
| --- | ---: | --- |
| Xbox presenter, Windows executable under Wine | 0 | Two instances of VUID 01387 |
| Presenter synchronization, Windows executable under Wine | 0 | One instance of VUID 01387 |
| Standalone device control, Windows executable under Wine | 0 | One instance of VUID 01387 |
| Standalone device control, native Linux | 0 | None |

The loader log confirmed insertion of the Khronos validation library in the device dispatch chain. The host reported that the enabled extension list included `VK_EXT_external_memory_dma_buf` without its dependency `VK_KHR_external_memory_fd`. The standalone control requests zero device extensions, creates one device and exits. This diagnostic can occur without starting the Leonida renderer.

Dezmonius’s Windows 10 / RTX 5070 report concerns a missing dispatch handle when a Windows validation layer is enabled. The Wine tests used Linux validation beneath Wine and left the guest validation switch off. A stack trace from the affected native setup would help identify the first failing Vulkan call. [Original Windows report on Mumogg’s integration PR](https://github.com/ubiquitarianism/leonida/pull/321#issuecomment-7133087537)

## Repeat the host comparison

Build `device-control.cpp` against Vulkan headers twice, linking the native binary to `libvulkan` and the Windows binary to the `vulkan-1.dll` import library. On the same Linux host and GPU, run the commands below with the Linux Khronos validation layer installed. If the layer is installed in a private directory, point `VK_LAYER_PATH` at its manifest and ensure its shared library is resolvable. The control prints the adapter name, device-extension count and return value.

```sh
export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
export VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT
export VK_LOADER_DEBUG=layer
./device-control >native.log 2>&1
wine ./device-control.exe >wine.log 2>&1
```

Inspect both logs for the loaded layer and for `VUID-vkCreateDevice-ppEnabledExtensionNames-01387`. The application can exit successfully when the layer reports a diagnostic. For the two presenter runs use the test builds described in the [companion report](README.md), set `LEONIDA_VK_VALIDATION=0` and pass the same host layer environment through to Wine.
