# Graphics runtime patches

Ten source and test commits for the Leonida compatibility runtime, based on `de4c37383cad386eaa851047628d7b92c51009c5`. The series includes Wine test registration, Vulkan presentation and capture fixes, shader translation fixes, audio structure sizes, descriptor heap routing and regression tests. All GPU and audio results below use synthetic fixtures. Retail startup is unverified.

## Refreshing title-backed descriptor tables

On the frame-loop branch, the GPU missed updates to descriptors in a rebound table. The two commits in [descriptor-table-refresh.mbox](descriptor-table-refresh.mbox) connect the Xbox heap backing to the submission-time scan, retain the largest declared range at each address, and route the scan through the submitting device. On an RTX 4070 Ti under Wine, reusing the same closed list produced the expected new 64-word pattern on both submissions. This also passed with a second compatibility device alive. [Controls, reproduction and combined patch order](tests/descriptor-table-refresh/README.md)

When the game copies an API-written descriptor with the CPU, the Vulkan heap can be correct while the game's copy still contains zeros. Applying [descriptor-write-through.mbox](descriptor-write-through.mbox) after the two refresh commits fixed the CPU copy under Wine. Two consecutive GPU submissions went from 0/64 expected words each to 64/64 each. [Write-through results and reproduction](tests/descriptor-write-through/README.md)

## Optional graphics layout on the frame-loop branch

Apply [graphics-root-layout.mbox](graphics-root-layout.mbox) to Mumogg's frame-loop branch at `e0d4fd2b81e6636b1c6bb2f76be79e910e84d84b`. The fix accepts descriptor tables that fit the selected graphics layout and preserves both address words when a signature falls back to the default layout. A synthetic 17-table draw failed before submission on the original branch. With the fix, three draws produced the expected values across all 4,096 pixels on an RTX 4070 Ti under Wine. [Regression cases and pixel reproduction](tests/graphics-root-layout/README.md)

## Apply the series

From a clean Leonida checkout containing the base commit, run the following commands with the path to the downloaded mailbox.

```sh
git switch -c runtime-integration de4c37383cad386eaa851047628d7b92c51009c5
git am /path/to/runtime-fixes.mbox
git rev-parse 'HEAD^{tree}'
```

The verified source tree is `039706d73610f47bb0cd7f2b387395f2df856533`. Applying the published series to the base reproduced this tree.

## Verification

The first seven commits were tested together in the suites below. The two presenter follow-up commits were tested in the source-built Windows presenter on Wine/XWayland. An injected out-of-date present invalidated readback, a suboptimal present retained it, and the captured pixel counts matched all three synthetic images.

| Suite | Result |
| --- | --- |
| Translator semantics | 6,055 tests across 460 files, zero failed files |
| Native Release CTest on Linux | 68 passed, 2 skipped, zero failed. The skips require a translated corpus and opt-in Vulkan validation. |
| Native GPU suite on RTX 4070 Ti | 199 passed, 49 skipped, zero failed. All 255 shaders translated and passed SPIR-V validation. |
| Windows audio ABI under Wine | 65 checks, zero failures |

The Windows drawing guest and presenter measurements are in [windows-guest-frame-results.txt](windows-guest-frame-results.txt). Before-and-after shader measurements, including the emulated wave64 run, are in [RTX4070Ti-shader-results.txt](RTX4070Ti-shader-results.txt). The complete Windows runtime build is unverified.

The tenth commit fixes the optional KHR ray-tracing path. A short overlapping buffer could hide the CPU mapping for an instance pointer list. The queue also used the global arena base when translating descriptors in imported buffers. The new queue regression checks the staged Vulkan addresses, a short alias, an incomplete range, an unreadable list and a missing device address. It passed on Linux and as a Windows executable under Wine using a Vulkan test double. The final Linux CTest run passed 69 tests and skipped the same two listed above. A separate two-ray GPU test verified the KHR build on an RTX 4070 Ti, including a descriptor buffer imported from host memory. The Windows D3D12 shim changes remain unverified. [GPU test, results and reproduction instructions](tests/khr-instance-addresses/README.md)

