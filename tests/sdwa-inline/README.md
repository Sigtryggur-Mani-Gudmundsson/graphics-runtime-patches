# Shader decoder comparison on an RTX 4070 Ti

Leonida's ps_sdwa_f16_const and cs_w32_permlane_fi fixtures each have an independent expected result in their assembly source. This script assembles both, disassembles their bytes with the selected backend, and passes real code ranges and synthetic sidecars to the translator. It validates the resulting SPIR-V and reads the results back from Vulkan.

| Decoder | Original pixel shader | Patched pixel shader | Patched compute output |
| --- | --- | --- | --- |
| LLVM 19.1.7 | Translator rejected the instruction at offset 0x190 | 4,096 of 4,096 pixels matched | All eight words matched |
| LLVM 23.1.5 | — | 4,096 of 4,096 pixels matched | All eight words matched |
| Leonida Python decoder | — | 4,096 of 4,096 pixels matched | All eight words matched |

The original LLVM 19 compute control also matched. The four pixel channels, read as raw 32-bit words, were `aaaa3800 aaaa1c00 aaaa3e00 aaaa73f0`. The lane-control shader returned `1000 0 4 1 1000 0 52 49` in decimal. These results were reproduced after applying the mailbox to the clean base. All measurements used synthetic shaders.

LLVM 19's disassembler prints the eight-byte unsigned conversion with native source `1.0` as a twelve-byte instruction with literal `0x3c00`. That changes the selected high halfword and displaces the next instruction in the listing. The same issue occurs with the signed conversion. The translator now recovers the source and byte length when the native opcode, selector, other fields and printed literal agree. The tests exercise consecutive conversions, offsets after them, and inputs with disassembler warnings. They also check that a changed literal or selection field still fails validation.

A separate commit makes the Python decoder print the two lane-control bits for ordinary permlane instructions and gives `exp null` a target operand in the text parser. Both LLVM 19 and 23 print the two-bit form. On this fixture, both spellings produced the same GPU words.

## Reproduce on Linux

From this directory, set SOURCE to the patched checkout and BUILD to the directory with vk_harness and vk_compute_test. The test also needs glslangValidator, spirv-as and spirv-val. The Python disassembler run uses llvm-mc to assemble the input shader.

```sh
SOURCE=/absolute/path/to/Leonida
BUILD=/absolute/path/to/native-build
LLVM_MC=/path/to/llvm19/bin/llvm-mc python3 -I check.py "$SOURCE" "$BUILD"
LLVM_MC=/path/to/llvm23/bin/llvm-mc python3 -I check.py "$SOURCE" "$BUILD"
python3 -I check.py "$SOURCE" "$BUILD" --backend python
```

Apply [sdwa-inline-recovery.mbox](../../sdwa-inline-recovery.mbox) with `git am` on the base commit `de4c373`. The standalone tree is `1181912094870a2018f14070cdadf01696e2f8a7`.
