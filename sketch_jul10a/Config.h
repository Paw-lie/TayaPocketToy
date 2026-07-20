#pragma once

#include <Arduino.h>

namespace Config {

//screen dimensions
constexpr uint8_t kScreenWidth = 128;
constexpr uint8_t kScreenHeight = 64;

// The OLED reset pin. Set to -1 if not used.
constexpr int8_t kOledReset = -1;
constexpr uint8_t kScreenAddressPrimary = 0x3C;
constexpr uint8_t kScreenAddressSecondary = 0x3D;

//display sda/scl pins
constexpr int8_t kSdaPin = 26;
constexpr int8_t kSclPin = 25;

//button settings
constexpr uint8_t kButtonSlots = 3;
constexpr int8_t kButtonPins[kButtonSlots] = {33, 32, 27};


//timing settings
constexpr uint32_t kLogicTickMs = 250;
constexpr uint32_t kAnimationTickMs = 600;
constexpr uint32_t kRenderTickMs = 33;
constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kLongPressMs = 650;
constexpr uint32_t kAutoSaveMs = 15000;
constexpr uint32_t kSplashDurationMs = 2000;
constexpr uint32_t kFoodAnimationTickMs = kAnimationTickMs * 2;
constexpr uint8_t kFoodPlaybackCycles = 1;
constexpr uint8_t kPostFeedReactionCycles = 3;
constexpr uint8_t kSleepinessGainPlay = 7;
constexpr uint8_t kSleepinessGainFeed = 5;
constexpr uint8_t kSleepinessTickGain = 1;
constexpr uint8_t kSleepinessSleepRecover = 3;
constexpr int8_t kPetWanderRangePx = 7;
constexpr uint16_t kPetWanderPauseMinMs = 1400;
constexpr uint16_t kPetWanderPauseMaxMs = 4200;
constexpr uint16_t kPetWanderStepMinMs = 240;
constexpr uint16_t kPetWanderStepMaxMs = 520;
constexpr uint8_t kRumbleHungerThreshold = 10;
constexpr uint16_t kRumbleDurationMs = 2000;
constexpr uint16_t kRumbleCooldownMs = 5000;
constexpr uint8_t kPoopChancePct = 30;
constexpr uint8_t kPoopDirectBigChancePct = 10;
constexpr uint8_t kPoopForceAfterFeeds = 3;
constexpr uint16_t kPoopDelayMinMs = 8000;
constexpr uint16_t kPoopDelayMaxMs = 18000;
constexpr uint8_t kBigPoopHappinessDrop = 1;
constexpr uint16_t kBigPoopDropEveryTicks = 6;


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
