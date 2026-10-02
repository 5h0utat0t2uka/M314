#include "../firmware/cores3/wifi_settings.h"
#include <cassert>
#include <iostream>

int main() {
  using namespace wifi_settings;
  assert(valid("Home", "password"));
  assert(valid("Open", ""));
  assert(valid("日本語のSSID", "password"));
  assert(valid(std::string(32, 'x'), std::string(63, 'x')));
  assert(!valid(std::string(33, 'x'), "password"));
  assert(!valid("", "password"));
  assert(!valid("Home", "short"));
  assert(!valid("Home", "password\n"));
  assert(!valid(std::string("Home\0other", 10), "password"));
  assert(!valid("Home", std::string("password\0other", 14)));
  assert(valid("Home", std::string(64, 'a')));
  assert(!valid("Home", std::string(64, 'z')));
  assert(!valid("Home", std::string(65, 'a')));
  Credentials stored;
  assert(!valid(stored));
  strcpy(stored.ssid, "Home");
  strcpy(stored.password, "password");
  assert(valid(stored));
  stored.version = 2;
  assert(!valid(stored));
  stored.version = 1;
  memset(stored.ssid, 'x', sizeof(stored.ssid));
  assert(!valid(stored));
  strcpy(stored.ssid, "Home");
  memset(stored.password, 'x', sizeof(stored.password));
  assert(!valid(stored));
  assert(escapeHtml("\"><script>'&") == "&quot;&gt;&lt;script&gt;&#39;&amp;");
  assert(escapeHtml("日本語") == "日本語");
  std::cout << "wifi_settings tests passed\n";
}
