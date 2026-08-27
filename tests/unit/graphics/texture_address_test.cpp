// Unit tests for rex/graphics/pipeline/texture/address.h (ported from xenia
// texture_address.h).
//
// The new bit-decomposition implementation is cross-checked against the
// legacy UModel-era formulas that ReXGlue's texture caches have used since
// the initial port (inlined below as independent references), plus structural
// properties documented in the header.

#include <cstdint>
#include <random>

#include <catch2/catch_test_macros.hpp>

#include <rex/graphics/pipeline/texture/address.h>

namespace {

// Legacy 2D tiled offset (from src/graphics/pipeline/texture/util.cpp,
// originally from gildor2/UModel UnTexture.cpp). Independently validates the
// Tiled2D bit mixing.
int32_t LegacyTiled2D(int32_t x, int32_t y, uint32_t pitch,
                      uint32_t bytes_per_block_log2) {
  pitch = (pitch + 31) & ~31u;
  // Top bits of coordinates.
  int32_t macro = ((x >> 5) + (y >> 5) * int32_t(pitch >> 5)) << (bytes_per_block_log2 + 7);
  // Lower bits of coordinates (result is 6-bit value).
  int32_t micro = ((x & 7) + ((y & 0xE) << 2)) << bytes_per_block_log2;
  // Mix micro/macro + add few remaining x/y bits.
  int32_t offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
  // Mix bits again.
  return ((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F);
}

// Legacy 3D tiled offset (from src/graphics/pipeline/texture/util.cpp,
// reconstructed from disassembly of XGRAPHICS::TileVolume).
int32_t LegacyTiled3D(int32_t x, int32_t y, int32_t z, uint32_t pitch, uint32_t height,
                      uint32_t bytes_per_block_log2) {
  pitch = (pitch + 31) & ~31u;
  height = (height + 31) & ~31u;
  int32_t macro_outer = ((y >> 4) + (z >> 2) * int32_t(height >> 4)) * int32_t(pitch >> 5);
  int32_t macro = ((((x >> 5) + macro_outer) << (bytes_per_block_log2 + 6)) & 0xFFFFFFF) << 1;
  int32_t micro = (((x & 7) + ((y & 6) << 2)) << (bytes_per_block_log2 + 6)) >> 6;
  int32_t offset_outer = ((y >> 3) + (z >> 2)) & 1;
  int32_t offset1 = offset_outer + ((((x >> 3) + (offset_outer << 1)) & 3) << 1);
  int32_t offset2 = ((macro + (micro & ~15)) << 1) + (micro & 15) +
                    ((z & 3) << (bytes_per_block_log2 + 6)) + ((y & 1) << 4);
  int32_t address = (offset1 & 1) << 3;
  address += (offset2 >> 6) & 7;
  address <<= 3;
  address += offset1 & ~1;
  address <<= 2;
  address += offset2 & ~511;
  address <<= 3;
  address += offset2 & 63;
  return address;
}

}  // namespace

TEST_CASE("texture_address: TiledCombine decomposes into documented bit fields",
          "[graphics][texture_address]") {
  using rex::graphics::texture_address::TiledCombine;
  std::mt19937 rng(0x5EED);
  for (int i = 0; i < 10000; ++i) {
    const int32_t outer_inner_bytes = int32_t(rng() % (1u << 20));
    const uint32_t bank = rng() & 1;
    const uint32_t pipe = rng() & 3;
    const uint32_t y_lsb = rng() & 1;
    const int32_t addr = TiledCombine(outer_inner_bytes, bank, pipe, y_lsb);
    // Documented layout: the low 8 bits of outer_inner_bytes are scattered
    // into the low 12 address bits (page) around the y_lsb/pipe/bank bits:
    //  [3:0] = oib[3:0], [4] = y_lsb, [5] = oib[4], [7:6] = pipe,
    //  [10:8] = oib[7:5], [11] = bank, [15:12] = oib[11:8]
    CHECK((addr & 0xF) == (outer_inner_bytes & 0xF));
    CHECK(((addr >> 4) & 1) == y_lsb);
    CHECK(((addr >> 5) & 1) == ((outer_inner_bytes >> 4) & 1));
    CHECK(((addr >> 6) & 3) == pipe);
    CHECK(((addr >> 8) & 7) == ((outer_inner_bytes >> 5) & 7));
    CHECK(((addr >> 11) & 1) == bank);
    CHECK((addr >> 12) == (outer_inner_bytes >> 8));
    // Round trip: recombining the extracted fields reproduces the address.
    CHECK(TiledCombine(outer_inner_bytes, bank, pipe, y_lsb) == addr);
  }
}

TEST_CASE("texture_address: Tiled2D matches legacy formula", "[graphics][texture_address]") {
  using namespace rex::graphics::texture_address;
  std::mt19937 rng(0x2D2D);
  for (int i = 0; i < 200000; ++i) {
    const int32_t x = int32_t(rng() % 16384);
    const int32_t y = int32_t(rng() % 8192);
    const uint32_t pitch = 32u * (1u + rng() % 256u);  // 32..8192
    const uint32_t bpp = rng() % 5;                    // 0..4 bytes-per-block log2
    CHECK(Tiled2D(x, y, pitch, uint32_t(bpp)) == LegacyTiled2D(x, y, pitch, uint32_t(bpp)));
  }
}

TEST_CASE("texture_address: Tiled3D matches legacy formula", "[graphics][texture_address]") {
  using namespace rex::graphics::texture_address;
  std::mt19937 rng(0x3D3D);
  for (int i = 0; i < 200000; ++i) {
    const int32_t x = int32_t(rng() % 16384);
    const int32_t y = int32_t(rng() % 2048);
    const int32_t z = int32_t(rng() % 1024);
    const uint32_t pitch = 32u * (1u + rng() % 256u);
    const uint32_t height = 32u * (1u + rng() % 64u);
    const uint32_t bpp = rng() % 5;
    // The legacy int32 formula wraps at 32 bits; Tiled3D returns the true
    // 64-bit value (up to 2^39). Low 32 bits must match in this range.
    CHECK(uint32_t(Tiled3D(x, y, z, pitch, height, uint32_t(bpp))) ==
          uint32_t(LegacyTiled3D(x, y, z, pitch, height, uint32_t(bpp))));
  }
}

TEST_CASE("texture_address: Tiled2D structural properties", "[graphics][texture_address]") {
  using namespace rex::graphics::texture_address;
  const uint32_t pitch = 256;
  const uint32_t bpp = 2;  // 4-byte blocks (e.g. 8_8_8_8)

  // Origin.
  CHECK(Tiled2D(0, 0, pitch, bpp) == 0);

  // Hand-computed values from the documented decomposition (bpp = 0):
  // (1,0) -> inner block 1; (8,0) -> pipe 1; (0,1) -> y_lsb; (0,3) -> inner 8 +
  // y_lsb; (0,16) -> bank 1.
  CHECK(Tiled2D(1, 0, 32, 0) == 1);
  CHECK(Tiled2D(8, 0, 32, 0) == (1 << 6));
  CHECK(Tiled2D(0, 1, 32, 0) == (1 << 4));
  CHECK(Tiled2D(0, 3, 32, 0) == (8 | (1 << 4)));
  CHECK(Tiled2D(0, 16, 32, 0) == (1 << 11));

  // Macro tile origin: the block at (0,0) within a 32x32 tile has the address
  // of the tile origin itself. Tile origins are laid out macro-tile-linearly
  // in blocks (X least significant), scaled by the block size, and then pass
  // through the page/bank/pipe interleaving - so they are NOT linear in bytes.
  for (int y_tile = 0; y_tile < 8; ++y_tile) {
    for (int x_tile = 0; x_tile < 16; ++x_tile) {
      const int32_t tile_linear = x_tile + int32_t(pitch >> 5) * y_tile;
      const int32_t expected = TiledCombine((tile_linear << 6) << bpp, 0, 0, 0);
      CHECK(Tiled2D(x_tile * 32, y_tile * 32, pitch, bpp) == expected);
    }
  }

  // Within a tile row the origin grows strictly monotonically (the
  // interleaving is a strictly increasing function of the block offset).
  for (int x_tile = 1; x_tile < 16; ++x_tile) {
    CHECK(Tiled2D(x_tile * 32, 0, pitch, bpp) > Tiled2D((x_tile - 1) * 32, 0, pitch, bpp));
  }
}

TEST_CASE("texture_address: Tiled3D structural properties", "[graphics][texture_address]") {
  using namespace rex::graphics::texture_address;
  const uint32_t pitch = 256;
  const uint32_t height = 128;
  const uint32_t bpp = 2;

  CHECK(Tiled3D(0, 0, 0, pitch, height, bpp) == 0);

  // Macro tile origin (32x16x4 tiles), laid out in blocks with Z most
  // significant: (x_tile + pitch/32 * (y_tile + 2 * height/32 * z_tile)) << 7,
  // then through the page/bank/pipe interleaving.
  for (int z_tile = 0; z_tile < 8; ++z_tile) {
    for (int y_tile = 0; y_tile < 4; ++y_tile) {
      for (int x_tile = 0; x_tile < 8; ++x_tile) {
        const int64_t tile_linear =
            x_tile + int64_t(pitch >> 5) * (y_tile + 2 * int64_t(height >> 5) * z_tile);
        // At tile origins the bank/pipe bits are NOT always zero: bank = z[2]
        // and pipe[1] ^= bank (see the layout doc above).
        const int64_t bank = z_tile & 1;
        const int64_t pipe = bank << 1;
        const int64_t expected = TiledCombine((tile_linear << 7) << bpp, bank, pipe, 0);
        CHECK(Tiled3D(x_tile * 32, y_tile * 16, z_tile * 4, pitch, height, bpp) == expected);
      }
    }
  }
}
