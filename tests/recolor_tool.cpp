// Applies the mod's tunic recolour to a .bmd file: recolor_tool <in.bmd> <out.bmd> <colour 0-15>.
// Used by tests/recolor_preview.py to render previews from your own game files.
#include "recolor.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: recolor_tool in.bmd out.bmd colour\n");
        return 2;
    }
    FILE* in = std::fopen(argv[1], "rb");
    if (in == nullptr) return 1;
    std::vector<uint8_t> data;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(in);
    hs::recolor::bmd(data.data(), static_cast<uint32_t>(data.size()), static_cast<uint8_t>(std::atoi(argv[3])));
    FILE* out = std::fopen(argv[2], "wb");
    if (out == nullptr) return 1;
    std::fwrite(data.data(), 1, data.size(), out);
    std::fclose(out);
    return 0;
}
