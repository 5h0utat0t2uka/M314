#include <M5Unified.h>
#include <Preferences.h>
#include <math.h>
#include "ld2450.h"
#include "relative_heading.h"
#include "target_trail.h"
#include "region_filter.h"
#include "display_diff.h"

constexpr int kRadarRx = 18;  // PORT.C R <- LD2450 TX
constexpr int kRadarTx = 17;  // PORT.C T -> LD2450 RX
constexpr uint32_t kRadarBaud = 256000;
constexpr int kOriginX = 160;
constexpr int kOriginY = 164;
constexpr int kRadius = 154;
constexpr float kRadians = 0.01745329252f;
constexpr uint16_t kBackground = 0x0000;
constexpr uint16_t kGrid = 0x08E9;
constexpr uint16_t kMuted = 0x1BFC;
constexpr uint16_t kBlue = 0x1BFC;  // #187FE5 encoded as RGB565
constexpr uint16_t kBright = 0x957E;
constexpr uint16_t kPanelBackground = 0x1BFC;  // #187FE5 encoded as RGB565
constexpr uint16_t kPanelText = 0xFFFF;  // #ffffff encoded as RGB565
constexpr uint16_t kDistance = 0xE105; // #e6222f encoded as RGB565
constexpr uint16_t kRipple = 0x75DF;  // #70b8ff encoded as RGB565
constexpr uint32_t kRadarFrameMs = 20;
constexpr uint32_t kPanelFrameMs = 100;

HardwareSerial radarSerial(1);
M5Canvas screen(&M5.Display);
M5Canvas radarBackdrop(&M5.Display);
M5Canvas fixedGrid(&M5.Display);
M5Canvas displayedFrame(&M5.Display);
ld2450::Parser radar;
RelativeHeading heading;
TargetTrail trails[3];
uint32_t lastDrawMs = 0;
uint32_t lastPanelMs = 0;
uint32_t lastImuPollMs = 0;
float cachedRotation = NAN;
unsigned rangeMeters = 6;
bool screenReady = false;
bool displayInitialized = false;

void drawText(const char* text, int x, int y, uint16_t color, int size = 1,
              uint16_t background = kBackground,
              lgfx::textdatum_t datum = lgfx::textdatum_t::top_left) {
  screen.setFont(&fonts::Font0);
  screen.setTextDatum(datum);
  screen.setTextSize(size);
  screen.setTextColor(color, background);
  screen.drawString(text, x, y);
}

// Angles are measured from forward (+Y); +X goes right in the sensor's view.
int polarX(float radius, float degrees) {
  return kOriginX + lroundf(radius * sinf(degrees * kRadians));
}

int polarY(float radius, float degrees) {
  return kOriginY - lroundf(radius * cosf(degrees * kRadians));
}

void drawArc(float radius, float startAngle, float endAngle, uint16_t color) {
  const float start = fmaxf(-90, startAngle);
  const float end = fminf(90, endAngle);
  if (start >= end) return;
  int previousX = polarX(radius, start);
  int previousY = polarY(radius, start);
  for (float angle = start; angle < end;) {
    angle = fminf(angle + 2, end);
    const int x = polarX(radius, angle);
    const int y = polarY(radius, angle);
    screen.drawSmoothLine(previousX, previousY, x, y, color);
    previousX = x;
    previousY = y;
  }
}

void drawRing(float radius, uint16_t color) {
  drawArc(radius, -90, 90, color);
}

void drawRadial(float inner, float outer, float angle, uint16_t color) {
  if (angle < -90 || angle > 90) return;
  screen.drawSmoothLine(polarX(inner, angle), polarY(inner, angle),
                  polarX(outer, angle), polarY(outer, angle), color);
}

void drawFixedGrid() {
  constexpr float inner = kRadius * 0.45f;
  drawRing(kRadius - 1, kGrid);
  drawRing(kRadius, kBlue);
  drawRing(inner + 1, kGrid);
  drawRing(inner, kMuted);
}

