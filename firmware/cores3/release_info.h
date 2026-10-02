#pragma once

#include <array>
#include <stdint.h>
#include <string_view>

namespace release_info {

inline bool version(std::string_view text, std::array<unsigned, 3>& result) {
  size_t offset = 0;
  for (size_t i = 0; i < 3; ++i) {
    const size_t start = offset;
    unsigned value = 0;
    while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') {
      if (offset - start >= 5) return false;
      value = value * 10 + (text[offset++] - '0');
    }
    if (offset == start || (offset - start > 1 && text[start] == '0')) return false;
    result[i] = value;
    if (i < 2 && (offset == text.size() || text[offset++] != '.')) return false;
  }
  return offset == text.size();
}

inline bool digest(std::string_view text, std::array<uint8_t, 32>& bytes) {
  if (text.size() != 64) return false;
  for (size_t i = 0; i < 32; ++i) {
    unsigned byte = 0;
    for (unsigned j = 0; j < 2; ++j) {
      char c = text[i * 2 + j];
      if (c >= '0' && c <= '9') byte = byte * 16 + c - '0';
      else if (c >= 'a' && c <= 'f') byte = byte * 16 + c - 'a' + 10;
      else return false;
    }
    bytes[i] = byte;
  }
  return true;
}

inline bool downloadUrl(std::string_view url) {
  // GitHub's public release redirect is the only off-repository destination allowed.
  constexpr std::string_view repository = "https://github.com/5h0utat0t2uka/hlk-ld2450/releases/";
  constexpr std::string_view assets = "https://release-assets.githubusercontent.com/";
  return url.substr(0, repository.size()) == repository || url.substr(0, assets.size()) == assets;
}

}  // namespace release_info
