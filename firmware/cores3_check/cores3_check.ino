#include <M5Unified.h>
#include <math.h>
#include "ld2450.h"
#include "relative_heading.h"
#include "target_trail.h"

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
constexpr uint16_t kDistance = 0xFA8A;
constexpr uint16_t kRipple = 0x553F;  // #52a7fa encoded as RGB565
constexpr uint32_t kRadarFrameMs = 20;
constexpr uint32_t kPanelFrameMs = 100;

HardwareSerial radarSerial(1);
M5Canvas screen(&M5.Display);
M5Canvas radarBackdrop(&M5.Display);
ld2450::Parser radar;
RelativeHeading heading;
TargetTrail trails[3];
uint32_t lastDrawMs = 0;
uint32_t lastPanelMs = 0;
uint32_t lastImuPollMs = 0;
float cachedRotation = NAN;
unsigned rangeMeters = 6;
bool screenReady = false;

void drawText(const char* text, int x, int y, uint16_t color, int size = 1,
              uint16_t background = kBackground) {
  screen.setFont(&fonts::Font0);
  screen.setTextDatum(lgfx::textdatum_t::top_left);
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

void drawGrid(float rotation) {
  constexpr float inner = kRadius * 0.45f;
  drawRing(kRadius - 1, kGrid);
  drawRing(kRadius, kBlue);
  drawRing(inner + 1, kGrid);
  drawRing(inner, kMuted);

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

void drawStatus(uint32_t nowMs, bool updatePanel = true) {
  if (!screenReady) return;
  const bool fresh = radar.fresh(nowMs);
  const bool imuFresh = heading.fresh(nowMs);
  const float rotation = imuFresh && heading.ready ? heading.degrees : 0;
  screen.setClipRect(0, 0, 320, kOriginY + 1);
  // Reuse the AA grid while stationary. 0.15 degrees is <0.5 px at the rim.
  if (!isfinite(cachedRotation) ||
      fabsf(RelativeHeading::wrap(rotation - cachedRotation)) >= 0.15f) {
    screen.fillRect(0, 0, 320, kOriginY + 1, kBackground);
    drawGrid(rotation);
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
    // 105,600 bytes instead of 153,600: do not transfer the unchanged footer.
    M5.Display.setClipRect(0, 0, 320, kOriginY + 1);
    screen.pushSprite(0, 0);
    M5.Display.clearClipRect();
    return;
  }

  // Filled side panels and their bottom bridge leave a black distance recess.
  screen.fillRect(0, kOriginY + 1, 320, 75, kBackground);
  // A 13-pixel black gap separates the radar baseline from the shorter panels.
  drawStatusPanel();

  snprintf(text, sizeof(text), "%05.2f", nearestMm / 1000.0f);
  const char* distanceText = nearest >= 0 ? text : "--.--";
  // Center the whole value + unit, with both fonts on the same baseline.
  screen.setFont(&fonts::FreeSans18pt7b);
  screen.setTextSize(1);
  const int distanceWidth = screen.textWidth(distanceText);
  screen.setFont(&fonts::FreeSans9pt7b);
  const int unitWidth = screen.textWidth("m");
  const int distanceX = kOriginX - (distanceWidth + 4 + unitWidth) / 2;
  screen.setTextColor(kDistance, kBackground);
  screen.setTextDatum(lgfx::textdatum_t::baseline_left);
  screen.setFont(&fonts::FreeSans18pt7b);
  screen.drawString(distanceText, distanceX, 206);
  screen.setFont(&fonts::FreeSans9pt7b);
  screen.drawString("m", distanceX + distanceWidth + 4, 206);

  // Left: range, relative rotation, speed, and exceptional link states.
  snprintf(text, sizeof(text), "RNG %um", rangeMeters);
  drawText(text, 12, 184, kPanelText, 1, kPanelBackground);
  if (imuFresh && heading.ready) {
    snprintf(text, sizeof(text), "REL %+04d", static_cast<int>(lroundf(heading.degrees)));
  } else {
    snprintf(text, sizeof(text), "%s", imuFresh ? "STILL 2s" : "IMU --");
  }
  drawText(text, 12, 194, kPanelText, 1, kPanelBackground);
  if (nearest >= 0) {
    snprintf(text, sizeof(text), "V %+dcm/s", static_cast<int>(radar.targets[nearest].speedCmS));
  } else {
    snprintf(text, sizeof(text), "V --cm/s");
  }
  drawText(text, 12, 204, kPanelText, 1, kPanelBackground);
  if (!fresh) {
    drawText(radar.received ? "TIMEOUT" : "WAITING", 12, 214, kPanelText, 1, kPanelBackground);
  } else if (count == 0) {
    drawText("NO TARGET", 12, 214, kPanelText, 1, kPanelBackground);
  }

  // Right: slot/count and coordinates.
  if (fresh) {
    if (nearest >= 0) snprintf(text, sizeof(text), "T%d N%u", nearest + 1, count);
    else snprintf(text, sizeof(text), "T-- N0");
  } else {
    snprintf(text, sizeof(text), "T-- N--");
  }
  drawText(text, 236, 184, kPanelText, 1, kPanelBackground);
  if (nearest >= 0) {
    snprintf(text, sizeof(text), "X %+dmm", static_cast<int>(radar.targets[nearest].xMm));
    drawText(text, 236, 194, kPanelText, 1, kPanelBackground);
    snprintf(text, sizeof(text), "Y %+dmm", static_cast<int>(radar.targets[nearest].yMm));
    drawText(text, 236, 204, kPanelText, 1, kPanelBackground);
  } else {
    drawText("X --mm", 236, 194, kPanelText, 1, kPanelBackground);
    drawText("Y --mm", 236, 204, kPanelText, 1, kPanelBackground);
  }
  screen.pushSprite(0, 0);
}

void setup() {
  auto config = M5.config();
  config.internal_mic = false;
  config.internal_spk = false;
  config.internal_imu = true;
  config.output_power = true;  // Supply 5 V to the external ports.
  M5.begin(config);
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
  screen.setTextWrap(false);
  if (!screenReady) {
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