void drawRotatingGrid(float rotation) {
  constexpr float inner = kRadius * 0.45f;
  // The semicircle stays forward-facing; its spokes and bracket marks rotate.
  // Target positions already use the sensor frame and must not rotate twice.
  for (int bearing = -180; bearing < 180; bearing += 15) {
    const float angle = RelativeHeading::wrap(bearing - rotation);
    const bool major = bearing % 45 == 0;
    drawRadial(kRadius - (major ? 7 : 3), kRadius + (major ? 3 : 1), angle, kBlue);
    if (major) {
      // Alternate spokes that stop at the inner arc with spokes passing through it.
      const float start = bearing % 90 == 0 ? inner * 0.65f : inner;
      drawRadial(start, kRadius, angle, kMuted);
    }
  }
  for (int bearing = -180; bearing < 180; bearing += 90) {
    const float angle = RelativeHeading::wrap(bearing - rotation);
    drawArc(kRadius - 11, angle - 20, angle + 20, kBlue);
    drawRadial(kRadius - 11, kRadius + 3, angle - 20, kBlue);
    drawRadial(kRadius - 11, kRadius + 3, angle + 20, kBlue);
  }
  for (int bearing = -135; bearing < 180; bearing += 90) {
    const float angle = RelativeHeading::wrap(bearing - rotation);
    drawArc(inner + 9, angle - 12, angle + 12, kBlue);
    drawRadial(inner, inner + 9, angle - 12, kBlue);
    drawRadial(inner, inner + 9, angle + 12, kBlue);
  }
  screen.drawSmoothLine(6, kOriginY, polarX(inner, -90), kOriginY, kBlue);
  screen.drawSmoothLine(polarX(inner, 90), kOriginY, 314, kOriginY, kBlue);
}

void drawRipple(uint32_t nowMs) {
  // One 1-pixel AA arc expands for 300 ms, then stays absent for 1000 ms.
  // This visual effect never gates reception or target updates.
  const uint32_t phaseMs = nowMs % 1300;
  if (phaseMs >= 300) return;
  const float radius = kRadius * phaseMs / 300.0f;
  if (radius >= 1) drawRing(radius, kRipple);
}

uint16_t dimColor(uint16_t color, float brightness) {
  const unsigned r = ((color >> 11) & 31) * brightness;
  const unsigned g = ((color >> 5) & 63) * brightness;
  const unsigned b = (color & 31) * brightness;
  return (r << 11) | (g << 5) | b;
}

void drawTrails(uint32_t nowMs, float rangeMm) {
  for (auto& trail : trails) {
    trail.expire(nowMs);
    for (size_t i = 1; i < trail.count; ++i) {
      const auto& a = trail.points[i - 1];
      const auto& b = trail.points[i];
      // Do not join across a part of the path outside the displayed semicircle.
      if (a.yMm < 0 || b.yMm < 0 || hypotf(a.xMm, a.yMm) > rangeMm ||
          hypotf(b.xMm, b.yMm) > rangeMm) continue;
      const int ax = kOriginX + lroundf(a.xMm * kRadius / rangeMm);
      const int ay = kOriginY - lroundf(a.yMm * kRadius / rangeMm);
      const int bx = kOriginX + lroundf(b.xMm * kRadius / rangeMm);
      const int by = kOriginY - lroundf(b.yMm * kRadius / rangeMm);
      if (ax == bx && ay == by) continue;
      const float brightness = 0.65f * (1 - uint32_t(nowMs - a.timeMs) / float(TargetTrail::lifetimeMs));
      screen.drawSmoothLine(ax, ay, bx, by, dimColor(kBlue, brightness));
    }
  }
}

void drawStatusPanel() {
  // Radius-2 circular fillets join the existing sloped recess to its top/bottom.
  constexpr float radius = 2;
  constexpr float slope = 10.0f / 38;
  const float normal = sqrtf(1 + slope * slope);
  const float offset = radius * (normal - slope);
  for (int y = 178; y < 216; ++y) {
    float edge = 86 + (y - 178) * slope;
    if (y < 180 - radius * slope / normal) {
      edge = 86 - offset + sqrtf(fmaxf(0, radius * radius - (y - 180) * (y - 180)));
    } else if (y > 214 + radius * slope / normal) {
      edge = 96 + offset - sqrtf(fmaxf(0, radius * radius - (y - 214) * (y - 214)));
    }
    const int solid = static_cast<int>(edge);
    screen.fillRect(4, y, solid - 4, 1, kPanelBackground);
    screen.fillRect(321 - solid, y, solid - 4, 1, kPanelBackground);
    const uint16_t edgeColor = dimColor(kPanelBackground, edge - solid);
    screen.drawPixel(solid, y, edgeColor);
    screen.drawPixel(320 - solid, y, edgeColor);
  }
  // Finish the two lower fillets at their horizontal tangents.
  const int tangent = static_cast<int>(96 + offset);
  screen.fillRect(4, 216, 313, 11, kPanelBackground);
  const uint16_t edgeColor = dimColor(kPanelBackground, 96 + offset - tangent);
  screen.drawPixel(tangent, 216, edgeColor);
  screen.drawPixel(320 - tangent, 216, edgeColor);
  screen.fillRect(tangent + 1, 216, 319 - 2 * tangent, 1, kBackground);
}

