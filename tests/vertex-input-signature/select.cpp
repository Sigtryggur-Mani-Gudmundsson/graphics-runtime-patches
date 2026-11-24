// Print the Vulkan vertex layout for a synthetic mesh whose fields are POSITION.xy followed by COLOR.rgba.
#include "shader_library.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) return 1;
    const std::filesystem::path dir = argv[1];
    leonida::ShaderLibrary library;
    if (!library.load(dir)) return 2;
    std::ifstream file(dir / "variant_1.dxbc", std::ios::binary);
    const std::vector<std::uint8_t> bytecode((std::istreambuf_iterator<char>(file)), {});
    const auto hash = leonida::sha256_hex(leonida::fssm_program(bytecode));
    const auto entries = std::string_view(argv[2]) == "with-signature"
        ? library.candidates("vs", hash, bytecode) : library.candidates("vs", hash);
    // Both entries fit the IA layout and neither uses descriptors. In this case pick_entry takes the first.
    if (entries.empty() || entries.front()->input_signature.size() != 2) return 3;
    const auto& inputs = entries.front()->input_signature;
    if (inputs[0].semantic != "POSITION" || inputs[1].semantic != "COLOR") return 4;
    std::cout << inputs[0].register_index << ":2f," << inputs[1].register_index << ":4f";
}
