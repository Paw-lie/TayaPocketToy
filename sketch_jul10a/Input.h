#pragma once

#include <Arduino.h>

#include "AppTypes.h"

class InputManager {
 public:
  void begin();
  bool poll(InputEvent& event, uint32_t nowMs);

 private:
  struct ButtonRuntime {
    bool enabled;
    bool stableLevel;
    bool lastReading;
    uint32_t lastChangeMs;
    uint32_t pressedMs;
  };

  static constexpr uint8_t kQueueCapacity = 8;

  ButtonRuntime buttons_[3];
  InputEvent queue_[kQueueCapacity];
  uint8_t queueHead_ = 0;
  uint8_t queueTail_ = 0;

  void pushEvent(InputEventType type, ButtonId button);
  bool popEvent(InputEvent& event);
};