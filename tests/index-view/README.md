# Indexed draw readback

This native Vulkan test records a Leonida indexed point draw and reads back an eight-pixel render target. Each index lights its own pixel. Valid indices beyond the D3D12 view and beside the zero page make incorrect fetches visible. The test submits through CommandList and replay(), including the production fill, copy, index bind and draw. The fixture supplies the synthetic resource, pipeline and scratch bindings.

Compare the same sources against the original checkout and a checkout with tedin7’s six commits applied. The ten cases ran on Linux on an NVIDIA GeForce RTX 4070 Ti with Khronos synchronization validation enabled. The original replay matched two cases. Both the patched restoration and an independent application to base `de4c373` matched all ten. No validation errors were reported. The Windows D3D12 entry points and retail content were outside this run. [Original author and pull request](https://github.com/ubiquitarianism/leonida/pull/326)

Each hex mask has bit N set when pixel N was white. Every other pixel was transparent black.

| Case | Original | Patched | Expected |
| --- | --- | --- | --- |
| Inside, 16-bit | 0c | 0c | 0c |
| Inside, 32-bit | 38 | 38 | 38 |
| Wholly outside | 18 | 01 | 01 |
| Odd 16-bit prefix, partial trailing index | 38 | 09 | 09 |
| 32-bit prefix | 3c | 0d | 0d |
| View clipped to the resource end | 06 | 03 | 03 |
| Wholly outside, larger than zero page | f6 | 01 | 01 |
| Empty view | 06 | 01 | 01 |
| Null view, nonzero first index | 80 | 01 | 01 |
| Null view, larger than zero page | 81 | 01 | 01 |

## Reproduce on Linux

Run from this fixture directory with a C++20 compiler, Vulkan headers and loader, glslangValidator, and the Khronos validation layer. The device must support Vulkan 1.3 and the runtime’s 144-byte push constant range. Set SOURCE to the Leonida checkout to test. A failing pixel comparison returns 1. An unavailable Vulkan device returns 77.

```sh
SOURCE=/absolute/path/to/Leonida
BUILD=$(mktemp -d)
glslangValidator -V points.vert -o "$BUILD/points.vert.spv"
glslangValidator -V points.frag -o "$BUILD/points.frag.spv"
c++ -std=c++20 -O1 -I"$SOURCE/runtime/test" -I"$SOURCE/runtime/src" draw.cpp -lvulkan -ldl -o "$BUILD/draw"
LEONIDA_VK_VALIDATION=1 "$BUILD/draw" "$BUILD/points.vert.spv" "$BUILD/points.frag.spv"
```