void presentScreen(int rows) {
  const auto* current = static_cast<const uint16_t*>(screen.getBuffer());
  auto* previous = static_cast<uint16_t*>(displayedFrame.getBuffer());
  bool writing = false;
  display_diff::forEachRegion(current, previous, rows, !displayInitialized,
      [&](int x, int y, int w, int h) {
        if (!writing) {
          M5.Display.startWrite();
          writing = true;
        }
        // Clip the destination, retaining the source's full 320-pixel stride.
        M5.Display.setClipRect(x, y, w, h);
        screen.pushSprite(0, 0);
        for (int row = y; row < y + h; ++row) {
          const int offset = row * display_diff::width + x;
          memcpy(previous + offset, current + offset, w * sizeof(uint16_t));
        }
      });
  if (writing) {
    M5.Display.clearClipRect();
    M5.Display.endWrite();
  }
  displayInitialized = true;
}

void drawStatus(uint32_t nowMs, bool updatePanel = true) {
  if (!screenReady) return;
  const bool fresh = radar.fresh(nowMs);
  const bool imuFresh = heading.fresh(nowMs);
  const float rotation = imuFresh && heading.ready ? heading.degrees : 0;
  screen.setClipRect(0, 0, 320, kOriginY + 1);
  // Reuse the AA grid while stationary. 0.15 degrees is <0.5 px at the rim.
  if (!isfinite(cachedRotation) ||
      fabsf(RelativeHeading::wrap(rotation - cachedRotation)) >= 0.15f) {
    fixedGrid.pushSprite(&screen, 0, 0);
    drawRotatingGrid(rotation);
    screen.pushSprite(&radarBackdrop, 0, 0);
    cachedRotation = rotation;
  } else {
    radarBackdrop.pushSprite(&screen, 0, 0);
  }
  if (fresh) drawRipple(nowMs);

  char text[64];
  unsigned count = 0;
  int nearest = -1;
  float nearestMm = 0;
  const float rangeMm = rangeMeters * 1000.0f;
  if (fresh) {
    drawTrails(nowMs, rangeMm);
    for (size_t i = 0; i < 3; ++i) {
      const auto& target = radar.targets[i];
      if (!target.present) continue;
      ++count;
      const float distance = hypotf(target.xMm, target.yMm);
      if (nearest < 0 || distance < nearestMm) {
        nearest = static_cast<int>(i);
        nearestMm = distance;
      }
      if (target.yMm < 0 || distance > rangeMm) {
        continue;
      }
      const int x = kOriginX + lroundf(target.xMm * kRadius / rangeMm);
      const int y = kOriginY - lroundf(target.yMm * kRadius / rangeMm);
      screen.fillSmoothCircle(x, y, 7, kGrid);
      screen.fillSmoothCircle(x, y, 4, kBlue);
      screen.fillSmoothCircle(x, y, 3, kGrid);
      screen.fillSmoothCircle(x, y, 2, kBright);
      snprintf(text, sizeof(text), "%u", static_cast<unsigned>(i + 1));
      drawText(text, x > 292 ? x - 15 : x + 9, y < 16 ? y + 8 : y - 10, kBright);
    }
  }
  screen.clearClipRect();
  if (!updatePanel) {
    presentScreen(kOriginY + 1);
    return;
  }

  // Filled side panels and their bottom bridge leave a black distance recess.
  screen.fillRect(0, kOriginY + 1, 320, 75, kBackground);
  // A 13-pixel black gap separates the radar baseline from the shorter panels.
  drawStatusPanel();

  char wholeMeters[8] = "--";
  char fractionalMeters[4] = "--";
  if (nearest >= 0) {
    // Round once before splitting so 1.995 m carries into 02 / 00 correctly.
    const unsigned centimeters = static_cast<unsigned>(lroundf(nearestMm / 10.0f));
    snprintf(wholeMeters, sizeof(wholeMeters), "%02u", centimeters / 100);
    snprintf(fractionalMeters, sizeof(fractionalMeters), "%02u", centimeters % 100);
  }
  // Large integer, small hundredths above the unit, centered as a single group.
  constexpr int distanceBaselineY = 208;
  // FreeSans18 digits extend 24 px above the baseline; FreeSans9 digits 12 px.
  // Align their tops: 25 px total = 13 px hundredths + 2 px gap + 10 px unit.
  constexpr int fractionBaselineY = distanceBaselineY - 24 + 12;
  // FreeSans18 '.' extends 3 px above its baseline: align its ink to the digit top.
  constexpr int separatorBaselineY = distanceBaselineY - 24 + 3;
  constexpr int separatorGap = 2;
  screen.setFont(&fonts::FreeSans18pt7b);
  screen.setTextSize(1);
  const int wholeWidth = screen.textWidth(wholeMeters);
  const int separatorWidth = screen.textWidth(".");
  screen.setFont(&fonts::FreeSans9pt7b);
  const int fractionWidth = screen.textWidth(fractionalMeters);
  const int unitWidth = screen.textWidth("m");
  const int columnWidth = fractionWidth > unitWidth ? fractionWidth : unitWidth;
  const int distanceX = kOriginX - (wholeWidth + separatorGap * 2 + separatorWidth + columnWidth) / 2;
  const int separatorX = distanceX + wholeWidth + separatorGap;
  const int columnX = separatorX + separatorWidth + separatorGap;
  // The panel is already cleared each frame. Transparent text prevents the
  // font's descent/background rectangle from erasing the blue bottom bridge.
  screen.setTextColor(kDistance);
  screen.setTextDatum(lgfx::textdatum_t::baseline_left);
  screen.setFont(&fonts::FreeSans18pt7b);
  screen.drawString(wholeMeters, distanceX, distanceBaselineY);
  screen.drawString(".", separatorX, separatorBaselineY);
  screen.setFont(&fonts::FreeSans9pt7b);
  screen.drawString(fractionalMeters, columnX + (columnWidth - fractionWidth) / 2, fractionBaselineY);
  screen.drawString("m", columnX + (columnWidth - unitWidth) / 2, distanceBaselineY);

  // Two rows per side, with the right column aligned to the panel's outer edge.
  constexpr int firstRowY = 190;
  constexpr int secondRowY = 204;
  snprintf(text, sizeof(text), "RNG %um", rangeMeters);
  drawText(text, 12, firstRowY, kPanelText, 1, kPanelBackground);
  if (imuFresh && heading.ready) {
    snprintf(text, sizeof(text), "REL %+04d", static_cast<int>(lroundf(heading.degrees)));
  } else {
    snprintf(text, sizeof(text), "%s", imuFresh ? "STILL 2s" : "IMU --");
  }
  drawText(text, 12, secondRowY, imuFresh && !heading.ready ? kDistance : kPanelText,
           1, kPanelBackground);

  // Right: nearest slot/count and its speed. N-- distinguishes a stale link from N0.
  if (fresh) {
    if (nearest >= 0) snprintf(text, sizeof(text), "T%d N%u", nearest + 1, count);
    else snprintf(text, sizeof(text), "T-- N0");
  } else {
    snprintf(text, sizeof(text), "T-- N--");
  }
  drawText(text, 308, firstRowY, kPanelText, 1, kPanelBackground,
           lgfx::textdatum_t::top_right);
  if (nearest >= 0) {
    snprintf(text, sizeof(text), "V %+dcm/s", static_cast<int>(radar.targets[nearest].speedCmS));
  } else {
    snprintf(text, sizeof(text), "V --cm/s");
  }
  drawText(text, 308, secondRowY, kPanelText, 1, kPanelBackground,
           lgfx::textdatum_t::top_right);
  presentScreen(240);
}

