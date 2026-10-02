#pragma once

#include <Arduino.h>
#include <array>

class FirmwareUpdate {
 public:
  using Progress = void (*)(const char* message, int percent);
  // Network operations run only in the startup menu, while radar rendering is paused.
  bool check(Progress progress);
  bool install(Progress progress);
  static bool confirmBoot(bool healthy);
  const char* message() const { return message_; }
  const String& version() const { return version_; }

 private:
  const char* message_ = "";
  String version_, url_;
  size_t size_ = 0;
  std::array<uint8_t, 32> sha256_{};
};
