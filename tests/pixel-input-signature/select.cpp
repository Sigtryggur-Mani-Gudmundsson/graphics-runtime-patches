// Load the module selected for a pixel shader's container using the runtime shader library.
#include "shader_library.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 5) return 1;
    leonida::ShaderLibrary library;
    if (!library.load(std::filesystem::path(argv[1]))) return 2;
    std::ifstream file(std::filesystem::path(argv[2]), std::ios::binary);
    const std::vector<std::uint8_t> bytecode((std::istreambuf_iterator<char>(file)), {});
    const auto program = leonida::fssm_program(bytecode);
    if (program.empty()) return 3;
    const auto hash = leonida::sha256_hex(program);
    const auto entries = std::string_view(argv[4]) == "with-signature"
        ? library.candidates("ps", hash, bytecode) : library.candidates("ps", hash);
    if (entries.empty() || entries.front()->binding_count) return 4;
    // All the fixture's candidates are binding-free. The normal PSO builder takes the first.
    const auto words = library.spirv(*entries.front());
    if (words.empty()) return 5;
    std::ofstream module(std::filesystem::path(argv[3]), std::ios::binary);
    module.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
    std::cout << entries.front()->name << '\n';
    return module.good() ? 0 : 6;
}
