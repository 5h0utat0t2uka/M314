#include "../firmware/cores3/target_trail.h"

#include <cassert>
#include <cstdio>

int main() {
  TargetTrail trail;
  ld2450::Target target{0, 2000, 0, 320, true};
  for (uint32_t t = 0; t <= 2500; t += 100) {
    target.xMm = t / 10;
    trail.update(target, t);
  }
  assert(trail.count == 20 && trail.points[0].timeMs == 600);
  assert(trail.points[19].xMm == 250);
  trail.expire(4500);
  assert(trail.count == 0);

  trail.update(target, 5000);
  target.present = false;
  trail.update(target, 5100);
  assert(trail.count == 0);
  target.present = true;
  trail.update(target, 5200);
  assert(trail.count == 1);
  trail.update(target, 5600);  // A report gap must not bridge old/new tracks.
  assert(trail.count == 1 && trail.points[0].timeMs == 5600);
  target.xMm += 1500;
  trail.update(target, 5700);
  assert(trail.count == 1 && trail.points[0].xMm == target.xMm);

  // Even malformed extreme coordinates cannot overflow the jump calculation.
  target.xMm = INT16_MIN;
  target.yMm = INT16_MIN;
  trail.update(target, 5800);
  target.xMm = INT16_MAX;
  target.yMm = INT16_MAX;
  trail.update(target, 5900);
  assert(trail.count == 1);

  // Bound memory even if reports arrive much faster than the normal 10 Hz.
  for (uint32_t t = 5901; t < 6000; ++t) trail.update(target, t);
  assert(trail.count == TargetTrail::capacity);
  assert(trail.points[0].timeMs == 6000 - TargetTrail::capacity);

  trail.count = 0;
  const uint32_t start = UINT32_MAX - 100;
  trail.update(target, start);
  trail.update(target, start + 200);
  assert(trail.count == 2);
  trail.expire(start + TargetTrail::lifetimeMs);
  assert(trail.count == 1 && trail.points[0].timeMs == start + 200);
  std::puts("Target trail tests passed");
}
