"""Build the standalone proxy mock and hardware test with LLVM-MinGW and DirectX-Headers v1.614.1."""
import argparse
from pathlib import Path
import re
import subprocess
from uuid import UUID


def write_sdk_adapter(sdk, build):
    header = (sdk / "d3d12.h").read_text()
    # Keep each GNU virtual declaration exactly as shipped. Only the nonvirtual
    # conveniences are missing from Microsoft's headers for this compiler.
    overloads = (
        ("GetResourceAllocationInfo", "D3D12_RESOURCE_ALLOCATION_INFO",
         "UINT mask, UINT count, const D3D12_RESOURCE_DESC* descs", ", mask, count, descs"),
        ("GetCustomHeapProperties", "D3D12_HEAP_PROPERTIES", "UINT mask, D3D12_HEAP_TYPE type", ", mask, type"),
        ("GetAdapterLuid", "LUID", "", ""),
    )
    for method, result, params, args in overloads:
        pattern = rf"(virtual {result} \*STDMETHODCALLTYPE {method}\([^;]*?= 0;)(\s*#endif)"
        wrapper = (f"\n        {result} {method}({params}) {{\n"
                   f"            {result} out;\n"
                   f"            {method}(&out{args});\n"
                   "            return out;\n        }")
        header, count = re.subn(pattern, lambda match: match[1] + wrapper + match[2], header)
        if count != 1:
            raise ValueError(f"Expected one GNU declaration of {method} in DirectX-Headers v1.614.1")
    (build / "d3d12.h").write_text(header)

    # MinGW implements __uuidof using template specializations. Derive those
    # for the Microsoft headers from the public MIDL interface declarations.
    guids = ["#pragma once", "#include <d3d12.h>"]
    for name in ("d3dcommon.h", "d3d12.h"):
        declarations = (sdk / name).read_text()
        for guid, iface in re.findall(r'MIDL_INTERFACE\("([\da-fA-F-]{36})"\)\s*(\w+)', declarations):
            value = UUID(guid)
            fields = (value.time_low, value.time_mid, value.time_hi_version, *value.bytes[8:])
            guids.append(f"__CRT_UUID_DECL({iface}, {', '.join(map(hex, fields))})")
    (build / "public_ids.h").write_text("\n".join(guids) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path, help="Leonida checkout")
    parser.add_argument("sdk", type=Path, help="DirectX-Headers v1.614.1 include/directx directory")
    parser.add_argument("build", type=Path, help="separate build directory")
    parser.add_argument("--cxx", default="x86_64-w64-mingw32-clang++")
    args = parser.parse_args()
    build = args.build.resolve()
    sdk = args.sdk.resolve()
    if build == sdk:
        parser.error("Use a separate build directory to preserve the SDK header")
    build.mkdir(parents=True, exist_ok=True)
    write_sdk_adapter(sdk, build)
    runtime = args.checkout.resolve() / "runtime"
    flags = [args.cxx, "-std=c++20", "-O1", "-fms-extensions", "-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX",
             f"-I{build}", f"-I{sdk}", f"-I{runtime / 'shims'}", "-include", str(build / "public_ids.h")]
    proxy = build / "proxy.o"
    subprocess.run([*flags, "-c", runtime / "shims/d3d12_xs_device.cpp", "-o", proxy], check=True)
    for name, source, extra in (
        ("xs_proxy_mock_test", runtime / "test/xs_proxy_mock_test.cpp", ["-D_NODISCARD=", "-D_VCRT_ALLOCATOR="]),
        ("host_device", Path(__file__).with_name("host_device.cpp"), []),
    ):
        subprocess.run([*flags, *extra, source, proxy, "-static", "-ld3d12", "-ldxgi", "-ldxguid",
                        "-lole32", "-luuid", "-luser32", "-o", build / f"{name}.exe"], check=True)


if __name__ == "__main__":
    main()
