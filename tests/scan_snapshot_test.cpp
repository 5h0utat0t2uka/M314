#include "../firmware/cores3_check/scan_snapshot.h"
#include <cassert>
#include <cstdio>

int main() {
  constexpr uint32_t period = ScanSnapshot::periodMs;
  constexpr uint32_t midCycle = period / 2;
  static_assert(period > 2, "Tests require a mid-cycle sample");
  ld2450::Parser radar;
  ScanSnapshot scan;
  assert(!scan.update(0, radar) && !scan.valid);
  radar.received = true;
  radar.lastFrameMs = midCycle;
  radar.targets[0] = {100, 200, 30, 1, true};
  assert(!scan.update(midCycle, radar) && !scan.valid);
  radar.lastFrameMs = period - 1;
  assert(!scan.update(period - 1, radar));
  assert(scan.update(period, radar) && scan.phaseMs == 0 && scan.valid);
  radar.targets[0] = {900, 1000, -50, 1, true};
  radar.targets[1] = {0, 100, 5, 1, true};
  radar.lastFrameMs = 2 * period - 1;
  assert(!scan.update(2 * period - 1, radar));
  assert(scan.targets[0].xMm == 100 && scan.targets[0].speedCmS == 30);
  assert(!scan.targets[1].present);
  assert(scan.update(2 * period, radar));
  assert(scan.targets[0].xMm == 900 && scan.targets[0].speedCmS == -50);
  assert(scan.targets[1].present);
  // Timeout must erase held data immediately; recovery waits for the next sweep.
  // This 1000 ms is the UART freshness limit, independent of the display period.
  const uint32_t timeoutAt = radar.lastFrameMs + 1000;
  for (uint32_t t = 2 * period + 1; t < timeoutAt; ++t) {
    scan.update(t, radar);
    assert(scan.valid);
  }
  assert(scan.update(timeoutAt, radar) && !scan.valid);
  assert(!scan.targets[0].present && !scan.targets[1].present);
  uint32_t recoveryAt = timeoutAt + 1;
  if (recoveryAt % period == 0) {
    scan.update(recoveryAt, radar);
    ++recoveryAt;
  }
  radar.lastFrameMs = recoveryAt;
  assert(!scan.update(recoveryAt, radar) && !scan.valid);
  const uint32_t nextSweep = (recoveryAt / period + 1) * period;
  radar.lastFrameMs = nextSweep - 1;
  assert(!scan.update(nextSweep - 1, radar) && !scan.valid);
  assert(scan.update(nextSweep, radar) && scan.valid);
  radar.targets[0] = {};
  radar.targets[1] = {};
  radar.lastFrameMs = nextSweep + period - 1;
  assert(!scan.update(nextSweep + period - 1, radar) && scan.targets[0].present);
  assert(scan.update(nextSweep + period, radar) && scan.valid && !scan.targets[0].present);
  const uint32_t delayedAt = nextSweep + 4 * period + midCycle;
  radar.lastFrameMs = delayedAt;
  assert(scan.update(delayedAt, radar) && scan.phaseMs == midCycle);
  ScanSnapshot wrapped;
  const uint32_t start = UINT32_MAX - midCycle;
  radar.lastFrameMs = start;
  assert(wrapped.update(start, radar));
  radar.lastFrameMs = start + period;
  assert(wrapped.update(start + period, radar) && wrapped.phaseMs == 0);
  std::puts("Scan snapshot tests passed (synchronization, timeout, recovery, wraparound)");
}
