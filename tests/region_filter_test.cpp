#include "../firmware/cores3_check/region_filter.h"

#include <cassert>
#include <cstdio>
#include <vector>

bool feed(ld2450::AckParser& parser, const std::vector<uint8_t>& bytes) {
  bool received = false;
  for (auto byte : bytes) received = parser.push(byte) || received;
  return received;
}

int main() {
  uint8_t request[38];
  const uint8_t enable[] = {1, 0};
  const uint8_t enableRequest[] = {0xFD, 0xFC, 0xFB, 0xFA, 4, 0, 0xFF, 0, 1, 0, 4, 3, 2, 1};
  assert(ld2450::encodeCommand(0xFF, enable, 2, request) == sizeof(enableRequest));
  assert(memcmp(request, enableRequest, sizeof(enableRequest)) == 0);

  const auto near = ld2450::nearExclusion();
  const uint8_t nearRequest[] = {
      0xFD, 0xFC, 0xFB, 0xFA, 0x1C, 0, 0xC2, 0, 2, 0,
      0x0C, 0xFE, 0, 0, 0xF4, 1, 0xF4, 1,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 3, 2, 1};
  assert(ld2450::encodeCommand(0xC2, near.bytes, sizeof(near.bytes), request) == sizeof(nearRequest));
  assert(memcmp(request, nearRequest, sizeof(nearRequest)) == 0);
  assert(near.mode() == 2);
  assert(near.coordinate(0, 0) == -500 && near.coordinate(0, 1) == 0);
  assert(near.coordinate(0, 2) == 500 && near.coordinate(0, 3) == 500);
  assert(ld2450::encodeCommand(0xC2, near.bytes, 27, request) == 0);
  assert(ld2450::encodeCommand(0xC2, nullptr, 26, request) == 0);
  const uint8_t exitRequest[] = {0xFD, 0xFC, 0xFB, 0xFA, 2, 0, 0xFE, 0, 4, 3, 2, 1};
  assert(ld2450::encodeCommand(0xFE, nullptr, 0, request) == sizeof(exitRequest));
  assert(memcmp(request, exitRequest, sizeof(exitRequest)) == 0);

  ld2450::AckParser parser;
  // Official C1 example: INCLUDE rectangle (+1000,+1000) to (-1000,+5000).
  const std::vector<uint8_t> queryAck = {
      0xFD, 0xFC, 0xFB, 0xFA, 0x1E, 0, 0xC1, 1, 0, 0, 1, 0,
      0xE8, 3, 0xE8, 3, 0x18, 0xFC, 0x88, 0x13,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 3, 2, 1};
  for (size_t i = 0; i < queryAck.size() - 1; ++i) assert(!parser.push(queryAck[i]));
  assert(parser.push(queryAck.back()));
  assert(parser.command == 0x01C1 && parser.status == 0 && parser.payloadSize == 26);
  ld2450::RegionFilter current;
  memcpy(current.bytes, parser.payload, 26);
  assert(current.mode() == 1 && current.coordinate(0, 2) == -1000);
  assert(current.coordinate(0, 3) == 5000);
  assert(!(current == near));
  auto off = current;
  ld2450::writeU16(off.bytes, 0);
  assert(off.mode() == 0 && memcmp(off.bytes + 2, current.bytes + 2, 24) == 0);

  const std::vector<uint8_t> setAck = {0xFD, 0xFC, 0xFB, 0xFA, 4, 0, 0xC2, 1, 0, 0, 4, 3, 2, 1};
  assert(feed(parser, setAck));
  assert(parser.command == 0x01C2 && parser.payloadSize == 0);
  auto failedAck = setAck;
  failedAck[8] = 1;
  assert(feed(parser, failedAck) && parser.status == 1);
  // Reports, bad lengths, bad footers, truncation and noise must not look like success.
  assert(!feed(parser, {0xAA, 0xFF, 3, 0, 0, 0, 0x55, 0xCC}));
  auto corrupt = setAck;
  corrupt.back() = 0;
  assert(!feed(parser, corrupt));
  corrupt = setAck;
  corrupt[4] = 0xFF;
  assert(!feed(parser, corrupt));
  assert(!feed(parser, {0xFD, 0xFC, 0xFB, 0xFA, 4, 0, 0xC2}));
  assert(feed(parser, queryAck));
  for (int i = 0; i < 256; ++i) assert(!parser.push(0xFD));
  assert(feed(parser, setAck));
  assert(parser.status == 0 && parser.command == 0x01C2);
  // Header/footer bytes inside a payload cannot truncate its enclosing frame.
  auto embedded = queryAck;
  memcpy(embedded.data() + 12, setAck.data(), 4);
  memcpy(embedded.data() + 16, setAck.data() + 10, 4);
  assert(feed(parser, embedded) && parser.payloadSize == 26);
  std::puts("LD2450 region filter protocol tests passed");
}
