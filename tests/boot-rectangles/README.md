# Boot integration fullscreen rectangle test

Integration PR #321 describes a fullscreen rectangle submitted in top-left, bottom-left, bottom-right order. Its default geometry shader assumes the first vertex is the right-angle corner. The branch has an override that assumes the second vertex. Both versions use the first submitted vertex for flat attributes. On a synthetic draw in the reported order, the original default covered 1,536 of 3,072 pixels. [Boot integration report](https://github.com/ubiquitarianism/leonida/pull/321)

The [backport](../../boot-rectangle-corners.mbox) applies to `e0d4fd2b81e6636b1c6bb2f76be79e910e84d84b`, the head of #321. Two commits restore tedin7’s adaptive corner selection and rectangle tests from PR #207 with authorship preserved. One integration commit keeps the original first vertex at the start of both output triangles, retains their winding, and preserves the existing opt-in fixed-second-corner mode. The final Git tree is `6b30eb1fdd814e0e0c11baaf5c153536c28874a9`. [Original rectangle work](https://github.com/ubiquitarianism/leonida/pull/207)

## Native Vulkan results

The fixture tests all 24 ordered triples of three distinct corners of an axis-aligned fullscreen rectangle on a 64 × 48 target. The integration branch translates its hand-written M1.5 vertex and pixel shaders. One pass checks the interpolated color at every pixel center and another uses a flat fragment shader with distinct vertex colors and a facing bit. A second test draws two differently ordered rectangles in one call. Each pass reads the actual RGBA32F attachment. The first-vertex expectation is the existing boot branch contract. Xbox hardware behavior remains unmeasured.

| Geometry shader | Correct gradient orders | Correct flat color and facing orders | Correct pixels on two-rectangle draw, gradient / flat |
| --- | ---: | ---: | ---: |
| Boot branch default | 8/24 | 8/24 | 2,304 / 1,536 |
| Boot branch fixed-second override | 8/24 | 8/24 | 1,536 / 1,536 |
| PR #207 at `21dc3889` | 24/24 | 8/24 | 3,072 / 1,536 |
| Backport default | 24/24 | 24/24 | 3,072 / 3,072 |
| Backport fixed-second override | 8/24 | 8/24 | 1,536 / 1,536 |

On an RTX 4070 Ti the patched default matched all 153,600 pixel comparisons. The SHA-256 of all 50 concatenated RGBA32F captures was `13df8b6ebbd44ebddeccf022a8cae1ada2b8d754af12dc47ebbe2b8a91f32bd6`. The original and patched override produced the same capture hash, `8db4bd41d34e3e31059278696dc865b292fa8f8bd157a8af8b645458093937f1`. The override deliberately forces a specific corner, so it fails the all-orders test. The geometry modules and the flat shader passed SPIR-V validation.

These are synthetic GPU draws through the production geometry-stage generator. Retail startup, the Windows D3D12 front end, perspective-varying corners and rotated rectangles in non-square viewports were outside this measurement. PR #321’s author reported that even with fullscreen coverage the title composed a transparent layer.

## Reproduce on Linux

Apply the mailbox to the specified #321 checkout with `git am`. From this fixture directory, run the commands below with a C++20 compiler, CMake, a supported LLVM AMDGPU assembler, glslangValidator, SPIRV-Tools and a Vulkan device supporting geometry shaders and the translated M1.5 shader features. Set `Vulkan_INCLUDE_DIR` during configuration if the headers are outside the compiler’s search paths.

```sh
SOURCE=/absolute/path/to/patched/Leonida
BUILD=$(mktemp -d)
cmake -S "$SOURCE/runtime" -B "$BUILD/native" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD/native" --target vk_harness
python3 -I "$SOURCE/runtime/test/shaders/build.py" "$BUILD/m15" vs_m15 ps_m15
c++ -std=c++20 -O2 -I"$SOURCE/runtime/src" emit.cpp -o "$BUILD/emit"
env -u LEONIDA_RECT_FOURTH "$BUILD/emit" "$BUILD/m15/spirv/vs_m15.spv" "$BUILD/geometry.spv"
spirv-val --target-env vulkan1.2 "$BUILD/geometry.spv"
glslangValidator -V first.frag -o "$BUILD/flat.spv"
python3 -I check.py "$BUILD/native/vk_harness" "$BUILD/m15/spirv" "$BUILD/geometry.spv" "$BUILD/flat.spv"
```

For the fixed-second control, regenerate the geometry module with `LEONIDA_RECT_FOURTH=v1v3` set on the emitter process. To test the integration default or PR #207, compile the same emitter with that checkout’s include path and keep the harness, shaders and pixel oracle identical.
