# Graphics runtime patches

Ten source and test commits for the Leonida compatibility runtime, based on `de4c37383cad386eaa851047628d7b92c51009c5`. The series includes Wine test registration, Vulkan presentation and capture fixes, shader translation fixes, audio structure sizes, descriptor heap routing and regression tests. All GPU and audio results below use synthetic fixtures. Retail startup is unverified.

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
