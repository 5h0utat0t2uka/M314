#include "../firmware/cores3/radar_sensor.h"

#include <array>
#include <cassert>
#include <cstdio>

// Manufacturer protocol V1.0, section 2: (1.5, 2.3) m.
constexpr std::array<uint8_t, 15> sample = {
    0xF4, 0xF3, 0xF2, 0xF1, 0x04, 0x0F, 0x00,
    0x0F, 0x00, 0x17, 0x00, 0xF8, 0xF7, 0xF6, 0xF5};
constexpr std::array<uint8_t, 11> empty = {
    0xF4, 0xF3, 0xF2, 0xF1, 0x04, 0x0B, 0x00, 0xF8, 0xF7, 0xF6, 0xF5};
constexpr std::array<uint8_t, 31> five = {
    0xF4, 0xF3, 0xF2, 0xF1, 0x04, 0x1F, 0x00,
    0xF1, 0xFF, 0x17, 0x00,  // (-1.5, 2.3) m, two's complement
    0x00, 0x00, 0x00, 0x00,  // Reported (0, 0) is still present.
    0xFF, 0x7F, 0x00, 0x80,  // Signed limits must not overflow in mm.
    0x01, 0x00, 0xFF, 0xFF,
    0x1E, 0x00, 0x28, 0x00,
    0xF8, 0xF7, 0xF6, 0xF5};
constexpr std::array<uint8_t, 30> ld2450Sample = {
    0xAA, 0xFF, 0x03, 0x00, 0x0E, 0x03, 0xB1, 0x86,
    0x10, 0x00, 0x40, 0x01, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0x55, 0xCC};

template <typename Parser, size_t Size>
void feed(Parser& parser, const std::array<uint8_t, Size>& frame, uint32_t nowMs) {
  for (uint8_t byte : frame) parser.push(byte, nowMs);
}

int main() {
  ld2460::Parser parser;
  assert(!parser.fresh(0));
  for (size_t i = 0; i + 1 < sample.size(); ++i) {
    assert(!parser.push(sample[i], 100));
  }
  assert(!parser.received);
  assert(parser.push(sample.back(), 100));
  assert(parser.frames == 1 && !parser.speedAvailable);
  assert(parser.targets[0].present && parser.targets[0].xMm == 1500);
  assert(parser.targets[0].yMm == 2300 && !parser.targets[1].present);
  assert(parser.fresh(1099) && !parser.fresh(1100));

  auto corrupt = sample;
  corrupt.back() = 0;
  feed(parser, corrupt, 1200);
  assert(parser.frames == 1 && parser.invalidFrames == 1);
  assert(parser.lastFrameMs == 100 && parser.targets[0].xMm == 1500);

  feed(parser, five, 1300);
  assert(parser.frames == 2);
  for (const auto& target : parser.targets) assert(target.present);
  assert(parser.targets[0].xMm == -1500 && parser.targets[0].yMm == 2300);
  assert(parser.targets[1].xMm == 0 && parser.targets[1].yMm == 0);
  assert(parser.targets[2].xMm == 3276700 && parser.targets[2].yMm == -3276800);
  assert(parser.targets[3].xMm == 100 && parser.targets[3].yMm == -100);
  assert(parser.targets[4].xMm == 3000 && parser.targets[4].yMm == 4000);
  feed(parser, sample, 1400);
  for (size_t i = 1; i < radar_sensor::maxTargets; ++i) assert(!parser.targets[i].present);
  feed(parser, empty, 1500);
  assert(parser.fresh(1500));
  for (const auto& target : parser.targets) assert(!target.present);

  // Noise, dropped bytes, and truncation at every position recover immediately
  // on the next complete frame, without extending freshness on broken data.
  for (size_t split = 0; split < five.size(); ++split) {
    ld2460::Parser broken;
    for (unsigned i = 0; i < 80; ++i) broken.push(0xF4, 10);
    for (size_t i = 0; i < split; ++i) broken.push(five[i], 10);
    assert(!broken.received);
    feed(broken, sample, 20);
    assert(broken.frames == 1 && broken.targets[0].xMm == 1500);
    broken = ld2460::Parser{};
    for (size_t i = 0; i < five.size(); ++i) {
      if (i != split) broken.push(five[i], 30);
    }
    assert(!broken.received);
    feed(broken, empty, 40);
    assert(broken.frames == 1 && !broken.targets[0].present);
  }
  // Reject wrong function, malformed/oversized lengths, and command ACKs.
  for (size_t field : {size_t(4), size_t(5), size_t(6)}) {
    auto malformed = sample;
    malformed[field] = 0xFF;
    ld2460::Parser checked;
    feed(checked, malformed, 50);
    assert(!checked.received);
    feed(checked, sample, 60);
    assert(checked.frames == 1);
  }
  ld2460::Parser acknowledgments;
  constexpr std::array<uint8_t, 12> ack = {
      0xFD, 0xFC, 0xFB, 0xFA, 0x06, 0x0C, 0, 0x11, 4, 3, 2, 1};
  feed(acknowledgments, ack, 100);
  assert(!acknowledgments.received);
  feed(acknowledgments, sample, 200);
  assert(acknowledgments.frames == 1);

  feed(parser, sample, UINT32_MAX - 499);
  assert(parser.fresh(499) && !parser.fresh(500));

  // Both factory protocols are identified from reports (including zero targets).
  radar_sensor::Parser automatic;
  feed(automatic, corrupt, 10);
  assert(automatic.model == radar_sensor::Model::Unknown);
  feed(automatic, empty, 20);
  assert(automatic.model == radar_sensor::Model::LD2460 && automatic.fresh(20));
  feed(automatic, five, 30);
  assert(automatic.targets[4].present && !automatic.speedAvailable);
  automatic.reset();
  assert(automatic.model == radar_sensor::Model::LD2460 && !automatic.received);
  for (const auto& target : automatic.targets) assert(!target.present);
  feed(automatic, ld2450Sample, 40);
  assert(!automatic.received);  // A known model cannot be changed by another protocol.
  feed(automatic, sample, 50);
  assert(automatic.frames == 1 && automatic.fresh(50));

  automatic = radar_sensor::Parser{};
  feed(automatic, ld2450Sample, 100);
  assert(automatic.model == radar_sensor::Model::LD2450 && automatic.speedAvailable);
  assert(automatic.targets[0].xMm == -782 && automatic.targets[0].speedCmS == -16);
  assert(!automatic.targets[3].present && !automatic.targets[4].present);
  automatic.reset();
  feed(automatic, sample, 200);
  assert(!automatic.received);
  feed(automatic, ld2450Sample, 300);
  assert(automatic.frames == 1 && automatic.speedAvailable);

  std::puts("LD2460 and sensor detection tests passed");
}
