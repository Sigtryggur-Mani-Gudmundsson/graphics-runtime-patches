// Run the production geometry-stage generator on a translated synthetic vertex shader.
#include "rect_list_geometry.h"
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
    const std::streamoff size = input.tellg();
    if (size <= 0 || size % 4) return 1;
    std::vector<std::uint32_t> words(size / 4);
    if (!input.seekg(0).read(reinterpret_cast<char*>(words.data()), size)) return 1;
    std::string reason;
    const auto geometry = leonida::rect_list_geometry(words, reason);
    if (geometry.empty()) {
        std::cerr << reason << '\n';
        return 1;
    }
    std::ofstream output(argv[2], std::ios::binary);
    output.write(reinterpret_cast<const char*>(geometry.data()), geometry.size() * 4);
    output.close();
    return output ? 0 : 1;
}
