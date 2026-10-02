#pragma once

#include <WebServer.h>
#include <atomic>
#include "wifi_settings.h"

class WifiSetup {
 public:
  enum class State { Closed, Ready, Connecting, Saved, Failed };
  bool begin();
  void poll();
  void stop();
  State state() const { return state_; }
  const char* message() const { return message_; }
  const String& accessPoint() const { return apName_; }
  const String& password() const { return apPassword_; }
  static bool load(wifi_settings::Credentials& credentials);

 private:
  WebServer server_{IPAddress(192, 168, 4, 1), 80};
  State state_ = State::Closed;
  const char* message_ = "";
  String apName_, apPassword_, token_;
  wifi_settings::Credentials pending_;
  uint32_t startedMs_ = 0, connectingMs_ = 0, savedMs_ = 0;
  bool routesReady_ = false;
  bool connectPending_ = false;
  bool eventsReady_ = false;
  // Wi-Fi callbacks run on another task. Only atomic event data crosses into poll().
  static constexpr uint32_t kAssociated = 1u << 16;
  std::atomic<bool> observing_{false};
  std::atomic<uint32_t> connectionEvent_{0};
  char failureMessage_[64] = {};
  bool allowedRequest();
  void sendPage(const String& body, int status = 200, bool refresh = false);
  void showForm();
  void submit();
  void showResult();
};
