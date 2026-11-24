# Graphics runtime patches

Seven source and test commits for the Leonida compatibility runtime, based on `de4c37383cad386eaa851047628d7b92c51009c5`. The series includes Wine test registration, a Vulkan presentation fix, shader translation fixes, audio structure sizes, descriptor heap routing and regression tests. All GPU and audio results below use synthetic fixtures. Retail startup is unverified.

## Apply the series

From a clean Leonida checkout containing the base commit, run the following commands with the path to the downloaded mailbox.

```sh
git switch -c runtime-integration de4c37383cad386eaa851047628d7b92c51009c5
git am /path/to/runtime-fixes.mbox
git rev-parse 'HEAD^{tree}'
```

The verified source tree is `6e7b8c299510e70772e9caa49195cc1425047d71`. Applying the published series to the base reproduced this tree.

## Combined verification

| Suite | Result |
| --- | --- |
| Translator semantics | 6,055 tests across 460 files, zero failed files |
| Native Release CTest on Linux | 68 passed, 2 skipped, zero failed. The skips require a translated corpus and opt-in Vulkan validation. |
| Native GPU suite on RTX 4070 Ti | 199 passed, 49 skipped, zero failed. All 255 shaders translated and passed SPIR-V validation. |
| Windows audio ABI under Wine | 65 checks, zero failures |

The Windows drawing guest and presenter measurements are in [windows-guest-frame-results.txt](windows-guest-frame-results.txt). Before-and-after shader measurements, including the emulated wave64 run, are in [RTX4070Ti-shader-results.txt](RTX4070Ti-shader-results.txt). The complete Windows runtime build is unverified.

## Source and attribution

- Shader fixes restore changes authored as Claude in [simomistralii/Leonida-PC PR 2](https://github.com/simomistralii/Leonida-PC/pull/2). The shader report identifies all three original commits.
- Audio padding matches [luizwidmer’s source snapshot](https://github.com/luizwidmer/Leonida-work/blob/7d61424381ea69e7422cd98e13ee2b682a58d055/runtime/shims/acphal.cpp).
- Descriptor routing extends the slot 14 work and tests in [Vyzerythe/Leonida PR 12](https://github.com/Vyzerythe/Leonida/pull/12). The series adds per-device heap creation and checks COM ownership and Xbox handle returns.
- The image-atomic test restores Kauê’s work from [PR 13](https://github.com/Vyzerythe/Leonida/pull/13). A follow-up regression checks the case where the inactive branch also reaches the nominally guarded block.
- The Wine CTest changes and zero-size Vulkan presenter fix were developed and verified with Codex. The discussion includes the isolated GPU test conditions. [PR 14](https://github.com/Vyzerythe/Leonida/pull/14#issuecomment-7119179715)

The patches retain the upstream [GNU General Public License version 3](LICENSE).
