#include "../firmware/cores3/display_diff.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <random>

namespace {
constexpr int width = display_diff::width;
constexpr int height = display_diff::height;
using Frame = std::array<uint16_t, width * height>;

struct Transfer {
  unsigned regions = 0;
  unsigned bytes = 0;
};

// Simulate the LCD and the saved frame independently. Every changed pixel must
// reach the LCD, even when it changes back to black. Unrequested rows stay put.
Transfer present(const Frame& current, Frame& saved, Frame& lcd,
                 int rows = height, bool force = false) {
  const Frame before = lcd;
  std::array<bool, width * height> touched{};
  Transfer result;
  display_diff::forEachRegion(current.data(), saved.data(), rows, force,
      [&](int x, int y, int w, int h) {
        assert(x >= 0 && y >= 0 && w > 0 && h > 0);
        assert(x + w <= width && y + h <= rows);
        ++result.regions;
        result.bytes += w * h * sizeof(uint16_t);
        for (int row = y; row < y + h; ++row) {
          for (int col = x; col < x + w; ++col) {
            const int i = row * width + col;
            assert(!touched[i]);  // No repeated transfer of overlapping regions.
            touched[i] = true;
            lcd[i] = saved[i] = current[i];
          }
        }
      });
  for (int i = 0; i < width * height; ++i) {
    assert(lcd[i] == (i < width * rows ? current[i] : before[i]));
    assert(saved[i] == lcd[i]);
  }
  return result;
}
}  // namespace

int main() {
  Frame current{}, saved{}, lcd{};
  lcd.fill(0xFFFF);  // Startup LCD contents need not match the saved buffer.
  auto transfer = present(current, saved, lcd, height, true);
  assert(transfer.regions == 1 && transfer.bytes == width * height * 2);
  assert(present(current, saved, lcd).regions == 0);

  current[7 * width + 15] = 0x0001;  // Include even a one-bit AA/color change.
  transfer = present(current, saved, lcd);
  assert(transfer.regions == 1 && transfer.bytes == 16 * 8 * 2);
  current[7 * width + 15] = 0;
  current[8 * width + 16] = 0x0001;  // Move across both tile boundaries.
  transfer = present(current, saved, lcd);
  assert(transfer.regions == 2 && transfer.bytes == 2 * 16 * 8 * 2);
  current.fill(0);  // Target disappears / sensor times out.
  present(current, saved, lcd);

  current[0] = current[16] = current[8 * width] = current[8 * width + 16] = 1;
  transfer = present(current, saved, lcd);
  assert(transfer.regions == 1 && transfer.bytes == 32 * 16 * 2);
  current.fill(0);
  present(current, saved, lcd);

  // A radar-only frame ends at row 164, midway through the final tile band.
  current[164 * width + 319] = 2;
  current[165 * width] = 3;
  current[239 * width + 319] = 4;
  transfer = present(current, saved, lcd, 165);
  assert(transfer.regions == 1 && transfer.bytes == 16 * 5 * 2);
  assert(saved[165 * width] == 0 && saved.back() == 0);
  present(current, saved, lcd);
  assert(saved[165 * width] == 3 && saved.back() == 4);

  current.fill(0xFFFF);  // Dense updates merge to one full-screen rectangle.
  transfer = present(current, saved, lcd);
  assert(transfer.regions == 1 && transfer.bytes == width * height * 2);

  // Successive sparse edits and erasures, with alternating radar/footer updates.
  std::mt19937 random(2450);
  for (int frame = 0; frame < 100; ++frame) {
    for (int edit = 0; edit < 50; ++edit) {
      current[random() % current.size()] = frame % 2 ? 0 : random();
    }
    present(current, saved, lcd, frame % 5 ? 165 : height);
  }
  present(current, saved, lcd);
  assert(present(current, saved, lcd).bytes == 0);
  std::puts("Display diff tests passed (LCD reconstruction, erasure, clipping, merging)");
}
