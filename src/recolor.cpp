#include "recolor.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace hs::recolor {

namespace {

uint16_t be16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}
void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

struct Rgb {
    float r, g, b;
};

Rgb decode565(uint16_t c) {
    return {((c >> 11) & 31) / 31.0f, ((c >> 5) & 63) / 63.0f, (c & 31) / 31.0f};
}

uint16_t encode565(Rgb c) {
    const auto q = [](float v, int max) {
        return static_cast<uint16_t>(std::clamp(static_cast<int>(std::lround(v * max)), 0, max));
    };
    return static_cast<uint16_t>((q(c.r, 31) << 11) | (q(c.g, 63) << 5) | q(c.b, 31));
}

void to_hsv(Rgb c, float& h, float& s, float& v) {
    const float mx = std::max({c.r, c.g, c.b});
    const float mn = std::min({c.r, c.g, c.b});
    const float d = mx - mn;
    v = mx;
    s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 0.0f) {
        h = 0.0f;
    } else if (mx == c.r) {
        h = 60.0f * std::fmod((c.g - c.b) / d, 6.0f);
    } else if (mx == c.g) {
        h = 60.0f * ((c.b - c.r) / d + 2.0f);
    } else {
        h = 60.0f * ((c.r - c.g) / d + 4.0f);
    }
    if (h < 0.0f) h += 360.0f;
}

Rgb from_hsv(float h, float s, float v) {
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    Rgb o{0, 0, 0};
    if (h < 60) o = {c, x, 0};
    else if (h < 120) o = {x, c, 0};
    else if (h < 180) o = {0, c, x};
    else if (h < 240) o = {0, x, c};
    else if (h < 300) o = {x, 0, c};
    else o = {c, 0, x};
    return {o.r + m, o.g + m, o.b + m};
}

// Only the tunic's green changes. Measured on Kmdl's textures: the tunic, skirt and cap sit at
// hue 70-170 with saturation down to about 0.15; hair and trousers are yellow (hue 40-60) and the
// lacing brown, so a hue floor of 62 keeps them.
uint16_t recolor565(uint16_t c, const TunicColor& to) {
    float h, s, v;
    to_hsv(decode565(c), h, s, v);
    if (h < 62.0f || h > 178.0f || s < 0.10f || v < 0.03f) return c;
    const float nh = to.hue >= 0.0f ? to.hue : h;
    const float ns = std::clamp(s * to.sat, 0.0f, 1.0f);
    const float nv = std::clamp(v * to.val, 0.0f, 1.0f);
    return encode565(from_hsv(nh, ns, nv));
}

// One 8-byte CMPR (DXT1) block. colour0 > colour1 means four colours, otherwise three plus
// transparent. If the new endpoints flip order, swap them and remap the 2-bit indices so the block
// still decodes the same way.
void block(uint8_t* p, const TunicColor& to) {
    const uint16_t c0 = be16(p);
    const uint16_t c1 = be16(p + 2);
    uint16_t n0 = recolor565(c0, to);
    uint16_t n1 = recolor565(c1, to);
    if (n0 == c0 && n1 == c1) return;
    const bool four = c0 > c1;
    bool swap = false;
    if (four) {
        if (n0 < n1) swap = true;
        if (n0 == n1) {
            if (n0 < 0xFFFF) ++n0;
            else --n1;
        }
    } else if (n0 > n1) {
        swap = true;
    }
    if (swap) {
        std::swap(n0, n1);
        for (int i = 4; i < 8; ++i) {
            const uint8_t b = p[i];
            uint8_t out = 0;
            for (int k = 0; k < 4; ++k) {
                uint8_t idx = (b >> (k * 2)) & 3;
                if (four) idx ^= 1;
                else if (idx < 2) idx ^= 1;
                out |= static_cast<uint8_t>(idx << (k * 2));
            }
            p[i] = out;
        }
    }
    put16(p, n0);
    put16(p + 2, n1);
}

}  // namespace

uint32_t cmpr_size(uint32_t w, uint32_t h, uint32_t mips) {
    uint32_t total = 0;
    for (uint32_t level = 0; level < std::max<uint32_t>(mips, 1); ++level) {
        const uint32_t lw = std::max<uint32_t>(w >> level, 1);
        const uint32_t lh = std::max<uint32_t>(h >> level, 1);
        total += ((lw + 7) / 8) * ((lh + 7) / 8) * 32;
    }
    return total;
}

void cmpr(uint8_t* data, uint32_t bytes, const TunicColor& to) {
    for (uint32_t i = 0; i + 8 <= bytes; i += 8) block(data + i, to);
}

void bmd(uint8_t* file, uint32_t size, uint8_t color) {
    if (color == 0 || size < 0x20 || std::memcmp(file, "J3D2", 4) != 0) return;
    const TunicColor& to = color_of(color);
    const uint32_t sections = be32(file + 0x0C);
    uint32_t off = 0x20;
    for (uint32_t s = 0; s < sections && off + 8 <= size; ++s) {
        const uint32_t secSize = be32(file + off + 4);
        if (std::memcmp(file + off, "TEX1", 4) == 0 && off + 0x14 <= size) {
            const uint16_t count = be16(file + off + 8);
            const uint32_t headers = off + be32(file + off + 0x0C);
            std::vector<uint32_t> done;
            for (uint16_t i = 0; i < count; ++i) {
                const uint32_t h = headers + i * 0x20u;
                if (h + 0x20 > size || file[h] != 0x0E) continue;  // CMPR only
                const uint32_t img = h + be32(file + h + 0x1C);
                if (std::find(done.begin(), done.end(), img) != done.end()) continue;
                done.push_back(img);
                // ResTIMG: +0x18 is the mip count (at least one level).
                const uint32_t bytes = cmpr_size(be16(file + h + 2), be16(file + h + 4), file[h + 0x18]);
                if (img + bytes <= size) cmpr(file + img, bytes, to);
            }
            return;
        }
        if (secSize == 0) break;
        off += secSize;
    }
}

}  // namespace hs::recolor