## Imported geometry in the default and KHR ray-tracing backends

After the ten runtime commits, apply [direct-geometry.mbox](direct-geometry.mbox) with `git am /path/to/direct-geometry.mbox`. This patch applies independently of the archive-selection changes. It records the Vulkan device address for imported host memory and resolves direct ray-build inputs through their registered address ranges.

With the same imported triangle, the default XDXR builder previously produced bounds from x=4 to x=5. The patched builder produced the supplied bounds from x=0 to x=1. The optional KHR path also recovered the expected ray hit for both imported vertices and a contiguous imported instance array. These are Linux RTX 4070 Ti measurements. [Hardware reproduction and results](tests/khr-instance-addresses/README.md)

## GPU-written instance pointers on the default XDXR backend

Apply [xdxr-instance-pointers.mbox](xdxr-instance-pointers.mbox) after direct-geometry.mbox with `git am /path/to/xdxr-instance-pointers.mbox`. Without the independent archive-selection patch, the resulting tree is `fe9989a41c621574a59e2595dd4229a5fae757d1`. The mailbox includes the compute shader source and the generated include used by the normal runtime build. Compiling the shader with the command in xdxr_build_spv.h reproduced the include byte for byte.

XDXR previously treated a top-level instance pointer list as a packed array of descriptors. The fix reads each pointer on the GPU and resolves it to the descriptor, including addresses in registered host imports. One test uses only arena addresses. Another mixes arena and imported addresses with an overlapping alias. The refit rewrites the pointer list on the GPU and changes the physical order of the descriptors.

| Synthetic case, 1,000 rays each | Reference hits | Previous XDXR hits | Patched XDXR hits |
| --- | ---: | ---: | ---: |
| Arena pointers | 570 | 0 | 570 |
| Mixed pointers | 575 | 0 | 575 |
| Refit after GPU list rewrite | 539 | 0 | 539 |

Linux and the cross-compiled Windows test under Wine used an RTX 4070 Ti. All 3,000 patched ray results matched the brute-force reference on each platform. To reproduce on Linux, build the runtime tests and run `ctest -R '^xdxr_build(_check)?$' --output-on-failure` in the build directory. The generator uses synthetic inputs and the checker compares GPU output with CPU traversal. Retail startup remains unverified.

## Reading black-frame and missing-shader evidence from a boot log

Apply [boot-render-diagnostics.mbox](boot-render-diagnostics.mbox) with `git am /path/to/boot-render-diagnostics.mbox`. It applies independently to the original source at `de4c37383cad386eaa851047628d7b92c51009c5`. A clean replay produced tree `3da5e874c3c0afb6a51426a347531555e8d712c3` and passed 42 trace, wrapper, and documentation tests.

