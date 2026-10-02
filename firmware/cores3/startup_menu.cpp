#include "startup_menu.h"
#include "firmware_version.h"
#include <M5Unified.h>
#include <WiFi.h>
#include <cstring>

namespace {
void text(const char* value, int x, int y, uint16_t color = TFT_WHITE) {
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(lgfx::textdatum_t::top_left);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.drawString(value, x, y);
}

void title(const char* value) {
  M5.Display.clearClipRect();
  M5.Display.fillScreen(TFT_BLACK);
  text(value, 16, 14);
}

void button(const char* value, int y, int height = 42) {
  M5.Display.fillRoundRect(16, y, 288, height, 5, 0x22FC);  // #245ce0
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(lgfx::textdatum_t::middle_center);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString(value, 160, y + height / 2);
}

void updateProgress(const char* message, int percent) {
  title("Software update");
  text(message, 16, 75);
  text("Keep power connected", 16, 107);
  if (percent >= 0) {
    M5.Display.drawRoundRect(16, 147, 288, 18, 4, TFT_WHITE);
    M5.Display.fillRect(20, 151, 280 * percent / 100, 10, 0x22FC);
    char value[8];
    snprintf(value, sizeof(value), "%d%%", percent);
    text(value, 16, 177);
  }
}
}  // namespace

void StartupMenu::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  drawHome();
}

void StartupMenu::drawHome() {
  page_ = Page::Home;
  if (touchTest_) {
    title("Touch test");
    text("Tap each +. USB: touch off", 16, 41);
    const char* labels[] = {"Start", "Update", "Wi-Fi setup"};
    for (int i = 0; i < 3; ++i) {
      const int y = 70 + i * 50;
      button("", y);
      text(labels[i], 24, y + 12);
      M5.Display.drawFastHLine(154, y + 21, 13, TFT_YELLOW);
      M5.Display.drawFastVLine(160, y + 15, 13, TFT_YELLOW);
    }
    return;
  }
  title("Motion Tracker");
  text((String("Version ") + kFirmwareVersion).c_str(), 16, 41);
  button("Start", 70);
  button("Update", 120);
  button("Wi-Fi setup", 170);
}

void StartupMenu::drawWifi() {
  drawnWifiState_ = wifi_.state();
  title("Wi-Fi setup");
  text("1. Join this Wi-Fi", 16, 43);
  text(wifi_.accessPoint().c_str(), 28, 65, 0x9E7F);
  text((String("Password: ") + wifi_.password()).c_str(), 28, 88);
  text("2. Open in your browser", 16, 120);
  text("http://192.168.4.1", 28, 143, 0x9E7F);
  text(wifi_.message(), 16, 176);
  button("Back", 204, 30);
}

void StartupMenu::showMessage(const char* message) {
  page_ = Page::Message;
  title("Motion Tracker");
  text(message, 16, 80);
  button("Back", 186);
}

bool StartupMenu::pollTouchConsole() {
  bool changed = false;
  for (int i = 0; i < 64 && Serial.available(); ++i) {
    const int byte = Serial.read();
    if (byte < 0) break;
    if (byte == '\r' || byte == '\n') {
      if (!touchCommandOverflow_ && touchCommandLength_) {
        touchCommand_[touchCommandLength_] = '\0';
        if (strcmp(touchCommand_, "touch test") == 0 || strcmp(touchCommand_, "touch off") == 0) {
          touchTest_ = strcmp(touchCommand_, "touch test") == 0;
          waitForRelease_ = true;
          drawHome();
          changed = true;
          Serial.printf("TOUCH TEST %s: screen=%ldx%ld rotation=%u\n", touchTest_ ? "ON" : "OFF",
                        static_cast<long>(M5.Display.width()), static_cast<long>(M5.Display.height()),
                        static_cast<unsigned>(M5.Display.getRotation()));
          if (touchTest_) Serial.println("Tap + at (160,91), (160,141), (160,191). Buttons are inactive.");
        } else {
          Serial.println("Menu commands: touch test | touch off");
        }
      }
      touchCommandLength_ = 0;
      touchCommandOverflow_ = false;
    } else if (touchCommandLength_ < sizeof(touchCommand_) - 1) {
      touchCommand_[touchCommandLength_++] = static_cast<char>(byte);
    } else {
      touchCommandOverflow_ = true;
    }
  }
  return changed;
}

bool StartupMenu::poll() {
  if (page_ == Page::Home && pollTouchConsole()) return false;
  if (waitForRelease_) {
    if (M5.Touch.getCount() == 0) waitForRelease_ = false;
    return false;
  }
  if (touchTest_) {
    const auto touch = M5.Touch.getDetail();
    if (touch.wasPressed()) {
      const auto raw = M5.Touch.getTouchPointRaw();
      Serial.printf("TOUCH BEGIN raw=(%d,%d) screen=(%d,%d) base=(%d,%d)\n",
                    raw.x, raw.y, touch.x, touch.y, touch.base_x, touch.base_y);
    }
    if (touch.wasReleased()) {
      Serial.printf("TOUCH END base=(%d,%d) final=(%d,%d) clicked=%u state=%u\n",
                    touch.base_x, touch.base_y, touch.x, touch.y,
                    static_cast<unsigned>(touch.wasClicked()), static_cast<unsigned>(touch.state));
      drawHome();
      M5.Display.drawCircle(touch.base_x, touch.base_y, 5, TFT_MAGENTA);
    }
    return false;
  }
  if (page_ == Page::Wifi) {
    wifi_.poll();
    if (wifi_.state() == WifiSetup::State::Closed) { drawHome(); return false; }
    if (drawnWifiState_ != wifi_.state()) drawWifi();
  }
  const auto touch = M5.Touch.getDetail();
  if (!touch.wasClicked() || touch.base_x < 16 || touch.base_x >= 304) return false;
  const int y = touch.base_y;
  if (page_ == Page::Home) {
    if (y >= 70 && y < 112) {
      WiFi.mode(WIFI_OFF);
      return true;
    }
    if (y >= 120 && y < 162) {
      if (!update_.check(updateProgress)) {
        WiFi.mode(WIFI_OFF);
        showMessage(update_.message());
      } else {
        page_ = Page::Release;
        title("Software update");
        text((String("Current: ") + kFirmwareVersion).c_str(), 16, 62);
        text((String("Available: ") + update_.version()).c_str(), 16, 88);
        button("Install", 132);
        button("Back", 186);
      }
    } else if (y >= 170 && y < 212) {
      if (wifi_.begin()) { page_ = Page::Wifi; drawWifi(); }
      else { showMessage("Could not start Wi-Fi"); }
    }
  } else if (page_ == Page::Wifi && y >= 204 && y < 234) {
    wifi_.stop();
    drawHome();
  } else if ((page_ == Page::Release || page_ == Page::Message) && y >= 186 && y < 228) {
    WiFi.mode(WIFI_OFF);
    drawHome();
  } else if (page_ == Page::Release && y >= 132 && y < 174) {
    if (!update_.install(updateProgress)) {
      WiFi.mode(WIFI_OFF);
      showMessage(update_.message());
    }
  }
  return false;
}
