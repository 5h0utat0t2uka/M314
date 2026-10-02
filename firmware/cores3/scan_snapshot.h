#pragma once

#include "ld2450.h"

// Display samples share the ripple clock; UART reception remains independent.
class ScanSnapshot {
 public:
  static constexpr uint32_t periodMs = 800;
  static constexpr uint32_t sweepMs = 300;
  ld2450::Target targets[3] = {};
  bool valid = false;
  uint32_t phaseMs = 0;

  bool update(uint32_t nowMs, const ld2450::Parser& radar) {
    bool boundary = !started_;
    if (!started_) {
      started_ = true;
      cycleStartMs_ = nowMs;
    }
    const uint32_t elapsed = nowMs - cycleStartMs_;
    if (elapsed >= periodMs) {
      cycleStartMs_ += (elapsed / periodMs) * periodMs;
      boundary = true;
    }
    phaseMs = nowMs - cycleStartMs_;
    if (!radar.fresh(nowMs)) {
      const bool changed = valid;
      valid = false;
      for (auto& target : targets) target = {};
      return changed;
    }
    if (!boundary) return false;
    for (size_t i = 0; i < 3; ++i) targets[i] = radar.targets[i];
    valid = true;
    return true;
  }

 private:
  bool started_ = false;
  uint32_t cycleStartMs_ = 0;
};