Run `python -I tools/boot_trace.py --d3d12 d3d12.log --counts-only` to share aggregate measurements. The parser recognizes lookup failures by shader stage and the stub creation milestones from rafaelpssz’s restoration of the diagnostic switch. Each milestone gives a lower bound across graphics and compute. The log cannot establish how many draws used those stubs. [ubiquitarianism/leonida PR 42](https://github.com/ubiquitarianism/leonida/pull/42)

On the saved Windows synthetic presenter run, the parser found one fully black capture and two with color, including the single-pixel frame. With the older rounded-mean output it reported one capture with color and two inconclusive captures. These counts describe the presenter readback in that run. The retail black-screen cause remains open.

## Recovering a damaged shader index

Apply [shader-index-recovery.mbox](shader-index-recovery.mbox) with `git am /path/to/shader-index-recovery.mbox`. The patch applies independently to `de4c37383cad386eaa851047628d7b92c51009c5`. Its clean replay produced tree `b83aa1dcc162f61dfa061b8a1ea1c38e75e66e70`.

Previously, the shaders stage could report success after a user reran it with a truncated `shaders/spirv/index.json`. The pipelines stage then rejected the same file and asked for another rerun. The fix rebuilds a malformed index from completed shards when the translation plan still matches. The low-disk preflight accounts for a merge-only retry. A temporarily locked index is preserved.

The regression uses two synthetic pixel shaders, the actual translator, SPIRV-Tools and two shard workers. After truncating the merged index, the patched shaders stage restored the exact original bytes. Both compiled modules and both shard indexes retained their contents and modification times. Run `python -I tests/test_make_game_empty_translation.py ShadersStageTest.test_real_shards_restore_the_index_without_rebuilding_modules` with SPIRV-Tools on PATH to repeat it.

## Vertex shader selection for programs with shared hashes

Apply [vertex-input-signature.mbox](vertex-input-signature.mbox) with `git am /path/to/vertex-input-signature.mbox`. A clean replay on `de4c37383cad386eaa851047628d7b92c51009c5` produced tree `04098226eb9b51da255e5c0a197b9bd281fbad84`. The runtime reads the existing translator index.

Two vertex shader containers can hold the same native program and declare the same semantic names while assigning POSITION and COLOR to different registers. Both translations pass the input-layout check. The patch compares the container’s ISG1 input signature against the indexed candidates before applying the existing layout and binding preferences.

A synthetic 64 × 64 Vulkan draw on an RTX 4070 Ti used the same mesh, pipeline shaders and render target for both runs. With the original lookup all 4,096 pixels retained the clear value. With the matching signature all 4,096 pixels were green as expected. The Windows shader-library regression also passed under Wine. The GPU measurement covered native Vulkan vertex fetch. The full Windows graphics pipeline and retail startup remain unverified for this change.

The [reproduction script](tests/vertex-input-signature/render.py) generates both containers, obtains their metadata from the translator and exercises the two lookups. With a patched source checkout, a built `vk_harness`, Python, a C++20 compiler and `glslangValidator` on Linux, run `python -I tests/vertex-input-signature/render.py /path/to/source /path/to/vk_harness` from this patch collection.

## Pixel shaders with the same program and different position inputs

Apply [pixel-input-signature.mbox](pixel-input-signature.mbox) after the vertex-input-signature patch. Applying both to `de4c37383cad386eaa851047628d7b92c51009c5` produced tree `226c3b802540d749f6f553f9655d6391f609a6f8`.

The source pixel shader in this test exports v2 to red. The container’s `SV_Position` read mask determines whether v2 holds depth or reciprocal W. The test assembles one RDNA2 program and packages it in two synthetic containers. Leonida’s unpacker, disassembler and translator produce two validated SPIR-V modules with the same FSSM hash. The runtime now uses the input signature to choose between them. The translator also records a valid empty signature explicitly so the runtime can distinguish it from missing metadata.

| Requested input | First entry in index | Original lookup | Patched lookup |
| --- | --- | --- | --- |
| Reciprocal W = 1 | Depth | 4,096 black pixels | 4,096 red pixels |
| Depth = 0 | Reciprocal W | 4,096 red pixels | 4,096 black pixels |

Both rows used the same full-screen triangle on an RTX 4070 Ti. A Windows build of the shader-library regression passed under Wine. The GPU readback came from the native Vulkan harness. A retail run with these aliases remains unverified.

With LLVM’s AMDGPU assembler, SPIRV-Tools, glslangValidator, a C++20 compiler and Python on Linux, run `python -I tests/pixel-input-signature/render.py /path/to/patched/source /path/to/vk_harness`. [Reproduction script](tests/pixel-input-signature/render.py)

## Pixel shader front-face masks

Apply [front-face-mask.mbox](front-face-mask.mbox) with `git am` on the base commit. The resulting standalone tree is `11dccca8bd04ab16dec89a39f8aacf7cf497741a`. The first commit restores Rafael’s translator fix and unit test. The second adds a mixed-winding GPU regression. [Upstream PR and evidence](https://github.com/ubiquitarianism/leonida/pull/40)

The test renders two quads with opposite winding. Its hand-written pixel shader checks the facing register for nonzero, all bits set, and zero. On an RTX 4070 Ti the original translator produced the same color for all 4,096 pixels. The patched shader matched a GLSL reference pixel for pixel, with 2,048 pixels of each winding. This test uses synthetic geometry and shaders.

With a built vk_harness, glslangValidator, spirv-as and spirv-val available, run these commands from the patched checkout.

```sh
python3 -I tests/test_ps_front_face.py
LEONIDA_VK_HARNESS=/absolute/path/to/vk_harness python3 -I tests/test_ps_front_face_gpu.py
```

## Index buffer view bounds in Vulkan replay

Apply [index-view-bounds.mbox](index-view-bounds.mbox) with `git am` to the base commit. This mailbox preserves tedin7’s six commits from [PR 326](https://github.com/ubiquitarianism/leonida/pull/326). Its standalone tree is `049288a4fe5ac64628d2fe42e6f3140028165fc0`.

Indexed draws now preserve indices inside a view and read zero beyond its end. A native RTX 4070 Ti readback reproduced eight wrong images on the original replay. All ten images matched after the fix, with synchronization validation enabled. [Test source, results and reproduction instructions](tests/index-view/README.md)

## Recovering pipeline shaders with register tuples that end in VCC

Apply [vcc-tuple-recovery.mbox](vcc-tuple-recovery.mbox) with `git am` to the base commit. The first commit preserves tedin7’s [PR 327](https://github.com/ubiquitarianism/leonida/pull/327). The second recovers a shader instruction consumed by LLVM 19 after a register tuple and bounds recovery to the declared program. Applying both to the base produced tree `e6e6d927ab2abdd135f6a5fb5f6507ffef232bf1`.

The original PR refused all six synthetic programs from real LLVM 19 listings. The follow-up validated all six under LLVM 19.1.7 and 23.1.5. Five programs produced the expected words on an RTX 4070 Ti under each toolchain. [Test, measurements and reproduction commands](tests/vcc-tuples/README.md)

## LLVM 19 inline constants in pixel shader conversions

[sdwa-inline-recovery.mbox](sdwa-inline-recovery.mbox) applies independently to the base commit. LLVM 19 can print an eight-byte signed or unsigned integer-to-half conversion as twelve bytes. The translator now recovers the inline value from the native shader and advances by the correct width.

On an RTX 4070 Ti, the previously rejected LLVM 19 pixel shader matched the reference in all 4,096 pixels. LLVM 23 and the Python decoder produced the same pixels. Both versions of LLVM also passed the decoder parity suite after correcting the lane-selector spelling and null export operand. [GPU fixture, expected values and commands](tests/sdwa-inline/README.md)

## Buffer writes on a dedicated copy queue

Apply [copy-queue-sync.mbox](copy-queue-sync.mbox) and [buffer-copy-order.mbox](buffer-copy-order.mbox) to the base commit. The two series preserve the contributions in PRs 314 and 354 and add checks for the copy queue's stage filter and an unavailable barrier callback. The dedicated transfer queue on an RTX 4070 Ti reproduced four synchronization hazards before the corrections. [Test results, attribution and reproduction commands](tests/copy-queue-sync/README.md)

## Fullscreen rectangle coverage on the boot integration branch

For the corner order reported on PR #321, the branch's default drew half the target. The backport restores tedin7’s adaptive corner selection and preserves the first submitted vertex for flat colors. It filled the target in all 24 axis-aligned corner orders on an RTX 4070 Ti. The original override and the patched override gave identical synthetic readbacks. [Backport](boot-rectangle-corners.mbox) · [GPU comparisons and reproduction steps](tests/boot-rectangles/README.md)

## Indexed Xbox quad colors and instance order

The follow-up to tedin7’s rectangle and quad list PR fixes the indexed path. The original GPU replay rendered a full red quad black and drew the wrong overlapping instance last. On the RTX 4070 Ti the patched replay matched the reference in all 4,096 pixels of each of eight synthetic frames. This patch applies to PR #207 head `21dc3889`. [Patch](indexed-quad-replay.mbox) · [Test, results and reproduction steps](tests/indexed-quads/README.md) · [Upstream PR](https://github.com/ubiquitarianism/leonida/pull/207)

## Recovering a lost presentation surface

The Xbox window presenter can lose its Vulkan surface while checking supported formats or creating a replacement swapchain. The old code handled surface loss during acquire and present, but an error during setup could leave startup without a usable presenter or make it retry the same dead surface. The fix retires the lost surface, keeps the last capture unavailable during recovery and uses the retained window to create a new surface on a later frame. [Source patch and regression test](presenter-surface-recovery.mbox)

The test injects `VK_ERROR_SURFACE_LOST_KHR` during startup format enumeration and during live swapchain creation. The unpatched startup control failed at `presenter.ok()`. An independent control that started normally failed on its first present after the injected swapchain loss. On the patched code, one temporarily unavailable window produced no stale capture or extra presented frame. When the window returned, the test read back all 12,288 pixels of a 128 × 96 colour frame within the existing two-level RGB tolerance. A separate allocation failure retried on the same surface. The full presenter synchronization test passed on Linux with Wine 11.22, a private XWayland display and an RTX 4070 Ti. The surface-loss results were injected. Surface creation, Vulkan submission and pixel readback used the driver. Retail startup was untested. [Vulkan surface capability query](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceCapabilitiesKHR.html) · [Vulkan swapchain creation](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateSwapchainKHR.html)

Apply `git am /path/to/presenter-surface-recovery.mbox` to the original base or upstream `ece4ba5b`. The resulting trees are `9c55e2725f66e58e9659bbc2e312bf43b6e80fdf` and `ff4b405209f8dde9bad017393301b7037024d170`, respectively. Build and run `vk_presenter_sync_test` on a Vulkan-capable Windows display or under Wine.

## Archive selection in hashdict

The optional [archive-selection.mbox](archive-selection.mbox) restores Yash Dubey’s fix from [ubiquitarianism/leonida PR 25](https://github.com/ubiquitarianism/leonida/pull/25) and adds a regression check for ordinary data in the second selected archive. Spaces around comma-separated archive names are accepted. An explicitly named archive missing from the index now produces a usage error before the tool writes the output dictionary.

After applying the ten runtime commits, apply this mailbox with `git am /path/to/archive-selection.mbox`. The resulting source tree is `4acb45e133883543f3db5e92c2b2c1748f37099d`. All 42 cases in `python -I tests/test_hashdict.py` passed on a fresh application. Before the fix, the same suite reproduced both the selection error and a successful exit for a misspelled name.

## Source and attribution

- Shader fixes restore changes authored as Claude in [simomistralii/Leonida-PC PR 2](https://github.com/simomistralii/Leonida-PC/pull/2). The shader report identifies all three original commits.
- Audio padding matches [luizwidmer’s source snapshot](https://github.com/luizwidmer/Leonida-work/blob/7d61424381ea69e7422cd98e13ee2b682a58d055/runtime/shims/acphal.cpp).
- Descriptor routing extends the slot 14 work and tests in [Vyzerythe/Leonida PR 12](https://github.com/Vyzerythe/Leonida/pull/12). The series adds per-device heap creation and checks COM ownership and Xbox handle returns.
- The image-atomic test restores Kauê’s work from [PR 13](https://github.com/Vyzerythe/Leonida/pull/13). A follow-up regression checks the case where the inactive branch also reaches the nominally guarded block.
- The Wine CTest changes and zero-size Vulkan presenter fix were developed and verified with Codex. The discussion includes the isolated GPU test conditions. [PR 14](https://github.com/Vyzerythe/Leonida/pull/14#issuecomment-7119179715)

The patches retain the upstream [GNU General Public License version 3](LICENSE).
