# Refreshing game-owned sampler tables before submission

A 64×64 textured draw kept using its original wrap and clamp samplers after the game swapped their descriptors in CPU memory. The command recorder assigned sampler tables a refresh width of zero, and the queue's heap registry contained only CBV/SRV/UAV heaps. Both conditions excluded samplers from submission-time copies.

The patch records the declared span of a sampler table, looks up the heap on the submitting device and copies game-written slots before the draw executes. Buffer address translation applies only to buffer-view heaps. Sampler tables cover up to the full 2,048-slot shader-visible sampler heap. The copy is bounded by both the declared range and the remaining heap size.

## GPU results

The M2 shader reads the sampler words from a root table. Its texture coordinates extend past the edge, making wrap and clamp produce different pixels. The test closes one list, swaps two previously registered sampler descriptors with CPU stores, submits the list, restores the descriptors and submits the same list again. Each readback follows a fence. The fixture also changes a slot outside the bounded tables and checks the last slot of an unbounded table directly in the mapped Vulkan heap.

| Source build and table | Swapped samplers | Restored samplers | Range check |
| --- | ---: | ---: | --- |
| Before, one-slot tables | 512/4,096 pixels | 4,096/4,096 | Adjacent slot unchanged |
| After, one-slot tables | 4,096/4,096 | 4,096/4,096 | Adjacent slot unchanged |
| Before, unbounded | 512/4,096 | 4,096/4,096 | Slot 2,047 stayed stale |
| After, unbounded | 4,096/4,096 | 4,096/4,096 | Slot 2,047 matched both updates |

With the mirror disabled, the GPU kept the original sampler order on both submissions and matched all 4,096 expected pixels each time. The ordinary textured draw passed, including its linear-filter comparison. Buffer descriptor readback still matched 64/64 twice for both an API-written descriptor and the two-device case.

## Reproduce

Start from Mumogg's frame-loop branch at `e0d4fd2b81e6636b1c6bb2f76be79e910e84d84b`. Apply the descriptor refresh prerequisites and then the sampler patch. The independent clean application produced tree `5c1d8c3e177e2d1ed09c06cfb6e94c7cea98b812` and passed the unbounded GPU case. The patch also works with the previously published descriptor write-through change applied between the two mailboxes. [Frame-loop PR](https://github.com/ubiquitarianism/leonida/pull/321) · [Descriptor refresh prerequisite](../../descriptor-table-refresh.mbox) · [Sampler patch](../../sampler-table-refresh.mbox)

```sh
git am /path/to/descriptor-table-refresh.mbox /path/to/sampler-table-refresh.mbox
cmake --build runtime/build --config Release --target d3d12_m2_textured_test
ctest --test-dir runtime/build -C Release -R '^d3d12_m2_(textured|(unbounded_)?sampler_refresh(_off)?)$' --output-on-failure
```

Tested with source-built Windows executables under Wine on an RTX 4070 Ti. All four synthetic M2 shader modules passed spirv-val. The fixture exercises sampler encodings already registered by CreateSampler. Retail output, newly synthesized sampler encodings and a native MSVC run remain unverified.

Developed with Codex on Mumogg's heap mirror and Etr0p's table scanner, using AlxcBTW's M2 sampler control and Vonsek's indexed texture fixture. [M2 sampler control](https://github.com/ubiquitarianism/leonida/commit/e4f10932) · [Indexed M2 test](https://github.com/ubiquitarianism/leonida/commit/89a080b2) · [Cross-build details](../graphics-root-layout/README.md)
