# VCC register tuples with LLVM 19 and 23

The fixture builds six small gfx1013 compute programs from synthetic inputs. It runs each native byte stream through the selected llvm-mc and passes the resulting listing, diagnostics and FSPX sidecars to the production translator. It assembles and validates each accepted module for Vulkan 1.2. Five programs run on the GPU with a single work item and known heap contents. The image program checks the translated descriptor lookup and SPIR-V validity. It has no bound texture in this test.

| Disassembler | Translator | SPIR-V modules validated | GPU readbacks matching expected words |
| --- | --- | ---: | ---: |
| LLVM 19.1.7 | PR 327 as submitted | 0 of 6 | No dispatch |
| LLVM 19.1.7 | PR plus successor recovery | 6 of 6 | 5 of 5 |
| LLVM 23.1.5 | PR 327 as submitted | 6 of 6 | 5 of 5 |
| LLVM 23.1.5 | PR plus successor recovery | 6 of 6 | 5 of 5 |

These runs used Linux and an NVIDIA GeForce RTX 4070 Ti. LLVM 19 emitted one to three invalid-instruction warnings per program. When it rejects the first dword of a scalar load, it can print the second dword as an export and consume the first dword of the following instruction. The PR as submitted refused all six real LLVM 19 listings with a fall-through into undecodable words. LLVM 23 printed explicit lists ending in vcc_lo and vcc_hi and gave zero warnings on these fixtures.

The fix preserves both dwords of a literal move following a recovered scalar load. A separate regression checks that a variable-length image instruction ends within the declared code range even when additional bytes follow it in the blob. Retail shader counts have not been remeasured. [tedin7’s original PR](https://github.com/ubiquitarianism/leonida/pull/327)

| GPU program | Expected 32-bit output words, hex |
| --- | --- |
| 4-register load | 11110000, 11110001, 11110002, 11110003 |
| 8-register load | 11110000, 11110005, 11110006, 11110007 |
| 16-register load | 11110000, 1111000d, 1111000e, 1111000f |
| Nonzero load offset followed by a literal move | 11110001, 1111000e, 1111000f, 11110010 |
| Descriptor shared by scalar and vector loads | cafe1234, beef5678 |

## Reproduce

Set SOURCE to the checkout to test and HARNESS to a built vk_compute_test. Install spirv-as, spirv-val and an llvm-mc build with the AMDGPU target. Run from the directory containing compare.py.

```sh
SOURCE=/absolute/path/to/Leonida
HARNESS=/absolute/path/to/vk_compute_test
LLVM_MC=/path/to/llvm19/bin/llvm-mc python3 -I compare.py "$SOURCE" "$HARNESS"
LLVM_MC=/path/to/llvm23/bin/llvm-mc python3 -I compare.py "$SOURCE" "$HARNESS"
```

The [mailbox](../../vcc-tuple-recovery.mbox) contains tedin7’s original commit followed by the correction. On a checkout that already contains PR 327, apply just the second message.

```sh
MAILDIR=$(mktemp -d)
git mailsplit -o"$MAILDIR" /path/to/vcc-tuple-recovery.mbox
git am "$MAILDIR/0002"
```
