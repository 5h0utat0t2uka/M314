#pragma once

#include <stdint.h>
#include <string.h>

namespace display_diff {

constexpr int width = 320;
constexpr int height = 240;
constexpr int tileWidth = 16;
constexpr int tileHeight = 8;

// Compare final RGB565 pixels, including AA edges, erased objects and fading
// trails. Merge neighboring tiles horizontally and identical bands vertically.
// Buffers have a 320-pixel stride; rows is the updated prefix (165 or 240).
template <typename Emit>
void forEachRegion(const uint16_t* current, const uint16_t* previous,
                   int rows, bool forceFull, Emit emit) {
  if (forceFull) {
    emit(0, 0, width, rows);
    return;
  }
  constexpr int columns = width / tileWidth;
  uint32_t masks[(height + tileHeight - 1) / tileHeight] = {};
  const int bands = (rows + tileHeight - 1) / tileHeight;
  for (int band = 0; band < bands; ++band) {
    const int top = band * tileHeight;
    const int bottom = top + tileHeight < rows ? top + tileHeight : rows;
    for (int column = 0; column < columns; ++column) {
      for (int y = top; y < bottom; ++y) {
        const int offset = y * width + column * tileWidth;
        if (memcmp(current + offset, previous + offset,
                   tileWidth * sizeof(uint16_t)) != 0) {
          masks[band] |= uint32_t{1} << column;
          break;
        }
      }
    }
  }
  for (int band = 0; band < bands;) {
    const int first = band++;
    const uint32_t mask = masks[first];
    while (band < bands && masks[band] == mask) ++band;
    if (!mask) continue;
    const int bottom = band * tileHeight < rows ? band * tileHeight : rows;
    for (int column = 0; column < columns;) {
      if (!(mask & (uint32_t{1} << column))) {
        ++column;
        continue;
      }
      const int left = column++;
      while (column < columns && (mask & (uint32_t{1} << column))) ++column;
      emit(left * tileWidth, first * tileHeight,
           (column - left) * tileWidth, bottom - first * tileHeight);
    }
  }
}

}  // namespace display_diff
