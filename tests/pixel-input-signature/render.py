"""Translate one synthetic RDNA2 pixel program with two signatures and verify both draws on Vulkan."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import zlib


def words(*values):
    return struct.pack(f"<{len(values)}I", *values)


def pixel_program():
    source = """v_mov_b32_e32 v0, 0
v_mov_b32_e32 v1, 1.0
exp mrt0 v2, v0, v0, v1 done vm
s_endpgm
"""
    listing = subprocess.run(["llvm-mc", "-triple=amdgcn--amdpal", "-mcpu=gfx1030", "-show-encoding"],
                             input=source, capture_output=True, text=True, check=True).stdout
    groups = re.findall(r"encoding: \[([^\]]+)\]", listing)
    code = bytes(int(byte, 16) for group in groups for byte in group.split(","))
    if len(code) != 20:
        raise ValueError("unexpected instruction encoding length")
    header = bytearray(0x148)
    header[:4] = b"FSSM"
    struct.pack_into("<I", header, 0xc0, 0x120)  # instructions start 0x28 bytes into this section
    struct.pack_into("<I", header, 0xc8, 0x28 + len(code))
    struct.pack_into("<I", header, 0xd4, 0x20)  # wave32
    return bytes(header) + code


def run(repo, harness, tmp):
    sys.path.insert(0, str(repo / "tests"))
    from test_shader_meta import isg1_chunk, make_dxbc

    program = pixel_program()
    compressed = zlib.compress(program)
    blob = b"XBCH" + words(1, len(compressed), 0, len(program), 0) + compressed
    dxil = b"XDAT" + words(1, 12, 13, 24, len(blob)) + blob
    depth, reciprocal = "ps_a_depth", "ps_b_reciprocal_w"
    for name, read_mask in [(depth, 4), (reciprocal, 8)]:
        signature = isg1_chunk([("SV_Position", 0, 0, 15, read_mask, 1, 3)])
        (tmp / (name + ".dxbc")).write_bytes(make_dxbc([signature, (b"DXIL", dxil)]))
    library = tmp / "spirv"
    for tool, args in [
        ("fssm_unpack.py", [tmp]),
        ("shader_disasm.py", [tmp / "fssm", "--mcpu", "gfx1030", "--out", tmp / "disasm"]),
        ("rdna2spirv.py", [tmp / "disasm", "--out", library]),
    ]:
        subprocess.run([sys.executable, "-I", repo / "tools" / tool, *args], check=True)
    index = library / "index.json"
    entries = json.loads(index.read_text(encoding="utf-8"))
    if (set(entries) != {depth, reciprocal} or entries[depth]["fssm_sha256"] != entries[reciprocal]["fssm_sha256"]
            or entries[depth]["ps_system_inputs"] != ["2"] or entries[reciprocal]["ps_system_inputs"] != ["3"]):
        raise ValueError("unexpected translated shader metadata")

    vertex = tmp / "fullscreen.vert"
    vertex.write_text("""#version 450
void main() {
    const vec2 p[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
    gl_Position = vec4(p[gl_VertexIndex], 0, 1);
}
""", encoding="utf-8")
    subprocess.run(["glslangValidator", "-V", vertex, "-o", tmp / "vertex.spv"], check=True)
    selector = tmp / "select"
    subprocess.run(["c++", "-std=c++20", "-I", repo / "runtime/src", Path(__file__).with_name("select.cpp"),
                    "-o", selector], check=True)
    for first, requested in [(depth, reciprocal), (reciprocal, depth)]:
        index.write_text(json.dumps({name: entries[name] for name in (first, requested)}), encoding="utf-8")
        for mode, expected in [("hash-only", first), ("with-signature", requested)]:
            chosen = subprocess.check_output([selector, library, tmp / (requested + ".dxbc"),
                                              tmp / "pixel.spv", mode], text=True).strip()
            if chosen != expected:
                raise ValueError(f"selected {chosen}, expected {expected}")
            raw = tmp / "frame.raw"
            subprocess.run([harness, tmp / "vertex.spv", tmp / "pixel.spv", raw, "64", "64"], check=True)
            color = (float(expected == reciprocal), 0.0, 0.0, 1.0)
            pixels = list(struct.iter_unpack("<4f", raw.read_bytes()))
            matching = pixels.count(color)
            print(f"{mode} requested {requested}, selected {chosen}, {matching}/{len(pixels)} pixels match {color}")
            if len(pixels) != 4096 or matching != 4096:
                raise SystemExit("unexpected pixel readback")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo", type=Path, help="patched Leonida source checkout")
    parser.add_argument("harness", type=Path, help="built vk_harness executable")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as scratch:
        run(args.repo.resolve(), args.harness.resolve(), Path(scratch))
