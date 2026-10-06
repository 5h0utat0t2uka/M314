#pragma once

#include "scan_snapshot.h"

// Use the display snapshot clock; never enqueue a backlog of missed sounds.
class SoundCues {
 public:
  static constexpr uint32_t detectionDelayMs = ScanSnapshot::detectionDelayMs;
  struct Events {
    bool ripple = false;
    unsigned detection = 0;
    bool stopAll = false;
    bool stopDetection = false;
  };

  void setEnabled(bool enabled) { enabled_ = enabled; reset(); }
  void reset() { pendingCount_ = 0; active_ = false; }

  Events update(uint32_t nowMs, const ScanSnapshot& scan, bool snapshotChanged) {
    Events events;
    if (!enabled_ || !scan.valid) {
      events.stopAll = active_;
      reset();
      return events;
    }
    if (snapshotChanged) {
      cycleStartMs_ = nowMs - scan.phaseMs;
      unsigned count = 0;
      for (const auto& target : scan.targets) if (target.present) ++count;
      events.stopDetection = count == 0;
      events.ripple = scan.phaseMs < 75;
      pendingCount_ = count;
      active_ = true;
    }
    const uint32_t elapsed = nowMs - cycleStartMs_;
    if (pendingCount_ && elapsed >= detectionDelayMs) {
      // A stalled frame must not emit an old detection in the resting phase.
      if (elapsed < ScanSnapshot::sweepMs) events.detection = pendingCount_;
      pendingCount_ = 0;
    }
    return events;
  }

 private:
  bool enabled_ = false;
  bool active_ = false;
  unsigned pendingCount_ = 0;
  uint32_t cycleStartMs_ = 0;
};
