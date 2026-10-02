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
  void drawHome();
  void drawWifi();
  void showMessage(const char* text);
};
