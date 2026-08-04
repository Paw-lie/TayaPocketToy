#pragma once

#include <Arduino.h>

namespace Config {

//screen dimensions
constexpr uint8_t kScreenWidth = 48;
constexpr uint8_t kScreenHeight = 84;
// Adafruit_GFX rotation: 0/2=landscape, 1/3=portrait.
constexpr uint8_t kDisplayRotation = 1;

// PCD8544 (Nokia 5110) wiring on XIAO ESP32S3 board labels.
// CLK=SCLK, DIN=MOSI, plus any free GPIO for DC/CS/RST.
constexpr int8_t kLcdClkPin = D4;
constexpr int8_t kLcdDinPin = D5;
constexpr int8_t kLcdDcPin = D2;
constexpr int8_t kLcdCsPin = D3;
constexpr int8_t kLcdRstPin = D1;
constexpr uint8_t kLcdContrastNormal = 65;
constexpr uint8_t kLcdContrastDim = 48;

// Buttons are not connected yet.
constexpr uint8_t kButtonSlots = 3;
constexpr int8_t kButtonPins[kButtonSlots] = {D7, D8, D9};


//timing settings
constexpr uint32_t kLogicTickMs = 3000;
constexpr uint32_t kAnimationTickMs = 600;
constexpr uint32_t kRenderTickMs = 600;
constexpr uint16_t kIdleLoopSleepMs = 15;
constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kLongPressMs = 600;
constexpr uint32_t kAutoSaveMs = 30000;
constexpr uint32_t kSplashDurationMs = 2000;
constexpr uint32_t kFoodAnimationTickMs = kAnimationTickMs * 2;
constexpr uint8_t kFoodPlaybackCycles = 1;
constexpr uint8_t kPostFeedReactionCycles = 3;
constexpr uint8_t kSleepinessGainPlay = 7;
constexpr uint8_t kSleepinessGainFeed = 5;
constexpr uint8_t kSleepinessTickGain = 1;
constexpr uint8_t kSleepinessSleepRecover = 3;
constexpr int8_t kPetWanderRangePx = 3;
constexpr uint16_t kPetWanderPauseMinMs = 5*1000;
constexpr uint16_t kPetWanderPauseMaxMs = 20*1000;
constexpr uint16_t kPetWanderStepMinMs = 120;
constexpr uint16_t kPetWanderStepMaxMs = 240;
constexpr int8_t kFeedPetOffsetXPx = 9;
constexpr uint32_t kToyBoredMinMs = 25000;
constexpr uint32_t kToyBoredMaxMs = 45000;
constexpr uint32_t kPetAnimMs = 1200;
constexpr uint32_t kPetOutcomeMs = 1800;
constexpr int8_t kPetSatisfiedHappinessGain = 3;
constexpr int8_t kPetWorriedHappinessDrop = 2;
constexpr uint8_t kSleepStartSleepinessThreshold = 50;  // 50% energy or lower.
constexpr uint16_t kDarkAwakeBoredTicks = 8;            // With 3s logic ticks: ~24s in dark while awake.
constexpr int8_t kDarkAwakeBoredHappinessDrop = 1;
constexpr uint8_t kRumbleHungerThreshold = 10;
constexpr uint16_t kRumbleDurationMs = 2000;
constexpr uint16_t kRumbleCooldownMs = 5000;
constexpr uint8_t kPoopChancePct = 30;
constexpr uint8_t kPoopForceAfterFeeds = 5; // Force a poop after this many feedings, if none has occurred yet.
constexpr uint16_t kPoopDelayMinMs = 5000;
constexpr uint16_t kPoopDelayMaxMs = 15000;
constexpr uint8_t kBigPoopHappinessDrop = 1;
constexpr uint16_t kBigPoopDropEveryTicks = 2;


//pet settings
constexpr int16_t kPetX = 0;
constexpr int16_t kPetY = 0;
constexpr uint8_t kPetScale = 1;
constexpr bool kEnableAccessories = true;

/// @brief The namespace and key used for saving/loading the pet state in non-volatile storage.
constexpr char kSaveNamespace[] = "vtpet";
constexpr char kSaveKey[] = "state";
constexpr uint8_t kSaveVersion = 1;

}  // namespace Config
