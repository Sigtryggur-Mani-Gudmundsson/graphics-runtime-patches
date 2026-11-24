# Indexed Xbox quad replay

On tedin7’s rectangle and quad list branch, a non-indexed quad uses two triangles with the same first vertex. Indexed quads issue a separate Vulkan triangle fan per quad. A flat input can then select a different corner’s value, and two overlapping instances can overwrite each other in the wrong order. These limitations are documented in the original change. This follow-up reproduces both on the GPU and corrects indexed replay. [Original PR #207](https://github.com/ubiquitarianism/leonida/pull/207)

The patch expands each group of four application indices into two triangles in device-local scratch, in the command buffer before the draw. The draw keeps the application's base vertex, instance count and first instance. Out-of-view indices read zero and an incomplete final group is ignored. Scratch allocation must succeed and the expanded count must fit the Vulkan 32-bit draw count. Copy recording scales with the number of in-view quads. Performance on large production draws is unmeasured.

## RTX 4070 Ti results

The test uses the production command-list and Vulkan replay code, synthetic shaders, an application index buffer written by a GPU transfer, and a 64 × 64 render target. It compares each image with a separate triangle-list draw on the same GPU. In the flat-color case the first corner is red and the other three are black. In the overlap case two instances and two quads cover the same pixels. Green must be the last visible write. The non-indexed controls matched both reference images on both revisions.

| Indexed draw | Original pixels matching | Patched pixels matching |
| --- | ---: | ---: |
| Flat color | 0 | 4,096 |
| Overlapping instance order | 0 | 4,096 |
| 16-bit indices with nonzero first index and negative base vertex | 0 | 4,096 |
| Partial 16-bit view | 0 | 4,096 |
| Partial 32-bit view | 0 | 4,096 |
| 65 complete quads and a trailing vertex | 4,096 | 4,096 |
| Null index view | 4,096 | 4,096 |
| Fewer than four indices | 4,096 | 4,096 |

Khronos synchronization validation was enabled for the GPU runs. The original regression exited 1, and the patched regression exited 0. The CTest targets `replay_quad_list`, `vulkan_scratch` and `vk_command_replay` passed. This exercised native Linux Vulkan replay. The Windows D3D12 entry points, retail startup and the rectangle-list geometry stage were outside this measurement.

## Reproduce

Use PR #207 head `21dc3889280dba49916f63342dab72750d3ee4b9` and apply [indexed-quad-replay.mbox](../../indexed-quad-replay.mbox) with `git am`. The patched source tree is `c476fe1c1b8819c85013e23fb2e6565c6475513c`. To run the negative control, compile the same fixture against the original PR head. Run from this fixture directory with a C++20 compiler, glslangValidator, Vulkan headers, loader, drivers and validation layer.

```sh
SOURCE=/absolute/path/to/Leonida
BUILD=$(mktemp -d)
glslangValidator -V quad.vert -o "$BUILD/quad.vert.spv"
glslangValidator -V quad.frag -o "$BUILD/quad.frag.spv"
c++ -std=c++20 -O1 -I"$SOURCE/runtime/test" -I"$SOURCE/runtime/src" draw.cpp -lvulkan -ldl -o "$BUILD/draw"
LEONIDA_VK_VALIDATION=1 "$BUILD/draw" "$BUILD/quad.vert.spv" "$BUILD/quad.frag.spv"
```
