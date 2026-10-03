#include "../firmware/cores3/sound_cues.h"
#include <cassert>
#include <cstdio>

static void silent(const SoundCues::Events& events) {
  assert(!events.ripple && !events.detection && !events.stopAll && !events.stopDetection);
}

int main() {
  ld2450::Parser radar;
  ScanSnapshot scan;
  SoundCues cues;
  radar.received = true;
  radar.lastFrameMs = 100;
  radar.targets[0] = {0, 1000, 0, 1, true};
  assert(scan.update(100, radar));
  silent(cues.update(100, scan, true));  // Off never produces events.
  cues.setEnabled(true);
  auto events = cues.update(100, scan, true);
  assert(events.ripple && !events.detection);
  silent(cues.update(219, scan, false));
  events = cues.update(220, scan, false);
  assert(!events.ripple && events.detection == 1);
  silent(cues.update(221, scan, false));  // Only once per sweep.

  radar.targets[1] = {500, 1000, 0, 1, true};
  radar.targets[2] = {-500, 1000, 0, 1, true};
  radar.lastFrameMs = 400;
  assert(!scan.update(400, radar));
  silent(cues.update(400, scan, false));  // Live UART counts must not change held sound.
  radar.lastFrameMs = 900;
  assert(scan.update(900, radar));
  assert(cues.update(900, scan, true).ripple);
  assert(cues.update(1020, scan, false).detection == 3);

  for (auto& target : radar.targets) target = {};
  radar.lastFrameMs = 1700;
  assert(scan.update(1700, radar));
  events = cues.update(1700, scan, true);
  assert(events.ripple && events.stopDetection && !events.detection);
  silent(cues.update(1820, scan, false));

  radar.targets[0].present = true;
  radar.lastFrameMs = 2500;
  assert(scan.update(2500, radar));
  cues.update(2500, scan, true);
  // Link loss cancels both a scheduled tone and any playing tail.
  scan.valid = false;
  assert(cues.update(2550, scan, true).stopAll);
  silent(cues.update(2620, scan, false));
  silent(cues.update(2700, scan, false));
  radar.lastFrameMs = 3200;
  assert(!scan.update(3200, radar) && !scan.valid);
  silent(cues.update(3200, scan, false));
  assert(scan.update(3300, radar));
  assert(cues.update(3300, scan, true).ripple);
  assert(cues.update(3420, scan, false).detection == 1);

  // A stalled frame does not play stale cues during the pause, or catch up in a burst.
  radar.lastFrameMs = 4600;
  assert(scan.update(4600, radar) && scan.phaseMs == 500);
  events = cues.update(4600, scan, true);
  assert(!events.ripple && !events.detection);
  silent(cues.update(4601, scan, false));
  radar.lastFrameMs = 4900;
  assert(scan.update(4900, radar));
  cues.update(4900, scan, true);
  cues.reset();  // USB configuration cancels a pending tone.
  silent(cues.update(5020, scan, false));

  // Preserve scheduling across millis() wraparound.
  ScanSnapshot wrapped;
  const uint32_t start = UINT32_MAX - 50;
  radar.lastFrameMs = start;
  assert(wrapped.update(start, radar));
  assert(cues.update(start, wrapped, true).ripple);
  silent(cues.update(start + 119, wrapped, false));
  assert(cues.update(start + 120, wrapped, false).detection == 1);
  cues.setEnabled(false);
  silent(cues.update(start + 800, wrapped, true));
  std::puts("Sound cue tests passed (Off, sweep sync, counts, silence, timeout, stalls, wraparound)");
}
