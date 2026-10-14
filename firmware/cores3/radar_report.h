#pragma once

#include <stddef.h>
#include <stdint.h>

namespace radar_sensor {

constexpr size_t maxTargets = 5;

struct Target {
  // LD2460 reports signed 16-bit decimeters; convert without narrowing.
  int32_t xMm = 0;
  int32_t yMm = 0;
  int16_t speedCmS = 0;
  uint16_t resolutionMm = 0;
  bool present = false;
};

struct Report {
  Target targets[maxTargets] = {};
  uint32_t frames = 0;
  uint32_t invalidFrames = 0;
  uint32_t lastFrameMs = 0;
  bool received = false;
  bool speedAvailable = false;

  bool fresh(uint32_t nowMs) const {
    return received && static_cast<uint32_t>(nowMs - lastFrameMs) < 1000;
  }
};

}  // namespace radar_sensor