// Sensor settings are explicit USB-console commands, never boot-time writes.
bool radarCommand(uint16_t command, const uint8_t* payload, size_t length,
                  size_t expectedPayload, ld2450::AckParser& ack) {
  uint8_t request[38];
  const size_t size = ld2450::encodeCommand(command, payload, length, request);
  if (!size) return false;
  // Discard old reports/ACKs before starting a new transaction.
  for (size_t i = 0; i < 1024 && radarSerial.available(); ++i) radarSerial.read();
  ack = ld2450::AckParser{};
  if (radarSerial.write(request, size) != size) {
    Serial.println("ERROR: UART write failed");
    return false;
  }
  const uint32_t started = millis();
  while (uint32_t(millis() - started) < 1500) {
    for (size_t i = 0; i < 1024 && radarSerial.available(); ++i) {
      const int byte = radarSerial.read();
      if (byte < 0 || !ack.push(static_cast<uint8_t>(byte))) continue;
      if (ack.command != (command | 0x0100)) continue;
      if (ack.status != 0 || ack.payloadSize != expectedPayload) {
        Serial.printf("ERROR: command %04X ACK status=%u, payload=%u\n",
                      command, ack.status, unsigned(ack.payloadSize));
        return false;
      }
      return true;
    }
    delay(1);
  }
  Serial.printf("ERROR: command %04X timed out (check TX/RX and sensor power)\n", command);
  return false;
}

