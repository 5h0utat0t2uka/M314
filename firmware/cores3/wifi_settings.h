#pragma once

#include <stdint.h>
#include <string>
#include <string_view>
#include <cstring>

namespace wifi_settings {

// One NVS blob keeps the SSID and password together, even across a power loss.
struct Credentials {
  uint8_t version = 1;
  char ssid[33] = {};
  char password[65] = {};
};

inline bool valid(std::string_view ssid, std::string_view password) {
  if (ssid.empty() || ssid.size() > 32 || ssid.find('\0') != ssid.npos ||
      password.find('\0') != password.npos) return false;
  if (password.empty()) return true;  // Open networks.
  if (password.size() < 8 || password.size() > 64) return false;
  for (unsigned char c : password) {
    if (password.size() == 64) {
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F'))) return false;
    } else if (c < 32 || c > 126) return false;
  }
  return true;
}

inline bool valid(const Credentials& value) {
  if (value.version != 1 || !memchr(value.ssid, 0, sizeof(value.ssid)) ||
      !memchr(value.password, 0, sizeof(value.password))) return false;
  return valid(value.ssid, value.password);
}

inline std::string escapeHtml(std::string_view input) {
  std::string out;
  for (char c : input) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

}  // namespace wifi_settings
