#include "../firmware/cores3/release_info.h"
#include <cassert>
#include <iostream>
#include <string>

int main() {
  using namespace release_info;
  std::array<unsigned, 3> current{}, next{};
  assert(version("0.1.0", current));
  assert(version("0.10.0", next));
  assert(next > current);
  assert(version("1.0.0", next));
  assert(next > current);
  for (const char* bad : {"", "1", "1.2", "1.2.3.4", "v1.2.3", "1.2.3-beta",
                          "01.2.3", "-1.2.3", "1.2.3/other", "100000.0.0"}) {
    assert(!version(bad, next));
  }
  std::array<uint8_t, 32> bytes{};
  assert(digest(std::string(64, 'f'), bytes));
  assert(bytes.front() == 255 && bytes.back() == 255);
  assert(!digest(std::string(63, 'f'), bytes));
  assert(!digest(std::string(65, 'f'), bytes));
  assert(!digest(std::string(64, 'g'), bytes));
  assert(downloadUrl("https://github.com/5h0utat0t2uka/M314/releases/download/v0.1.0/firmware.bin"));
  assert(downloadUrl("https://release-assets.githubusercontent.com/github-production-release-asset/123?token=x"));
  assert(!downloadUrl("http://github.com/5h0utat0t2uka/M314/releases/latest"));
  assert(!downloadUrl("https://github.com/other/repo/releases/latest"));
  assert(!downloadUrl("https://release-assets.githubusercontent.com.attacker.test/file"));
  assert(!downloadUrl("https://release-assets.githubusercontent.com@attacker.test/file"));
  std::cout << "release_info tests passed\n";
}
