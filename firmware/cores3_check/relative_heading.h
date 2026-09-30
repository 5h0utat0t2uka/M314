#pragma once

#include <math.h>
#include <stdint.h>

// A relative turn indicator, not a magnetic compass. Calibrate while stationary.
class RelativeHeading {
 public:
  bool ready = false;
  float degrees = 0;  // Clockwise turn viewed from above is positive.

  static float wrap(float angle) {
    angle = fmodf(angle + 180.0f, 360.0f);
    if (angle < 0) angle += 360.0f;
    return angle - 180.0f;
  }

  void reset() { *this = RelativeHeading{}; }

  bool fresh(uint32_t nowMs) const {
    return received_ && static_cast<uint32_t>(nowMs - lastMs_) < 250;
  }

  // Acceleration in g, angular velocity in degrees/s (M5Unified units).
  void update(float ax, float ay, float az, float gx, float gy, float gz,
              uint32_t nowMs) {
    if (!isfinite(ax) || !isfinite(ay) || !isfinite(az) ||
        !isfinite(gx) || !isfinite(gy) || !isfinite(gz)) {
      reset();
      return;
    }
    uint32_t elapsed = received_ ? nowMs - lastMs_ : 0;
    if (elapsed > 100) {
      // Missing rotation cannot be reconstructed. Establish a new reference.
      reset();
      elapsed = 0;
    }
    lastMs_ = nowMs;
    received_ = true;
    const float accelNorm = sqrtf(ax * ax + ay * ay + az * az);
    const float gyroNorm = sqrtf(gx * gx + gy * gy + gz * gz);

    if (!ready) {
      if (accelNorm < 0.9f || accelNorm > 1.1f || gyroNorm > 3.0f) {
        samples_ = 0;
        sumX_ = sumY_ = sumZ_ = 0;
        return;
      }
      if (samples_ == 0) calibrationStartMs_ = nowMs;
      sumX_ += gx;
      sumY_ += gy;
      sumZ_ += gz;
      ++samples_;
      if (nowMs - calibrationStartMs_ >= 2000 && samples_ >= 100) {
        biasX_ = sumX_ / samples_;
        biasY_ = sumY_ / samples_;
        biasZ_ = sumZ_ / samples_;
        upX_ = ax / accelNorm;
        upY_ = ay / accelNorm;
        upZ_ = az / accelNorm;
        ready = true;
      }
      return;
    }

    // Low-pass the measured vertical axis, so modest changes in holding angle
    // do not require a hardcoded X/Y/Z yaw axis. Ignore large linear acceleration.
    if (accelNorm > 0.85f && accelNorm < 1.15f) {
      const float alpha = elapsed / (150.0f + elapsed);
      upX_ += alpha * (ax / accelNorm - upX_);
      upY_ += alpha * (ay / accelNorm - upY_);
      upZ_ += alpha * (az / accelNorm - upZ_);
    }
    const float norm = sqrtf(upX_ * upX_ + upY_ * upY_ + upZ_ * upZ_);
    if (norm < 0.1f) {
      reset();
      return;
    }
    const float clockwiseRate = -((gx - biasX_) * upX_ +
                                  (gy - biasY_) * upY_ +
                                  (gz - biasZ_) * upZ_) / norm;
    degrees = wrap(degrees + clockwiseRate * elapsed / 1000.0f);
  }

 private:
  bool received_ = false;
  uint32_t lastMs_ = 0;
  uint32_t calibrationStartMs_ = 0;
  uint32_t samples_ = 0;
  float sumX_ = 0, sumY_ = 0, sumZ_ = 0;
  float biasX_ = 0, biasY_ = 0, biasZ_ = 0;
  float upX_ = 0, upY_ = 0, upZ_ = 1;
};
