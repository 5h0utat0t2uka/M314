#pragma once

#include "wifi_setup.h"
#include "firmware_update.h"
#include "radar_sensor.h"

class StartupMenu {
 public:
  void begin(void (*selectSensor)(radar_sensor::Model));
  bool poll(const radar_sensor::Parser& radar);  // True after reception and a Sound choice.
  bool soundEnabled() const { return soundEnabled_; }
 private:
  enum class Page { Home, Sensor, SensorWaiting, SensorError, Sound, Wifi, Release, Message };
  Page page_ = Page::Home;
  WifiSetup wifi_;
  FirmwareUpdate update_;
  WifiSetup::State drawnWifiState_ = WifiSetup::State::Closed;
  bool soundEnabled_ = false;
  bool touchTest_ = false;
  bool waitForRelease_ = false;
  void (*selectSensor_)(radar_sensor::Model) = nullptr;
  radar_sensor::Model sensor_ = radar_sensor::Model::Unknown;
  uint32_t sensorStartedMs_ = 0;
  char touchCommand_[16] = {};
  size_t touchCommandLength_ = 0;
  bool touchCommandOverflow_ = false;
  bool pollTouchConsole();
  void drawHome();
  void drawSensor();
  void checkSensor();
  void drawSensorError();
  void drawSound();
  void drawWifi();
  void showMessage(const char* text);
};