bool readRegionFilter(ld2450::RegionFilter& filter) {
  ld2450::AckParser ack;
  if (!radarCommand(0x00C1, nullptr, 0, 26, ack)) return false;
  memcpy(filter.bytes, ack.payload, sizeof(filter.bytes));
  if (filter.mode() > 2) {
    Serial.println("ERROR: unsupported filter mode");
    return false;
  }
  return true;
}

void printRegionFilter(const ld2450::RegionFilter& filter) {
  Serial.printf("MODE %u (%s)\n", filter.mode(),
                filter.mode() == 0 ? "OFF" : filter.mode() == 1 ? "INCLUDE" : "EXCLUDE");
  for (size_t region = 0; region < 3; ++region) {
    Serial.printf("REGION %u: (%d, %d) to (%d, %d) mm\n", unsigned(region + 1),
                  filter.coordinate(region, 0), filter.coordinate(region, 1),
                  filter.coordinate(region, 2), filter.coordinate(region, 3));
  }
}

void configureRegionFilter(const char* action) {
  Serial.printf("FILTER %s: starting\n", action);
  ld2450::AckParser ack;
  const uint8_t enable[] = {1, 0};
  bool ok = radarCommand(0x00FF, enable, sizeof(enable), 4, ack);
  ld2450::RegionFilter current;
  if (ok) ok = readRegionFilter(current);
  if (ok) printRegionFilter(current);
  Preferences backup;
  bool backupOpen = false;
  bool restoring = false;
  if (ok && strcmp(action, "status") != 0) {
    ld2450::RegionFilter desired = current;
    if (strcmp(action, "off") == 0) {
      // Disable filtering while retaining stored rectangles.
      ld2450::writeU16(desired.bytes, 0);
    } else {
      backupOpen = backup.begin("ld2450-filter", false);
      ok = backupOpen;
      if (!ok) Serial.println("ERROR: cannot open filter backup");
      if (ok && strcmp(action, "restore") == 0) {
        restoring = true;
        ok = backup.getBytesLength("original") == sizeof(desired.bytes) &&
             backup.getBytes("original", desired.bytes, sizeof(desired.bytes)) == sizeof(desired.bytes) &&
             desired.mode() <= 2;
        if (!ok) Serial.println("ERROR: no valid original settings saved; use filter off to disable");
      } else if (ok) {
        desired = ld2450::nearExclusion();
        // Keep the first pre-trial state, including all three rectangles.
        // Never replace a backup on repeat commands or after a restart.
        if (!backup.isKey("original")) {
          ok = backup.putBytes("original", current.bytes, sizeof(current.bytes)) == sizeof(current.bytes);
          if (!ok) Serial.println("ERROR: cannot save original settings; sensor unchanged");
        } else if (backup.getBytesLength("original") != sizeof(current.bytes)) {
          ok = false;
          Serial.println("ERROR: invalid backup; sensor unchanged");
        }
      }
    }
    if (ok && !(desired == current)) {
      ok = radarCommand(0x00C2, desired.bytes, sizeof(desired.bytes), 0, ack);
    }
    if (ok) {
      ld2450::RegionFilter actual;
      ok = readRegionFilter(actual);
      if (ok) {
        printRegionFilter(actual);
        ok = actual == desired;
        if (!ok) Serial.println("ERROR: filter readback does not match request");
      }
    }
  }
  // Always try to leave configuration mode, including after a lost/failed ACK.
  const bool exited = radarCommand(0x00FE, nullptr, 0, 0, ack);
  ok = ok && exited;
  if (ok && restoring && !backup.remove("original")) {
    Serial.println("WARNING: settings restored, but backup cleanup failed");
  }
  if (backupOpen) backup.end();
  Serial.println(ok ? "FILTER OK (readback verified; normal detection resumed)"
                    : "FILTER FAILED: state may be unchanged or partially applied; run filter status");
  // Wait for fresh reports after the configuration pause; remove stale trails.
  radar = ld2450::Parser{};
  for (auto& trail : trails) trail.count = 0;
}

