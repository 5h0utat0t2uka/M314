#pragma once

#include "ld2450.h"

// A short sensor-relative history per reported slot, not a persistent person ID.
struct TargetTrail {
  static constexpr uint32_t lifetimeMs = 2000;
  static constexpr size_t capacity = 24;
  struct Point {
    int16_t xMm;
    int16_t yMm;
    uint32_t timeMs;
  };
  Point points[capacity] = {};
  size_t count = 0;

  void expire(uint32_t nowMs) {
    size_t first = 0;
    while (first < count && uint32_t(nowMs - points[first].timeMs) >= lifetimeMs) ++first;
    if (first) {
      count -= first;
      memmove(points, points + first, count * sizeof(Point));
    }
  }

  void update(const ld2450::Target& target, uint32_t nowMs) {
    expire(nowMs);
    if (!target.present) {
      count = 0;
      return;
    }
    if (count) {
      const auto& previous = points[count - 1];
      const int64_t dx = int64_t(target.xMm) - previous.xMm;
      const int64_t dy = int64_t(target.yMm) - previous.yMm;
      // Break on a missing report or a >1 m jump between consecutive reports.
      if (uint32_t(nowMs - previous.timeMs) > 300 || dx * dx + dy * dy > 1000000) count = 0;
    }
    if (count == capacity) {
      memmove(points, points + 1, (capacity - 1) * sizeof(Point));
      --count;
    }
    points[count++] = {target.xMm, target.yMm, nowMs};
  }
};
