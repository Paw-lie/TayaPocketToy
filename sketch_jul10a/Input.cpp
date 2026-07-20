#include "Input.h"

#include <Arduino.h>

#include "Config.h"

void InputManager::begin() {
  for (uint8_t index = 0; index < Config::kButtonSlots; ++index) {
    const int8_t pin = Config::kButtonPins[index];
    ButtonRuntime& button = buttons_[index];
    button.enabled = pin >= 0;
    button.stableLevel = HIGH;
    button.lastReading = HIGH;
    button.lastChangeMs = 0;
    button.pressedMs = 0;

    if (button.enabled) {
      pinMode(pin, INPUT_PULLUP);
      const bool reading = digitalRead(pin);
      button.stableLevel = reading;
      button.lastReading = reading;
    }
  }
}

bool InputManager::poll(InputEvent& event, uint32_t nowMs) {
  for (uint8_t index = 0; index < Config::kButtonSlots; ++index) {
    ButtonRuntime& button = buttons_[index];
    if (!button.enabled) {
      continue;
    }

    const bool reading = digitalRead(Config::kButtonPins[index]);
    if (reading != button.lastReading) {
      button.lastReading = reading;
      button.lastChangeMs = nowMs;
    }

    if ((nowMs - button.lastChangeMs) < Config::kDebounceMs) {
      continue;
    }

    if (reading == button.stableLevel) {
      continue;
    }

    const bool previousStableLevel = button.stableLevel;
    button.stableLevel = reading;

    if (button.stableLevel == LOW) {
      button.pressedMs = nowMs;
      continue;
    }

    if (previousStableLevel == LOW) {
      const uint32_t heldMs = nowMs - button.pressedMs;
      const InputEventType type = (heldMs >= Config::kLongPressMs) ? INPUT_LONG_PRESS : INPUT_SHORT_PRESS;
      pushEvent(type, static_cast<ButtonId>(index));
    }
  }

  return popEvent(event);
}

void InputManager::pushEvent(InputEventType type, ButtonId button) {
  const uint8_t nextHead = (queueHead_ + 1) % kQueueCapacity;
  if (nextHead == queueTail_) {
    return;
  }

  queue_[queueHead_] = {type, button};
  queueHead_ = nextHead;
}

bool InputManager::popEvent(InputEvent& event) {
  if (queueTail_ == queueHead_) {
    return false;
  }

  event = queue_[queueTail_];
  queueTail_ = (queueTail_ + 1) % kQueueCapacity;
  return true;
}