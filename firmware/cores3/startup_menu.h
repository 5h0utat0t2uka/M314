#pragma once

#include "wifi_setup.h"
#include "firmware_update.h"

class StartupMenu {
 public:
  void begin();
  bool poll();  // True once Start is selected.
 private:
  enum class Page { Home, Wifi, Release, Message };
  Page page_ = Page::Home;
  WifiSetup wifi_;
  FirmwareUpdate update_;
  WifiSetup::State drawnWifiState_ = WifiSetup::State::Closed;
  bool touchTest_ = false;
  bool waitForRelease_ = false;
  char touchCommand_[16] = {};
  size_t touchCommandLength_ = 0;
  bool touchCommandOverflow_ = false;
  bool pollTouchConsole();
  void drawHome();
  void drawWifi();
  void showMessage(const char* text);
};
