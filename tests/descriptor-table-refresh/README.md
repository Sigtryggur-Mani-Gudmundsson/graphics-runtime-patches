# Title-backed descriptor tables at submission

This two-commit mailbox targets Mumogg's frame-loop branch at `e0d4fd2b81e6636b1c6bb2f76be79e910e84d84b`. The branch has a submission-time scanner for shader-visible buffer descriptors. Consecutive bindings at the same GPU address kept the width of the first binding, and the Xbox proxy never registered a provider for the scanner to read title-owned descriptor backing. The scanner also routed every queue through the first compatibility device, although the same GPU heap offsets can identify different memory in each device. [Frame-loop integration PR](https://github.com/ubiquitarianism/leonida/pull/321)

The first commit retains the widest bound range and connects the existing backing registry to Etr0p's submission-time descriptor-source hook. The copy stays within both the declared scan extent and the live registered heap. The queue supplies a writable mapping returned by Vulkan. In the tested Wine build, VirtualQuery reported that mapping as MEM_FREE even as the GPU and CPU successfully exchanged data through it. The proxy continues checking title-side memory and the existing bind-time path continues checking its destination. The second commit makes each queue use the scanner for its own Vulkan context. The existing mirror convention treats an entirely zero title slot as unwritten.

## Reproduce

Apply the mailbox to a clean checkout of the base above. A clean replay produced tree `a3987159d6374e07dfedbaf055830518d147aa3a`. The tests use the branch's existing Windows runtime build, synthetic compute shader and a Vulkan 1.3 GPU.

```powershell
git am /path/to/descriptor-table-refresh.mbox
cmake --build runtime/build --config Release --target d3d12_title_va_test d3d12_compute_test
ctest --test-dir runtime/build -C Release -R '^d3d12_(rebound_(tables|tables_off|second_device)|title_va|compute)$' --output-on-failure
```

The regression binds a one-slot graphics table, a 65-slot compute table, then the one-slot table again, all at the same interior GPU handle. After closing the command list it writes a valid constant-buffer descriptor to slot 64 in the title's backing, submits, waits, changes that descriptor, and submits the same closed list again. The shader reads a different 64-word pattern on each pass. Two written slots immediately outside the declared range remain untouched in the real heap.

| Build or control | First readback | Second readback |
| --- | ---: | ---: |
| Original branch | 0/64 matched | 0/64 matched |
| Retain the widest binding only | 0/64 matched | 0/64 matched |
| Register the descriptor source only | 0/64 matched | 0/64 matched |
| Both changes, one compatibility device | 64/64 matched | 64/64 matched |
| Both changes, second of two live devices and original scanner routing | 0/64 matched | 0/64 matched |
| Final series, second of two live devices | 64/64 matched | 64/64 matched |
| Final series, mirror disabled | 64/64 expected zeros | 64/64 expected zeros |

The Windows executables ran under Wine on an RTX 4070 Ti. The existing compute regression passed. The original title-address regression passed on the baseline and final builds, including its eight negative controls. Linux CTest also passed command_list, descriptor_table and title_va_alias. Windows compilation used local LLVM-MinGW header and GUID adaptations described in the previous test report. Retail frames and native MSVC remain unverified. [Cross-build details](../graphics-root-layout/README.md)

## Combining the frame-loop patches

From the same base, use the mailbox order below. The descriptor recorder has adjacent edits in two mailboxes, so use Git's three-way application. The clean combined tree was `746f6586fa98eebc256ce7082092bed438b48322`.

```sh
git am --3way /path/to/boot-rectangle-corners.mbox /path/to/graphics-root-layout.mbox /path/to/descriptor-table-refresh.mbox
```

The combined build passed both root-layout modes and the two-device descriptor readback. The rectangle test covered all 4,096 pixels for each of the three right-angle positions, and the quad matched all 4,096 pixels exactly.

Developed with Codex using Mumogg's title-backed heap mirror and frame-loop integration, Etr0p's descriptor-source hook and table scanner, and the project's existing D3D12 compute fixtures. [Descriptor-source hook commit](https://github.com/ubiquitarianism/leonida/commit/f9db5e6)
