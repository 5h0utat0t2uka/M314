#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ld2450 {

inline uint16_t readU16(const uint8_t* p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

inline void writeU16(uint8_t* p, uint16_t value) {
  p[0] = value & 0xFF;
  p[1] = value >> 8;
}

struct RegionFilter {
  uint8_t bytes[26] = {};

  uint16_t mode() const { return readU16(bytes); }
  int coordinate(size_t region, size_t axis) const {
    const uint16_t raw = readU16(bytes + 2 + region * 8 + axis * 2);
    return raw < 0x8000 ? int(raw) : int(raw) - 65536;
  }
  bool operator==(const RegionFilter& other) const {
    return memcmp(bytes, other.bytes, sizeof(bytes)) == 0;
  }
};

inline RegionFilter nearExclusion() {
  RegionFilter filter;
  writeU16(filter.bytes, 2);
  // Configuration coordinates use two's complement, unlike target reports.
  writeU16(filter.bytes + 2, static_cast<uint16_t>(-500));
  writeU16(filter.bytes + 4, 0);
  writeU16(filter.bytes + 6, 500);
  writeU16(filter.bytes + 8, 500);
  return filter;
}

// Largest request here: 4 header + 2 length + 2 command + 26 payload + 4 footer.
inline size_t encodeCommand(uint16_t command, const uint8_t* payload,
                            size_t length, uint8_t (&output)[38]) {
  if (length > 26 || (length && !payload)) return 0;
  const uint8_t header[] = {0xFD, 0xFC, 0xFB, 0xFA};
  const uint8_t footer[] = {0x04, 0x03, 0x02, 0x01};
  memcpy(output, header, 4);
  writeU16(output + 4, length + 2);
  writeU16(output + 6, command);
  if (length) memcpy(output + 8, payload, length);
  memcpy(output + 8 + length, footer, 4);
  return length + 12;
}

// Bounded sliding window recovers even after bad lengths or truncated ACKs.
// Detection frames (AA FF 03 00 ... 55 CC) may share the same UART stream.
class AckParser {
 public:
  uint16_t command = 0;
  uint16_t status = 0;
  uint8_t payload[50] = {};
  size_t payloadSize = 0;

  bool push(uint8_t byte) {
    if (used_ == sizeof(buffer_)) {
      memmove(buffer_, buffer_ + 1, --used_);
    }
    buffer_[used_++] = byte;
    for (size_t start = 0; start + 14 <= used_; ++start) {
      const uint8_t* p = buffer_ + start;
      if (p[0] != 0xFD || p[1] != 0xFC || p[2] != 0xFB || p[3] != 0xFA) continue;
      const size_t length = readU16(p + 4);
      if (length < 4 || length > 54 || start + length + 10 != used_) continue;
      const uint8_t* tail = p + 6 + length;
      if (tail[0] != 4 || tail[1] != 3 || tail[2] != 2 || tail[3] != 1) continue;
      command = readU16(p + 6);
      status = readU16(p + 8);
      payloadSize = length - 4;
      memcpy(payload, p + 10, payloadSize);
      used_ = 0;
      return true;
    }
    return false;
  }

 private:
  uint8_t buffer_[64] = {};
  size_t used_ = 0;
};

}  // namespace ld2450
