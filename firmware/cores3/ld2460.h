#pragma once

#include <string.h>
#include "radar_report.h"

namespace ld2460 {

// HLK-LD2460 serial protocol V1.0, section 2 (UART2).
// Reports contain 0..5 signed little-endian X/Y pairs in 0.1 m units.
class Parser : public radar_sensor::Report {
 public:
  bool push(uint8_t byte, uint32_t nowMs) {
    if (used_ == sizeof(buffer_)) {
      memmove(buffer_, buffer_ + 1, sizeof(buffer_) - 1);
      --used_;
    }
    buffer_[used_++] = byte;
    // Check frames ending at this byte, so a damaged length or truncated
    // report cannot swallow the next complete report. Memory stays bounded.
    for (size_t length = 11; length <= used_; length += 4) {
      const uint8_t* frame = buffer_ + used_ - length;
      if (frame[0] != 0xF4 || frame[1] != 0xF3 || frame[2] != 0xF2 ||
          frame[3] != 0xF1 || frame[4] != 0x04 ||
          frame[5] != length || frame[6] != 0) continue;
      const uint8_t* tail = frame + length - 4;
      if (tail[0] != 0xF8 || tail[1] != 0xF7 ||
          tail[2] != 0xF6 || tail[3] != 0xF5) {
        ++invalidFrames;
        continue;
      }
      for (auto& target : targets) target = {};
      const size_t count = (length - 11) / 4;
      for (size_t i = 0; i < count; ++i) {
        targets[i].xMm = coordinateMm(frame + 7 + i * 4);
        targets[i].yMm = coordinateMm(frame + 9 + i * 4);
        // Presence comes from packet length, including a target at (0, 0).
        targets[i].present = true;
      }
      used_ = 0;
      ++frames;
      lastFrameMs = nowMs;
      received = true;
      // This protocol has neither target speed nor distance resolution.
      speedAvailable = false;
      return true;
    }
    return false;
  }

 private:
  uint8_t buffer_[11 + 4 * radar_sensor::maxTargets] = {};
  size_t used_ = 0;

  static int32_t coordinateMm(const uint8_t* data) {
    const uint16_t raw = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
    const int32_t signedValue = raw >= 0x8000 ? int32_t(raw) - 0x10000 : raw;
    return signedValue * 100;
  }
};

}  // namespace ld2460
