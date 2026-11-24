"""Compare each ordered triple of rectangle corners with an independent pixel reference."""
import argparse
import hashlib
import itertools
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

WIDTH, HEIGHT = 64, 48
# Label D3D clip-space corners. The native harness uses a positive-height viewport.
POSITIONS = {"TL": (-1, 1), "BL": (-1, -1), "BR": (1, -1), "TR": (1, 1)}
CLEAR = (-1.0,) * 4


def colour(u, v):
    return (0.25 + u * 0.5, v * 0.5, 0.125 + (u + v) * 0.25, 1.0)


def float_words(values):
    return struct.unpack(f"<{len(values)}I", struct.pack(f"<{len(values)}f", *values))


def settings(vertices, geometry):
    words = {}

    def write(offset, data):
        for i, word in enumerate(data):
            words[offset + i * 4] = word

    # ps_m15 reads its embedded scale at code base (zero here) + 0x100.
    write(0x100, float_words((0.5, 2.0, 1.0, 1.0)))
    # vs_m15 reads a structured V# at s0:s1, stride 32, one float4 position/colour pair per vertex.
    write(0x400, (0x800, 32 << 16, len(vertices), (1 << 28) | (77 << 12) | 0xfac))
    for i, (x, y) in enumerate(vertices):
        rgba = colour((x + 1) / 2, (y + 1) / 2)
        write(0x800 + 32 * i, float_words((x, y, 0.0, 1.0, *rgba)))
    env = {k: v for k, v in os.environ.items() if not k.startswith("VKH_")}
    return dict(env, VKH_GS=str(geometry), VKH_SGPRS="0x400,0", VKH_VERTICES=str(len(vertices)),
                VKH_HEAP=",".join(f"{k:#x}={v:#x}" for k, v in words.items()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("harness", "shaders", "geometry", "flat"):
        parser.add_argument(key, type=Path)
    args = parser.parse_args()
    shaders = args.shaders.resolve()
    # Reference depends only on pixel centres and the first submitted vertex.
    smooth = [tuple(v * s for v, s in zip(colour((x + 0.5) / WIDTH, (y + 0.5) / HEIGHT),
                                           (0.5, 2, 1, 1)))
              for y in range(HEIGHT) for x in range(WIDTH)]
    def flat_value(vertices):
        (x, y), (bx, by), (cx, cy) = vertices
        area = (bx - x) * (cy - y) - (by - y) * (cx - x)
        # vk_harness uses a positive-height viewport and counterclockwise front-face state.
        return (*colour((x + 1) / 2, (y + 1) / 2)[:3], 1.0 if area < 0 else 0.5)

    digest = hashlib.sha256()
    passed = {"gradient": 0, "flat": 0}
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp) / "pixels"

        def render(vertices, fragment, reference):
            raw.unlink(missing_ok=True)
            run = subprocess.run([args.harness.resolve(), shaders / "vs_m15.spv", fragment, raw,
                                  str(WIDTH), str(HEIGHT)], env=settings(vertices, args.geometry.resolve()),
                                 capture_output=True, text=True)
            if run.returncode:
                if run.stderr:
                    print(run.stderr, file=sys.stderr, end="")
                raise SystemExit(run.returncode)
            data = raw.read_bytes()
            digest.update(data)
            if len(data) != len(reference) * 16:
                raise ValueError(f"unexpected readback size {len(data)}")
            pixels = list(struct.iter_unpack("<4f", data))
            matches = sum(all(abs(a - b) <= 2e-6 for a, b in zip(pixel, want))
                          for pixel, want in zip(pixels, reference))
            return len(pixels) - pixels.count(CLEAR), matches

        for corners in itertools.permutations(POSITIONS, 3):
            vertices = [POSITIONS[c] for c in corners]
            gradient = render(vertices, shaders / "ps_m15.spv", smooth)
            flat = render(vertices, args.flat.resolve(), [flat_value(vertices)] * len(smooth))
            passed["gradient"] += gradient[1] == len(smooth)
            passed["flat"] += flat[1] == len(smooth)
            print(f"{' '.join(corners)}  covered/correct gradient {gradient} flat {flat}")

        # Different right-angle positions, winding, and provoking colours in one six-vertex draw.
        left = [(-1, 1), (-1, -1), (0, 1)]
        right = [(1, 1), (0, -1), (0, 1)]
        left_flat, right_flat = flat_value(left), flat_value(right)
        flat_reference = [left_flat if x < WIDTH // 2 else right_flat
                          for y in range(HEIGHT) for x in range(WIDTH)]
        mixed_gradient = render(left + right, shaders / "ps_m15.spv", smooth)
        mixed_flat = render(left + right, args.flat.resolve(), flat_reference)
        print(f"Two rectangles in one draw: gradient {mixed_gradient} flat {mixed_flat}")
        mixed_ok = mixed_gradient[1] == mixed_flat[1] == len(smooth)
    print(f"Fully correct orders: gradient {passed['gradient']}/24, flat {passed['flat']}/24")
    print(f"RGBA32F SHA-256: {digest.hexdigest()}")
    return int(not mixed_ok or any(n != 24 for n in passed.values()))


if __name__ == "__main__":
    raise SystemExit(main())
