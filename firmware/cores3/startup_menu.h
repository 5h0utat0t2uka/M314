#pragma once

#include "wifi_setup.h"
#include "firmware_update.h"

class StartupMenu {
 public:
  void begin();
  bool poll();  // True after Start and a Sound choice.
  bool soundEnabled() const { return soundEnabled_; }
 private:
  enum class Page { Splash, Home, Sound, Wifi, Release, Message };
  Page page_ = Page::Home;
  uint32_t splashStartedMs_ = 0;
  WifiSetup wifi_;
  FirmwareUpdate update_;
  WifiSetup::State drawnWifiState_ = WifiSetup::State::Closed;
  bool soundEnabled_ = false;
  bool touchTest_ = false;
  bool waitForRelease_ = false;
  char touchCommand_[16] = {};
  size_t touchCommandLength_ = 0;
  bool touchCommandOverflow_ = false;
  bool pollTouchConsole();
  void drawHome();
  void drawSound();
  void drawWifi();
  void showMessage(const char* text);
};
