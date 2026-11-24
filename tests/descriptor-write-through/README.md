# CPU copies of API-written descriptors on Wine

On Mumogg's frame-loop branch, a game can create a constant-buffer view through the device proxy, copy that descriptor with the CPU, then bind the copy for a shader. The real Vulkan heap held the new descriptor, but the game's backing memory still contained zeros. A previous test supplied the bytes directly to the backing and passed on the same build. This test follows both directions of the copy.

The proxy checked both addresses with VirtualQuery before writing through. On this Wine and NVIDIA combination, that check can report MEM_FREE for a live Vulkan arena mapping. At heap creation the Vulkan implementation now confirms the exact base and size of its mapped allocation. The proxy holds the heap registration during the copy, bounds the copy by the remaining descriptor count and checks the game's destination for write access. Other backends keep their existing memory query.

## Reproduce

Start with Mumogg's frame-loop PR at `e0d4fd2b81e6636b1c6bb2f76be79e910e84d84b`. Apply the two prerequisite descriptor-refresh commits followed by this mailbox. A clean application produced tree `034ddb87d04eec61914bcb80c8c99cb87df34bf8`. The fixture uses the existing synthetic compute shader. [Upstream integration](https://github.com/ubiquitarianism/leonida/pull/321) · [Prerequisite patch](../../descriptor-table-refresh.mbox) · [Write-through patch](../../descriptor-write-through.mbox)

```sh
git am /path/to/descriptor-table-refresh.mbox /path/to/descriptor-write-through.mbox
cmake --build runtime/build --config Release --target d3d12_title_va_test
ctest --test-dir runtime/build -C Release -R '^d3d12_(api_descriptor_copy(_off)?|rebound_second_device|title_va)$' --output-on-failure
```

After closing the command list, the fixture creates a CBV through the proxy in an unbound shader-visible slot. It copies those bytes from the game's CPU backing into the bound table with memcpy. After the first fence, CopyDescriptorsSimple changes the unbound slot to a different CBV and the fixture repeats the CPU copy and the submission. The shader reads a different pattern each time. A separate CPU-only staging heap supplies a 32-byte reference for both operations.

| Build | Real slot matched staging | Game's slot matched staging | GPU words matched on first and second submissions |
| --- | --- | --- | --- |
| Descriptor-refresh prerequisite only | Both | Neither | 0/64 and 0/64 |
| With write-through patch | Both | Both | 64/64 and 64/64 |
| Patched with mirror disabled | Both | Neither | 64/64 and 64/64 expected zeros |

The fixture also protected the game's backing as read-only for an initial API write and verified that the slot stayed zero. The previous two-device readback and the existing title-address test passed. The two unchanged proxy mock cases for heap hooks and list CPU handles passed when run separately. The full mock exited with status 5 at device slot 26 on both the original and patched cross-builds.

Tested under Wine on an RTX 4070 Ti with the existing LLVM-MinGW header and GUID adaptations. Native MSVC and retail frame output remain unverified. Developed with Codex on Mumogg's title-backed mirror and Etr0p's submission scanner. [Cross-build details](../graphics-root-layout/README.md)