void configureBluetooth(bool enabled) {
  Serial.printf("Bluetoothを%sに設定します。\n", enabled ? "有効" : "無効");
  ld2450::AckParser ack;
  const uint8_t enableConfig[] = {1, 0};
  // Follow the official tutorial's wire bytes: on = 01 00, off = 00 00.
  const uint8_t setting[] = {static_cast<uint8_t>(enabled ? 1 : 0), 0};
  const bool entered = radarCommand(0x00FF, enableConfig, sizeof(enableConfig), 4, ack);
  const bool saved = entered && radarCommand(0x00A4, setting, sizeof(setting), 0, ack);
  if (!saved) {
    // A lost ACK can leave a saved setting pending. Do not reboot on failure.
    radarCommand(0x00FE, nullptr, 0, 0, ack);
    Serial.println("Bluetooth設定を確認できませんでした。未変更または反映待ちの可能性があります。自動再試行・再起動は行いません。");
  } else {
    // The module replies before rebooting. A3 is sent while configuration is enabled.
    Serial.println("Bluetooth設定の保存ACKを受信しました。センサーを再起動します。");
    if (!radarCommand(0x00A3, nullptr, 0, 0, ack)) {
      radarCommand(0x00FE, nullptr, 0, 0, ack);
      Serial.println("再起動ACKを確認できませんでした。設定は保存済みですが、反映状態は未確認です。");
    } else {
      // Discard reports buffered before/during reboot; require a fresh UART frame.
      delay(300);
      for (size_t i = 0; i < 1024 && radarSerial.available(); ++i) radarSerial.read();
      radar = ld2450::Parser{};
      const uint32_t started = millis();
      while (!radar.received && uint32_t(millis() - started) < 5000) {
        for (size_t i = 0; i < 1024 && radarSerial.available(); ++i) {
          const int byte = radarSerial.read();
          if (byte >= 0 && radar.push(static_cast<uint8_t>(byte), millis())) break;
        }
        if (!radar.received) delay(1);
      }
      if (radar.received) {
        Serial.printf("Bluetooth %s: 保存・再起動ACKとUART受信再開を確認しました。BLE電波の状態は未検証です。\n",
                      enabled ? "ON" : "OFF");
      } else {
        Serial.println("保存・再起動ACKは受信しましたが、UART受信が再開しません。電源と配線を確認してください。");
      }
    }
  }
  radar = ld2450::Parser{};
  for (auto& trail : trails) trail.count = 0;
}

void pollConsole() {
  static char line[48];
  static size_t used = 0;
  static bool overflow = false;
  for (size_t i = 0; i < 64 && Serial.available(); ++i) {
    const int byte = Serial.read();
    if (byte < 0) break;
    if (byte == '\r' || byte == '\n') {
      if (overflow) Serial.println("ERROR: command too long");
      else if (used) {
        line[used] = '\0';
        if (strcmp(line, "filter near") == 0) configureRegionFilter("near");
        else if (strcmp(line, "filter off") == 0) configureRegionFilter("off");
        else if (strcmp(line, "filter restore") == 0) configureRegionFilter("restore");
        else if (strcmp(line, "filter status") == 0) configureRegionFilter("status");
        else if (strcmp(line, "bluetooth off") == 0) configureBluetooth(false);
        else if (strcmp(line, "bluetooth on") == 0) configureBluetooth(true);
        else Serial.println("Commands: filter status | filter near | filter off | filter restore | bluetooth off | bluetooth on");
      }
      used = 0;
      overflow = false;
    } else if (byte < 32 || byte > 126 || used == sizeof(line) - 1) {
      overflow = true;
    } else if (!overflow) {
      line[used++] = static_cast<char>(byte);
    }
  }
}

