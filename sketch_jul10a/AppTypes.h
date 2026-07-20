#pragma once

#include <Arduino.h>

enum SceneId : uint8_t {
  SCENE_SPLASH = 0,
  SCENE_PET = 1,
  SCENE_MENU = 2,
  SCENE_DEV_INFO = 3,
  SCENE_STATUS = 4,
  SCENE_FEED_MENU = 5,
  SCENE_FEED_PLAYBACK = 6,
};

enum MenuActionId : uint8_t {
  MENU_FEED = 0,
  MENU_PET = 1,
  MENU_PLAY = 2,
  MENU_DEV_INFO = 3,
  MENU_STATUS = 4,
  MENU_LIGHTS_TOGGLE = 5,
  MENU_BRIGHTNESS = 6,
  MENU_CLEAN = 7,
  MENU_BACK = 8,
  MENU_ACTION_COUNT = 9,
};

enum ScreenBrightnessMode : uint8_t {
  BRIGHTNESS_NORMAL = 0,
  BRIGHTNESS_DIM = 1,
};

enum ButtonId : uint8_t {
  BUTTON_PRIMARY = 0,
  BUTTON_SECONDARY = 1,
  BUTTON_TERTIARY = 2,
};

enum InputEventType : uint8_t {
  INPUT_NONE = 0,
  INPUT_SHORT_PRESS = 1,
  INPUT_LONG_PRESS = 2,
};

enum PetMood : uint8_t {
  MOOD_HAPPY = 0,
  MOOD_NEUTRAL = 1,
  MOOD_HUNGRY = 2,
  MOOD_SAD = 3,
};

enum FoodPreference : uint8_t {
  FOOD_DISLIKED = 0,
  FOOD_NEUTRAL = 1,
  FOOD_LIKED = 2,
};

enum DirtLevel : uint8_t {
  DIRT_NONE = 0,
  DIRT_SMALL = 1,
  DIRT_BIG = 2,
};

enum AccessoryId : uint8_t {
  ACCESSORY_NONE = 0,
  ACCESSORY_BOW = 1,
  ACCESSORY_CAT_EARS = 2,
  ACCESSORY_DEVIL_HORNS = 3,
  ACCESSORY_HALO = 4,
  ACCESSORY_CROWN = 5,
  ACCESSORY_SHROOM = 6,
  ACCESSORY_SPROUT = 7,
};

struct InputEvent {
  InputEventType type;
  ButtonId button;
};

struct PetState {
  uint8_t hunger;
  uint8_t happiness;
  uint8_t sleepiness;
  uint32_t ageTicks;
  PetMood mood;
  AccessoryId accessory;
};

struct AnimationState {
  uint8_t animationId;
  uint8_t frameIndex;
  uint32_t lastAdvanceMs;
};

struct AppState {
  SceneId scene;
  uint8_t menuIndex;
  MenuActionId selectedMenuAction;
  uint8_t splashAnimationId;
  uint8_t baseAnimationId;
  uint8_t faceAnimationId;
  uint8_t accessoryAnimationId;
  uint8_t feedMenuIndex;
  uint8_t selectedFoodAnimationId;
  uint8_t activeFoodAnimationId;
  uint8_t foodFrameIndex;
  uint8_t poopSmallAnimationId;
  uint8_t poopBigAnimationId;
  uint8_t reactionFaceAnimationId;
  uint8_t reactionCyclesRemaining;
  uint8_t reactionLastFrameIndex;
  uint8_t foodPreferenceCount;
  uint8_t foodPreferenceAnimationIds[24];
  uint8_t foodPreferenceValues[24];
  uint32_t splashEndMs;
  uint32_t foodLastAdvanceMs;
  uint32_t foodPlaybackEndMs;
  uint32_t petWanderNextMs;
  uint32_t nextPoopAtMs;
  uint32_t rumbleEndMs;
  uint32_t nextRumbleAllowedMs;
  uint32_t rumbleStepNextMs;
  uint16_t likedFoodCount;
  uint16_t neutralFoodCount;
  uint16_t dislikedFoodCount;
  uint8_t feedCountSinceLastPoop;
  int8_t petOffsetX;
  int8_t petTargetOffsetX;
  bool reactionActive;
  bool rumbleActive;
  bool poopPending;
  bool pendingBigPoop;
  uint8_t rumbleStepIndex;
  DirtLevel dirtLevel;
  bool lightsOn;
  bool sleeping;
  ScreenBrightnessMode brightnessMode;
  bool canClean;
  PetState pet;
  AnimationState animation;
  uint32_t lastLogicMs;
  uint32_t lastRenderMs;
  uint32_t lastSaveMs;
  uint16_t statTickCounter;
  bool dirty;
};