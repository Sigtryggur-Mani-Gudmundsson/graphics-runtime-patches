# Graphics root table packing

This regression targets Mumogg's frame-loop branch at `e0d4fd2b81e6636b1c6bb2f76be79e910e84d84b`. It exercises the branch's optional graphics layout beginning at s8. The measured effect is on synthetic draws. The retail black-screen cause is still open. [Frame-loop integration PR](https://github.com/ubiquitarianism/leonida/pull/321)

## Command recording

Apply `graphics-root-layout.mbox` using `git am` on the base above. The expected tree is `08e397bb0b80d2ce89042f6d31de7995169ac0b2`. On a configured Windows runtime build with the existing bundle test enabled, run the commands below. Each new CTest entry sets its own layout environment.

```powershell
cmake --build runtime/build --config Release --target d3d12_bundle_test
ctest --test-dir runtime/build -C Release -R '^d3d12_(root_layout_(compact|legacy)|bundle)$' --output-on-failure
```

The test serializes each signature, creates it on the Vulkan-backed D3D12 device, and binds it at each bind point. For accepted graphics layouts it compares all 32 register words captured by a draw. The distinct high word belongs to a synthetic handle used only during recording.

| Graphics signature | Default layout | Optional layout |
| --- | --- | --- |
| One table and 24 constants | Two-word table starting at s0 | Falls back to the same two-word table |
| One table and one constant | Two-word table followed by the constant | One-word table at s8 followed by the constant at s9 |
| 17 tables | Graphics bind rejected | Fits through s24 |
| 24 tables | Graphics bind rejected | Fits through s31 |
| 25 tables | Graphics bind rejected | Graphics bind rejected |

All five signatures fit the compute layout. On the unpatched optional path the fallback address lost its high word (`0x11223344` became zero), and the 17- and 24-table lists failed to close. All five cases passed in each patched mode. The existing bundle suite also passed 14 scenarios in default mode.

## Pixel readback

The companion diff adapts the branch's hand-written M1.5 translated-draw fixture for this experiment. In a separate test checkout with the runtime patch applied, apply the fixture diff, build its target and run the test with the optional layout enabled. Use a separate build directory or rebuild the translated shaders when switching between fixtures.

```powershell
git apply /path/to/compact-draw.patch
cmake --build runtime/build --config Release --target d3d12_translated_draw_test
$env:LEONIDA_GRAPHICS_STAGE_LAYOUT = '1'
ctest --test-dir runtime/build -C Release -R '^d3d12_translated_draw$' --output-on-failure
```

The fixture declares 17 tables and four constants. The vertex shader fetches from the first table at s8 and reads its offsets at s25 and s26. The pixel shader reads the bias at s27. The three draws go through the device proxy and Vulkan queue to a 64 × 64 float target. On the original branch the list failed to close before submission. On the patched branch every pixel matched its expected RGBA value, including all 32 diagonal pixels that retained the clear color. Reapplying the fixture reproduced both SPIR-V modules byte for byte and the pixel check passed again. [Fixture diff](compact-draw.patch)

The measured runs used cross-compiled Windows executables under Wine on an RTX 4070 Ti. Local LLVM-MinGW build inputs selected the shim's C++ structure-return declarations, supplied the SDK interface GUIDs and a missing driver-query declaration, and preincluded the standard algorithm header for the Xbox device. The published runtime patch contains the layout changes and command-recording tests. Native MSVC and retail startup were untested. The runtime changes and fixture adaptation were developed with Codex from the branch's root packers, command recorder, bundle tests and translated-draw fixture.
