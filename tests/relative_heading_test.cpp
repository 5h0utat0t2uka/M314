#include "../firmware/cores3_check/relative_heading.h"

#include <cassert>
#include <cstdio>
#include <limits>

void near(float actual, float expected) { assert(fabsf(actual - expected) < 0.02f); }

void calibrate(RelativeHeading& heading, uint32_t start = 0, bool upright = false) {
  for (uint32_t t = 0; t <= 2000; t += 20) {
    heading.update(0, upright ? 1 : 0, upright ? 0 : 1, 0.1f, 0.2f, 0.3f, start + t);
  }
  assert(heading.ready);
  near(heading.degrees, 0);
}

int main() {
  RelativeHeading heading;
  assert(!heading.ready && !heading.fresh(0));
  calibrate(heading);
  assert(heading.fresh(2249) && !heading.fresh(2250));

  // Stationary bias is removed, rather than accumulating as rotation.
  for (uint32_t t = 2020; t <= 4000; t += 20) {
    heading.update(0, 0, 1, 0.1f, 0.2f, 0.3f, t);
  }
  near(heading.degrees, 0);
  for (uint32_t t = 4020; t <= 5000; t += 20) {
    heading.update(0, 0, 1, 0.1f, 0.2f, -29.7f, t);
  }
  near(heading.degrees, 30);
  for (uint32_t t = 5020; t <= 6000; t += 20) {
    heading.update(0, 0, 1, 0.1f, 0.2f, 30.3f, t);
  }
  near(heading.degrees, 0);

  // Rotation around a horizontal axis must not become a left/right turn.
  heading.update(0, 0, 1, 90.1f, 0.2f, 0.3f, 6020);
  near(heading.degrees, 0);

  // Standing the CoreS3 upright changes the gyro axis that represents yaw.
  heading.reset();
  calibrate(heading, 0, true);
  for (uint32_t t = 2020; t <= 3000; t += 20) {
    heading.update(0, 1, 0, 0.1f, -29.8f, 0.3f, t);
  }
  near(heading.degrees, 30);

  // Do not integrate across missing data, or reuse an old calibration window.
  heading.update(0, 1, 0, 0.1f, 0.2f, 0.3f, 4000);
  assert(!heading.ready);
  near(heading.degrees, 0);

  heading.reset();
  for (uint32_t t = 0; t <= 3000; t += 20) {
    heading.update(0, 0, 1, 0, 0, 30, t);
  }
  assert(!heading.ready);  // Moving during startup must defer calibration.
  calibrate(heading, 3020);

  heading.reset();
  const uint32_t start = UINT32_MAX - 1000;
  calibrate(heading, start);
  heading.update(0, 0, 1, 0.1f, 0.2f, -29.7f, start + 2020);
  near(heading.degrees, 0.6f);
  near(RelativeHeading::wrap(181), -179);
  near(RelativeHeading::wrap(-181), 179);
  near(RelativeHeading::wrap(720), 0);
  heading.update(0, 0, 1, 0, 0, std::numeric_limits<float>::quiet_NaN(), start + 2040);
  assert(!heading.ready && !heading.fresh(start + 2040));
  std::puts("Relative heading tests passed");
}
