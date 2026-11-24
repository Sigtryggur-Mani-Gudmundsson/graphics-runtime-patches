"""Translate the project's two reference shaders with real native sidecars and compare GPU readbacks."""
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path)
    parser.add_argument("build", type=Path, help="directory containing vk_compute_test and vk_harness")
    parser.add_argument("--backend", choices=("llvm-mc", "python"), default="llvm-mc")
    args = parser.parse_args()
    root = args.checkout.resolve()
    sys.path.insert(0, str(root / "tests/semantic"))
    from support import STUB_DXBC, rdna2spirv
    import shader_disasm

    harness = args.build.resolve()
    expected_cs = (1000, 0, 4, 1, 1000, 0, 52, 49)
    expected_rgba = (0xaaaa3800, 0xaaaa1c00, 0xaaaa3e00, 0xaaaa73f0)
    errors = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "disasm").mkdir()
        (tmp / "fssm").mkdir()
        for name in ("cs_w32_permlane_fi", "ps_sdwa_f16_const"):
            src = root / "runtime/test/shaders" / (name + ".asm")
            cmd = shader_disasm.MC + ["-triple=amdgcn--amdpal", "-mcpu=gfx1013", "-show-encoding"]
            text = subprocess.check_output(cmd, input=src.read_text(), text=True)
            machine = b"".join(bytes(int(x, 16) for x in group.split(","))
                               for group in re.findall(r"; encoding: \[([^\]]+)\]", text))
            if not machine.endswith(bytes.fromhex("000081bf")):
                raise ValueError("assembled shader has no terminal s_endpgm")
            asm, bad, diagnostics = shader_disasm.disasm(machine, "gfx1013", backend=args.backend)
            start = 0x148
            header = bytearray(start)
            struct.pack_into("<I", header, 0xd4, 0x20)
            struct.pack_into("<4I", header, 0x138, 0x88, 32, 1, 1)
            (tmp / "fssm" / (name + ".fssm")).write_bytes(header + machine)
            (tmp / (name + ".dxbc")).write_bytes(STUB_DXBC)
            path = tmp / "disasm" / (name + ".s")
            listing = f"; {name}  code 0x{start:x}..0x{start+len(machine):x} (scan), gfx1013, {bad} invalid words\n" + asm
            if diagnostics.strip():
                listing += "\n; --- llvm-mc messages ---\n; " + diagnostics.strip().replace("\n", "\n; ") + "\n"
            path.write_text(listing)
            try:
                spirv = rdna2spirv.translate(str(path))
            except rdna2spirv.Unsupported as exc:
                print(f"{name}: refused: {exc}", flush=True)
                errors += 1
                continue
            (tmp / "shader.asm").write_text(spirv)
            subprocess.run(["spirv-as", "--target-env", "vulkan1.3", "shader.asm", "-o", "shader.spv"], cwd=tmp, check=True)
            subprocess.run(["spirv-val", "--target-env", "vulkan1.3", "shader.spv"], cwd=tmp, check=True)
            env = {k: v for k, v in os.environ.items() if not k.startswith(("VKC_", "VKH_"))}
            if name.startswith("cs_"):
                env["VKC_DUMP"] = str(tmp / "compute.raw")
                subprocess.run([harness / "vk_compute_test", "shader.spv", "1", "dump"], cwd=tmp, env=env, check=True, timeout=60)
                actual = struct.unpack_from("<8I", (tmp / "compute.raw").read_bytes())
                print(f"{name}: {actual}", flush=True)
                errors += actual != expected_cs
            else:
                subprocess.run(["glslangValidator", "-V", root / "runtime/test/glsl/vs_fullscreen.vert", "-o", "vs.spv"],
                               cwd=tmp, check=True)
                subprocess.run([harness / "vk_harness", "vs.spv", "shader.spv", "rgba.raw", "64", "64"],
                               cwd=tmp, env=env, check=True, timeout=60)
                data = (tmp / "rgba.raw").read_bytes()
                if len(data) != 64 * 64 * 16:
                    raise ValueError("unexpected render target size")
                matches = sum(pixel == expected_rgba for pixel in struct.iter_unpack("<4I", data))
                print(f"{name}: {matches} of 4096 pixels matched {tuple(map(hex, expected_rgba))}", flush=True)
                errors += matches != 4096
    return int(bool(errors))


if __name__ == "__main__":
    sys.exit(main())
