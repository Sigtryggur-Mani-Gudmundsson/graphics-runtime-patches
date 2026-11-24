#!/usr/bin/env bash
# Usage: bash run.sh /path/to/Leonida [Vulkan-Headers/include]
set -eu
root=$(realpath "${1:?supply the Leonida source checkout}")
test_dir=$(cd -- "$(dirname -- "$0")" && pwd)
build_dir=$(mktemp -d)
trap 'rm -rf -- "$build_dir"' EXIT
headers=()
if [[ $# -gt 1 ]]; then headers=(-I"$(realpath "$2")"); fi
patch --silent --output="$build_dir/khr_test_device.h" "$root/runtime/test/vk_test_device.h" "$test_dir/device-features.patch"
glslangValidator -V --target-env vulkan1.2 -o "$build_dir/query.spv" "$test_dir/query.comp"
spirv-val --target-env vulkan1.2 "$build_dir/query.spv"
"${CXX:-c++}" -std=c++20 -O1 -I"$build_dir" -I"$root/runtime/test" -I"$root/runtime/src" "${headers[@]}" \
    "$test_dir/hardware.cpp" -ldl -pthread -o "$build_dir/hardware"
"$build_dir/hardware" "$build_dir/query.spv"
