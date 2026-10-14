#pragma once

#include "ld2450.h"
#include "ld2460.h"

namespace radar_sensor {

enum class Model { Unknown, LD2450, LD2460 };

struct Connection {
  int rx;
  int tx;
  uint32_t baud;
};

constexpr Connection connection(Model model) {
  if (model == Model::LD2450) return {8, 9, 256000};  // PORT.B
  if (model == Model::LD2460) return {18, 17, 115200};  // PORT.C
  return {-1, -1, 0};
}

// Identify a sensor only from a complete valid report, never from UART noise.
// A selected model accepts only that protocol; a timeout just clears data.
class Parser : public Report {
 public:
  Model model = Model::Unknown;

  void select(Model selected) {
    reset();
    model = selected;
  }

  bool push(uint8_t byte, uint32_t nowMs) {
    if (model != Model::LD2460 && ld2450_.push(byte, nowMs)) {
      model = Model::LD2450;
      static_cast<Report&>(*this) = ld2450_;
      return true;
    }
    if (model != Model::LD2450 && ld2460_.push(byte, nowMs)) {
      model = Model::LD2460;
      static_cast<Report&>(*this) = ld2460_;
      return true;
    }
    invalidFrames = ld2450_.invalidFrames + ld2460_.invalidFrames;
    return false;
  }

  void reset() {
    static_cast<Report&>(*this) = Report{};
    ld2450_ = ld2450::Parser{};
    ld2460_ = ld2460::Parser{};
  }

  const char* name() const {
    if (model == Model::LD2450) return "LD2450";
    if (model == Model::LD2460) return "LD2460";
    return "UNKNOWN";
  }

 private:
  ld2450::Parser ld2450_;
  ld2460::Parser ld2460_;
};

}  // namespace radar_sensor
