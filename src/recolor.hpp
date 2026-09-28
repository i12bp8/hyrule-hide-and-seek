#pragma once

// Tunic recolouring on raw GameCube texture data. No game code, so tests can run it on files
// extracted from a disc.

#include "common.hpp"

#include <cstdint>

namespace hs::recolor {

// Byte size of a CMPR texture including its mip levels.
uint32_t cmpr_size(uint32_t width, uint32_t height, uint32_t mips);

// Shifts the green of every CMPR block to `to`, keeping each block's decode mode.
void cmpr(uint8_t* data, uint32_t bytes, const TunicColor& to);

// Recolours every CMPR texture in a raw .bmd/.bdl file in place. Colour 0 (green) is a no-op.
void bmd(uint8_t* file, uint32_t size, uint8_t color);

}  // namespace hs::recolor
