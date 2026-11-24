# Keep immediate geometry uploads with their graphics device

Mumogg’s frame-loop integration can copy CPU-backed vertex and index data into a 16 MiB region of the Vulkan device arena. The callback reserves storage from one device, but its function-static base, cursor and first-attempt flag survive device destruction. The patch stores them and their mutex in the arena that owns the allocation. The reservation stays lazy. [Original implementation](https://github.com/Mumogg/leonida-continued/commit/237f9e0ba83c8b8e740dd7bcc0e10d311fddb897) · [Integration PR](https://github.com/ubiquitarianism/leonida/pull/321)

## GPU verification

The fixture uses four CPU-side vertices and six CPU-side indices to draw a red quadrant over a blue background. It waits for the GPU and checks an 8 × 8 readback grid. In the recreation run it releases the first Vulkan device, creates a second in the same process and repeats the draw. The verifier checks the actual arena allocation for each device as well as the successful readback.

| Build on integration head e0d4fd2b8 | Devices rendered | 16 MiB reservations | Readbacks |
| --- | ---: | ---: | --- |
| Original, single-device control | 1 | 1 | Expected red quadrant |
| Patched, device recreated | 2 | 2 | Expected red quadrant on both |

The fixture builds on the integration branch’s existing bundle-test graphics rig and hand-authored shaders. The measured runs used LLVM-MinGW, Wine 11.22 and an RTX 4070 Ti. Both linked the same companion shim objects and local DirectX SDK adaptations. The baseline ran only the single-device control. The retail game and native Windows were outside this test.

## Applying and repeating

Apply `device-ring.mbox` with `git am` on `e0d4fd2b81e6636b1c6bb2f76be79e910e84d84b`. Build `geometry-ring-test.cpp` in place of the source of the upstream `d3d12_bundle_test` target, with `runtime/test` on the include path. Keep that target's existing shader build definitions and linked dependencies. Set `LEONIDA_GRAPHICS_STAGE_LAYOUT=0` for the bundle fixture's ordinary root layout. The no-argument executable draws twice and the optional `--single` selects the control. Run the verifier from a shell where the fixture can create its temporary shader directory.

```sh
python check-ring.py wine ./geometry-ring-test.exe
```

On Windows, omit `wine`. The verifier returns success only if both devices draw correctly and each reserves exactly one 16 MiB shader-arena range before release.
