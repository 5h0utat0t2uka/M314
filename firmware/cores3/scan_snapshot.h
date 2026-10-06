#pragma once

#include "ld2450.h"

// Display samples share the ripple clock; UART reception remains independent.
class ScanSnapshot {
 public:
  static constexpr uint32_t periodMs = 800;
  static constexpr uint32_t sweepMs = 300;
  static constexpr uint32_t detectionDelayMs = 120;
  ld2450::Target targets[3] = {};
  bool valid = false;
  uint32_t phaseMs = 0;

  // Share the sound's peak time, including when sound is disabled.
  uint8_t markerOpacity() const {
    constexpr uint32_t attackMs = 60;
    constexpr uint32_t fadeMs = 600;
    constexpr unsigned minimum = 26;  // 10% of 255, rounded.
    constexpr unsigned span = 255 - minimum;
    static_assert(attackMs <= detectionDelayMs);
    static_assert(detectionDelayMs + fadeMs < periodMs);
    if (phaseMs < detectionDelayMs - attackMs) return minimum;
    if (phaseMs < detectionDelayMs) {
      return minimum + (span * (phaseMs - (detectionDelayMs - attackMs)) +
                        attackMs / 2) / attackMs;
    }
    const uint32_t elapsed = phaseMs - detectionDelayMs;
    if (elapsed >= fadeMs) return minimum;
    return minimum + (span * (fadeMs - elapsed) + fadeMs / 2) / fadeMs;
  }

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
