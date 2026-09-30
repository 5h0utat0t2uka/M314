#include "../firmware/cores3_check/ld2450.h"

#include <array>
#include <cassert>
#include <cstdio>

using Frame = std::array<uint8_t, 30>;

// Manufacturer protocol V1.03, section 2.3: (-782, 1713), -16 cm/s, 320 mm.
constexpr Frame sample = {
    0xAA, 0xFF, 0x03, 0x00, 0x0E, 0x03, 0xB1, 0x86,
    0x10, 0x00, 0x40, 0x01, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0x55, 0xCC};

void feed(ld2450::Parser& parser, const Frame& frame, uint32_t nowMs) {
  for (const auto byte : frame) parser.push(byte, nowMs);
}

int main() {
  ld2450::Parser parser;
  assert(!parser.fresh(0));

  // Partial reads must not publish a target before the whole frame arrives.
  for (size_t i = 0; i < sample.size() - 1; ++i) {
    assert(!parser.push(sample[i], 100));
  }
  assert(!parser.received);
  assert(parser.push(sample.back(), 100));
  assert(parser.frames == 1);
  assert(parser.targets[0].present);
  assert(parser.targets[0].xMm == -782);
  assert(parser.targets[0].yMm == 1713);
  assert(parser.targets[0].speedCmS == -16);
  assert(parser.targets[0].resolutionMm == 320);
  assert(!parser.targets[1].present && !parser.targets[2].present);
  assert(parser.fresh(1099));
  assert(!parser.fresh(1100));

  // A bad footer must not update freshness or replace the last valid targets.
  auto corrupt = sample;
  corrupt[29] = 0;
  feed(parser, corrupt, 1500);
  assert(parser.frames == 1 && parser.invalidFrames == 1);
  assert(parser.lastFrameMs == 100);
  assert(!parser.fresh(1500));

  // Garbage and overlapping headers must resynchronize on a complete frame.
  for (const uint8_t byte : {0xAA, 0xAA, 0xFF, 0x03, 0x72}) {
    parser.push(byte, 1600);
  }
  feed(parser, sample, 1700);
  assert(parser.frames == 2 && parser.fresh(1700));
  for (size_t i = 0; i < 17; ++i) parser.push(sample[i], 1800);
  feed(parser, sample, 1900);
  assert(parser.frames == 3);

  // Back-to-back frames, three targets, stationary targets, and sign limits.
  auto three = sample;
  const uint8_t second[] = {0xFF, 0xFF, 0xFF, 0x7F, 0, 0, 0x68, 0x01};
  memcpy(three.data() + 12, second, sizeof(second));
  const uint8_t third[] = {0, 0x80, 1, 0x80, 0xFF, 0xFF, 0x68, 0x01};
  memcpy(three.data() + 20, third, sizeof(third));
  feed(parser, three, 2000);
  assert(parser.frames == 4);
  assert(parser.targets[1].present && parser.targets[2].present);
  assert(parser.targets[1].xMm == 32767);
  assert(parser.targets[1].yMm == -32767);
  assert(parser.targets[1].speedCmS == 0);
  assert(parser.targets[2].xMm == 0);
  assert(parser.targets[2].yMm == 1);
  assert(parser.targets[2].speedCmS == 32767);

  // A header inside the payload is data, not the start of another frame.
  auto embeddedHeader = sample;
  memcpy(embeddedHeader.data() + 12, sample.data(), 4);
  feed(parser, embeddedHeader, 2100);
  assert(parser.frames == 5);
  assert(parser.targets[1].xMm == 32682);

  Frame empty = {};
  memcpy(empty.data(), sample.data(), 4);
  empty[28] = 0x55;
  empty[29] = 0xCC;
  feed(parser, empty, 2200);
  assert(parser.fresh(2200));
  for (const auto& target : parser.targets) assert(!target.present);

  // millis() wraps after about 49.7 days; timeout remains correct.
  feed(parser, sample, UINT32_MAX - 499);
  assert(parser.fresh(499));
  assert(!parser.fresh(500));
  feed(parser, sample, 600);
  assert(parser.fresh(600));

  std::puts("LD2450 parser tests passed");
}