void setup() {
  auto config = M5.config();
  config.internal_mic = false;
  config.internal_spk = false;
  config.internal_imu = true;
  config.output_power = true;  // Supply 5 V to the external ports.
  M5.begin(config);
  Serial.begin(115200);
  // Keep library offsets fixed while our stationary bias estimate is in use.
  M5.Imu.setCalibration(0, 0, 0);
  M5.Touch.setHoldThresh(800);

  M5.Display.setRotation(1);
  M5.Display.setBrightness(128);
  screen.setColorDepth(16);
  screen.setPsram(true);
  screenReady = screen.createSprite(320, 240) != nullptr;
  radarBackdrop.setColorDepth(16);
  radarBackdrop.setPsram(true);
  screenReady = screenReady && radarBackdrop.createSprite(320, kOriginY + 1) != nullptr;
  fixedGrid.setColorDepth(16);
  fixedGrid.setPsram(true);
  screenReady = screenReady && fixedGrid.createSprite(320, kOriginY + 1) != nullptr;
  displayedFrame.setColorDepth(16);
  displayedFrame.setPsram(true);
  screenReady = screenReady && displayedFrame.createSprite(320, 240) != nullptr;
  screen.setTextWrap(false);
  if (screenReady) {
    screen.fillScreen(kBackground);
    screen.setClipRect(0, 0, 320, kOriginY + 1);
    drawFixedGrid();
    screen.pushSprite(&fixedGrid, 0, 0);
    screen.clearClipRect();
  } else {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(8, 16);
    M5.Display.println("Display buffer failed");
  }

  radarSerial.setRxBufferSize(1024);
  radarSerial.begin(kRadarBaud, SERIAL_8N1, kRadarRx, kRadarTx);
  drawStatus(millis());
}

void loop() {
  M5.update();
  pollConsole();
  const auto touch = M5.Touch.getDetail();
  if (touch.wasClicked()) {
    rangeMeters = rangeMeters == 6 ? 2 : rangeMeters + 2;
  }
  if (touch.wasHold()) heading.reset();
  const uint32_t imuNowMs = millis();
  if (M5.Imu.isEnabled() && imuNowMs - lastImuPollMs >= 5) {
    lastImuPollMs = imuNowMs;
    const auto updated = M5.Imu.update();
    const int required = m5::IMU_Class::sensor_mask_accel | m5::IMU_Class::sensor_mask_gyro;
    if ((updated & required) == required) {
      const auto data = M5.Imu.getImuData();
      heading.update(data.accel.x, data.accel.y, data.accel.z,
                     data.gyro.x, data.gyro.y, data.gyro.z, millis());
    }
  }
  // Bound the work per loop so continuous noise cannot starve the display.
  for (size_t i = 0; i < 1024 && radarSerial.available() > 0; ++i) {
    const int byte = radarSerial.read();
    if (byte < 0) break;
    const uint32_t receivedMs = millis();
    if (radar.push(static_cast<uint8_t>(byte), receivedMs)) {
      for (size_t slot = 0; slot < 3; ++slot) trails[slot].update(radar.targets[slot], receivedMs);
    }
  }

  const uint32_t nowMs = millis();
  if (!radar.fresh(nowMs)) {
    for (auto& trail : trails) trail.count = 0;
  }
  if (static_cast<uint32_t>(nowMs - lastDrawMs) >= kRadarFrameMs) {
    lastDrawMs = nowMs;
    const bool updatePanel = static_cast<uint32_t>(nowMs - lastPanelMs) >= kPanelFrameMs;
    if (updatePanel) lastPanelMs = nowMs;
    drawStatus(nowMs, updatePanel);
  }
  delay(1);
}
