#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "radar_report.h"

namespace ld2450 {

using Target = radar_sensor::Target;

// HLK-LD2450 serial protocol V1.03, section 2.3.
// A fixed window also recovers after garbage, truncated frames, or a lost byte.
class Parser : public radar_sensor::Report {
 public:
  bool push(uint8_t byte, uint32_t nowMs) {
    if (used_ == sizeof(buffer_)) {
      memmove(buffer_, buffer_ + 1, sizeof(buffer_) - 1);
      --used_;
    }
    buffer_[used_++] = byte;
    if (used_ != sizeof(buffer_) || buffer_[0] != 0xAA ||
        buffer_[1] != 0xFF || buffer_[2] != 0x03 || buffer_[3] != 0x00) {
      return false;
    }
    if (buffer_[28] != 0x55 || buffer_[29] != 0xCC) {
      ++invalidFrames;
      return false;
    }

    for (size_t i = 0; i < 3; ++i) {
      const uint8_t* data = buffer_ + 4 + i * 8;
      targets[i].xMm = signedValue(data);
      targets[i].yMm = signedValue(data + 2);
      targets[i].speedCmS = signedValue(data + 4);
      targets[i].resolutionMm = unsignedValue(data + 6);
      // An unused target slot contains eight zero bytes, not just zero speed.
      targets[i].present = false;
      for (size_t j = 0; j < 8; ++j) {
        targets[i].present |= data[j] != 0;
      }
    }
    used_ = 0;
    ++frames;
    lastFrameMs = nowMs;
    received = true;
    speedAvailable = true;
    return true;
  }

 private:
  uint8_t buffer_[30] = {};
  size_t used_ = 0;

  static uint16_t unsignedValue(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           (static_cast<uint16_t>(data[1]) << 8);
  }

  static int16_t signedValue(const uint8_t* data) {
    const uint16_t raw = unsignedValue(data);
    const int16_t magnitude = raw & 0x7FFF;
    // This protocol uses bit 15 = positive; it is NOT two's complement.
    return (raw & 0x8000) ? magnitude : -magnitude;
  }
};

}  // namespace ld2450
