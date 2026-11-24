"""Compare two vertex layouts on the same mesh using the repository's native Vulkan test harness."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


def words(*values):
    return struct.pack(f"<{len(values)}I", *values)


def container(position_register, program):
    compressed = zlib.compress(program)
    xbch = b"XBCH" + words(1, len(compressed), 0, len(program), 0) + compressed
    dxil = b"XDAT" + words(1, 12, 13, 24, len(xbch)) + xbch
    rows, names = bytearray(), bytearray()
    for name, register, mask in [(b"POSITION", position_register, 3), (b"COLOR", 1 - position_register, 15)]:
        rows += words(0, 72 + len(names), 0, 0, 3, register, mask | (mask << 8), 0)
        names += name + b"\0"
    signature = words(2, 8) + rows + names
    parts = b"ISG1" + words(len(signature)) + signature + b"DXIL" + words(len(dxil)) + dxil
    return b"DXBC" + bytes(16) + words(1, 40 + len(parts), 2, 40, 48 + len(signature)) + parts


def run(repo, harness, tmp):
    sys.path.insert(0, str(repo / "tools"))
    import shader_meta

    program = b"FSSM synthetic vertex program"
    index = {}
    for register in (0, 1):
        name = f"variant_{register}"
        data = container(register, program)
        index[name] = dict(shader_meta.read_dxbc_meta(data), stage="vs", fssm_sha256=hashlib.sha256(program).hexdigest())
        (tmp / (name + ".dxbc")).write_bytes(data)
    (tmp / "index.json").write_text(json.dumps(index), encoding="utf-8")
    vertices = [(-1, -1), (3, -1), (-1, 3)]
    mesh = words(3, 3) + b"".join(struct.pack("<6f", x, y, 0, 1, 0, 1) for x, y in vertices) + words(0, 1, 2)
    (tmp / "mesh.bin").write_bytes(mesh)

    shaders = {
        "vert": """#version 450
layout(location=1) in vec2 pos;
layout(location=0) in vec4 color;
layout(location=0) out vec4 shaded;
void main() { gl_Position=vec4(pos,0,1); shaded=color; }
""",
        "frag": """#version 450
layout(location=0) in vec4 shaded;
layout(location=0) out vec4 out_color;
void main() { out_color=shaded; }
""",
    }
    for stage, source in shaders.items():
        path = tmp / ("color." + stage)
        path.write_text(source, encoding="utf-8")
        subprocess.run(["glslangValidator", "-V", path, "-o", tmp / (stage + ".spv")], check=True)
    selector = tmp / "select"
    subprocess.run(["c++", "-std=c++20", "-I", repo / "runtime/src", Path(__file__).with_name("select.cpp"),
                    "-o", selector], check=True)
    for mode, expected_green in [("without-signature", 0), ("with-signature", 4096)]:
        layout = subprocess.check_output([selector, tmp, mode], text=True)
        env = dict(os.environ, VKH_VERTEX_LAYOUT=layout, VKH_DRAW_MESH=str(tmp / "mesh.bin"))
        raw = tmp / "pixels.raw"
        subprocess.run([harness, tmp / "vert.spv", tmp / "frag.spv", raw, "64", "64"], check=True, env=env)
        pixels = list(struct.iter_unpack("<4f", raw.read_bytes()))
        green = pixels.count((0.0, 1.0, 0.0, 1.0))
        print(f"{mode}  {layout}  {green}/{len(pixels)} green pixels")
        if len(pixels) != 4096 or green != expected_green:
            raise SystemExit("unexpected vertex fetch result")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo", type=Path, help="patched Leonida source checkout")
    parser.add_argument("harness", type=Path, help="built vk_harness executable")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as scratch:
        run(args.repo.resolve(), args.harness.resolve(), Path(scratch))
