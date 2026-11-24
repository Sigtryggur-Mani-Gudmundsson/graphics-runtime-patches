"""Run synthetic FSPX programs through the chosen llvm-mc, translator and Vulkan device."""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("source", type=Path, help="Leonida checkout")
    ap.add_argument("harness", type=Path, help="built vk_compute_test")
    args = ap.parse_args()
    sys.path.insert(0, str(args.source.resolve() / "tools"))
    import rdna2spirv
    import shader_disasm

    harness = args.harness.resolve()
    cpu = "gfx1013"
    compiler = shader_disasm.MC
    print(subprocess.check_output(compiler + ["--version"], text=True).splitlines()[1].strip())

    def assemble(lines):
        output = subprocess.check_output(compiler + ["-triple=" + shader_disasm.TRIPLE, "-mcpu=" + cpu,
                                                    "-show-encoding"], input=lines, text=True)
        encodings = re.findall(r"; encoding: \[([^\]]+)\]", output)
        if len(encodings) != len(lines.splitlines()):
            raise RuntimeError("llvm-mc did not assemble every input line")
        return b"".join(bytes(int(b, 16) for b in group.split(",")) for group in encodings)

    # Build ordinary instructions with llvm-mc. The explicitly encoded tuple fields are
    # intentional so that both LLVM versions receive identical native instructions.
    prep = assemble("s_mov_b32 s4, 0x1000\ns_mov_b32 s5, 0\n")
    wait = assemble("s_waitcnt lgkmcnt(0)\n")
    end = assemble("s_endpgm\n")
    values = [0x11110000 + i for i in range(32)]
    initial = bytearray(0x3010)
    struct.pack_into("<32I", initial, 0x1000, *values)

    def store_four(regs):
        moves = "".join(f"v_mov_b32_e32 v{10+i}, {reg}\n" for i, reg in enumerate(regs))
        return assemble(moves + "v_mov_b32_e32 v6, 0x3000\nglobal_store_dwordx4 v6, v[10:13], s[0:1]\n")

    cases = []
    for n, raw in ((4, "021a08f4"), (8, "02190cf4"), (16, "021710f4")):
        start = 108 - n
        packet = bytes.fromhex(raw + "000000fa")
        regs = [f"s{start}", "s105", "vcc_lo", "vcc_hi"]
        cases.append((f"load_x{n}", prep + packet + wait + store_four(regs) + end,
                      initial, (values[0], values[n-3], values[n-2], values[n-1])))

    # Here the next real instruction is eight bytes. LLVM 19 consumes its first dword as
    # an export operand. Dropping that move changes the observable store address.
    packet = bytes.fromhex("021710f4 040000fa")
    store_address = assemble("v_mov_b32_e32 v6, 0x3000\n")
    store = assemble("v_mov_b32_e32 v10, s92\nv_mov_b32_e32 v11, s105\nv_mov_b32_e32 v12, vcc_lo\n"
                     "v_mov_b32_e32 v13, vcc_hi\nglobal_store_dwordx4 v6, v[10:13], s[0:1]\n")
    cases.append(("offset_and_literal", prep + packet + store_address + wait + store + end,
                  initial, (values[1], values[14], values[15], values[16])))

    # A descriptor loaded into s104..vcc_hi is then used by both scalar and vector memory instructions.
    descriptors = bytearray(initial)
    struct.pack_into("<4I", descriptors, 0x1000, 0x1100, 0, 16, 0x30000000)
    struct.pack_into("<2I", descriptors, 0x1100, 0xcafe1234, 0xbeef5678)
    header = assemble("s_mov_b32 s6, 32\ns_mov_b32 s7, 0x30000000\n")
    scalar = bytes.fromhex("021a28f4 000000fa") + wait + bytes.fromhex("b40220f4 000000fa") + wait
    vector = assemble("v_mov_b32_e32 v1, 4\n") + bytes.fromhex("001030e0 010b1a80")
    store_pair = assemble("s_waitcnt vmcnt(0)\nv_mov_b32_e32 v10, s10\nv_mov_b32_e32 v6, 0x3000\n"
                          "global_store_dwordx2 v6, v[10:11], s[0:1]\n")
    cases.append(("descriptor", prep + header + scalar + vector + store_pair + end,
                  descriptors, (0xcafe1234, 0xbeef5678)))

    image = bytes.fromhex("02192cf4 000000fa") + wait
    image += assemble("v_mov_b32_e32 v1, 0\nv_mov_b32_e32 v2, 0\n")
    image += bytes.fromhex("081f00f0 01041900")
    image += assemble("s_waitcnt vmcnt(0)\nv_mov_b32_e32 v6, 0x3000\nglobal_store_dword v6, v4, s[0:1]\n")
    cases.append(("image", prep + header + image + end, None, None))

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "fspx").mkdir()
        (root / "disasm").mkdir()
        for name, code, heap, expected in cases:
            name = "cs_" + name
            asm, bad, err = shader_disasm.disasm(code, cpu)
            if not asm.strip():
                raise RuntimeError(err or "no llvm-mc output")
            listing = f"; {name}  code 0x0..0x{len(code):x} (fspx), {cpu}, {bad} invalid words\n" + asm
            if err.strip():
                listing += "\n; --- llvm-mc messages ---\n; " + err.strip().replace("\n", "\n; ") + "\n"
            path = root / "disasm" / (name + ".s")
            path.write_text(listing)
            (root / "fspx" / (name + ".code")).write_bytes(code)
            (root / "fspx" / (name + ".json")).write_text(json.dumps(
                {"rsrc2": 0x88, "threads": [1, 1, 1], "wave32": True}))
            try:
                spirv = rdna2spirv.translate(str(path))
            except rdna2spirv.Unsupported as exc:
                print(f"{name}: {bad} disassembler warnings, refused: {exc}", flush=True)
                failures += 1
                continue
            (root / "shader.asm").write_text(spirv)
            subprocess.run(["spirv-as", "--target-env", "vulkan1.2", "shader.asm", "-o", "shader.spv"],
                           cwd=root, check=True)
            subprocess.run(["spirv-val", "--target-env", "vulkan1.2", "shader.spv"], cwd=root, check=True)
            if expected is None:
                if "OpFunctionCall %uint %desc_lookup_fn" not in spirv:
                    raise RuntimeError("missing image descriptor lookup")
                print(f"{name}: {bad} disassembler warnings, image lookup translated and validated", flush=True)
                continue
            (root / "init").write_bytes(heap)
            dump = root / "out"
            dump.unlink(missing_ok=True)
            env = {k: v for k, v in os.environ.items() if not k.startswith("VKC_")}
            env.update(VKC_HEAP_INIT=str(root / "init"), VKC_OUT_WORDS=str(len(heap) // 4), VKC_DUMP=str(dump))
            subprocess.run([str(harness), "shader.spv", "1", "dump"], cwd=root, env=env, check=True, timeout=60)
            actual = struct.unpack_from(f"<{len(expected)}I", dump.read_bytes(), 0x3000)
            print(f"{name}: {bad} disassembler warnings, read back {[hex(x) for x in actual]}", flush=True)
            if actual != expected:
                print("expected", [hex(x) for x in expected])
                failures += 1
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
