#include "App.h"

#include <Arduino.h>
#include <string.h>

#include "AppTypes.h"
#include "Assets.h"
#include "Config.h"
#include "Input.h"
#include "Persistence.h"
#include "Renderer.h"

namespace {

AppState gState;
InputManager gInput;
bool gRendererReady = false;
uint32_t gRendererRetryMs = 0;
constexpr uint8_t kInvalidAnimationId = 0xFF;
uint32_t gNowMs = 0;

constexpr MenuActionId kMenuOrder[] = {
  MENU_FEED,
  //MENU_PET,
  //MENU_PLAY,
  //MENU_DEV_INFO,
  MENU_STATUS,
  //MENU_LIGHTS_TOGGLE,
  //MENU_BRIGHTNESS,
  MENU_CLEAN,
  MENU_BACK,
};

uint8_t clampStat(int value) {
  return static_cast<uint8_t>(constrain(value, 0, 100));
}

bool startsWith(const char* text, const char* prefix) {
  if (text == nullptr || prefix == nullptr) {
    return false;
  }

  const size_t prefixLen = strlen(prefix);
  return strncmp(text, prefix, prefixLen) == 0;
}

uint8_t foodAnimationCount() {
  uint8_t count = 0;
  const uint8_t total = Assets::animationCount();
  for (uint8_t i = 0; i < total; ++i) {
    if (startsWith(Assets::animationName(i), "food_")) {
      ++count;
    }
  }

  return count;
}

uint8_t foodAnimationAt(uint8_t index) {
  uint8_t slot = 0;
  const uint8_t total = Assets::animationCount();
  for (uint8_t i = 0; i < total; ++i) {
    if (!startsWith(Assets::animationName(i), "food_")) {
      continue;
    }

    if (slot == index) {
      return i;
    }
    ++slot;
  }

  return Assets::splashAnimationId();
}

void normalizeFeedSelection() {
  const uint8_t count = foodAnimationCount();
  if (count == 0) {
    gState.feedMenuIndex = 0;
    gState.selectedFoodAnimationId = Assets::splashAnimationId();
    return;
  }

  gState.feedMenuIndex %= count;
  gState.selectedFoodAnimationId = foodAnimationAt(gState.feedMenuIndex);
}

uint8_t pickRandomAnimationByPrefix(const char* prefix, uint8_t fallback) {
  uint8_t selected = fallback;
  uint16_t matches = 0;
  const uint8_t count = Assets::animationCount();
  for (uint8_t i = 0; i < count; ++i) {
    const char* name = Assets::animationName(i);
    if (!startsWith(name, prefix)) {
      continue;
    }

    ++matches;
    if (random(matches) == 0) {
      selected = i;
    }
  }

  return selected;
}

uint8_t findAnimationByNamePart(const char* token, uint8_t fallback) {
  const uint8_t count = Assets::animationCount();
  for (uint8_t i = 0; i < count; ++i) {
    const char* name = Assets::animationName(i);
    if (name != nullptr && strstr(name, token) != nullptr) {
      return i;
    }
  }

  return fallback;
}

FoodPreference randomFoodPreference() {
  const long roll = random(100);
  if (roll < 20) {
    return FOOD_DISLIKED;
  }

  if (roll < 60) {
    return FOOD_NEUTRAL;
  }

  return FOOD_LIKED;
}

FoodPreference foodPreferenceFor(uint8_t animationId) {
  for (uint8_t i = 0; i < gState.foodPreferenceCount; ++i) {
    if (gState.foodPreferenceAnimationIds[i] == animationId) {
      return static_cast<FoodPreference>(gState.foodPreferenceValues[i]);
    }
  }

  const FoodPreference preference = randomFoodPreference();
  const uint8_t maxEntries = sizeof(gState.foodPreferenceAnimationIds) / sizeof(gState.foodPreferenceAnimationIds[0]);
  if (gState.foodPreferenceCount < maxEntries) {
    gState.foodPreferenceAnimationIds[gState.foodPreferenceCount] = animationId;
    gState.foodPreferenceValues[gState.foodPreferenceCount] = static_cast<uint8_t>(preference);
    ++gState.foodPreferenceCount;
  }

  return preference;
}

void updateMood();
bool isSleepBlocked();

void setDirtLevel(DirtLevel level) {
  gState.dirtLevel = level;
  gState.canClean = (level != DIRT_NONE);
}

bool happinessGainBlocked() {
  return gState.dirtLevel != DIRT_NONE;
}

void applyHappinessDelta(int delta) {
  if (delta > 0 && happinessGainBlocked()) {
    return;
  }

  gState.pet.happiness = clampStat(static_cast<int>(gState.pet.happiness) + delta);
}

bool isHungryForRumble() {
  return gState.pet.hunger <= Config::kRumbleHungerThreshold;
}

uint32_t randomInRange(uint32_t minValue, uint32_t maxValue) {
  if (maxValue <= minValue) {
    return minValue;
  }
  return minValue + static_cast<uint32_t>(random(static_cast<long>(maxValue - minValue + 1)));
}

void startPostFeedReaction(FoodPreference preference) {
  uint8_t reactionFace = findAnimationByNamePart("blobb_faces_blobb_neutral", gState.faceAnimationId);
  switch (preference) {
    case FOOD_DISLIKED:
      reactionFace = findAnimationByNamePart("blobb_faces_blobb_sad", reactionFace);
      break;
    case FOOD_LIKED:
      reactionFace = findAnimationByNamePart("blobb_faces_blobb_happy", reactionFace);
      break;
    case FOOD_NEUTRAL:
    default:
      break;
  }

  gState.reactionFaceAnimationId = reactionFace;
  gState.reactionCyclesRemaining = Config::kPostFeedReactionCycles;
  gState.reactionLastFrameIndex = gState.animation.frameIndex;
  gState.reactionActive = true;
}

void maybeSchedulePoopAfterFeed() {
  ++gState.feedCountSinceLastPoop;

  const bool forced = gState.feedCountSinceLastPoop >= Config::kPoopForceAfterFeeds;
  const bool randomTrigger = random(100) < Config::kPoopChancePct;
  if (!forced && !randomTrigger) {
    return;
  }

  gState.poopPending = true;
  gState.nextPoopAtMs = gNowMs + randomInRange(Config::kPoopDelayMinMs, Config::kPoopDelayMaxMs);

  // Usually first small then big; direct big is rare.
  if (gState.dirtLevel == DIRT_SMALL) {
    gState.pendingBigPoop = true;
  } else {
    gState.pendingBigPoop = random(100) < Config::kPoopDirectBigChancePct;
  }

  gState.feedCountSinceLastPoop = 0;
}

void applyFoodOutcome() {
  const FoodPreference preference = foodPreferenceFor(gState.activeFoodAnimationId);
  switch (preference) {
    case FOOD_DISLIKED:
      ++gState.dislikedFoodCount;
      gState.pet.hunger = clampStat(gState.pet.hunger + 10);
      applyHappinessDelta(-4);
      gState.pet.sleepiness = clampStat(gState.pet.sleepiness + Config::kSleepinessGainFeed);
      break;
    case FOOD_LIKED:
      ++gState.likedFoodCount;
      gState.pet.hunger = clampStat(gState.pet.hunger + 12);
      applyHappinessDelta(+6);
      gState.pet.sleepiness = clampStat(gState.pet.sleepiness + Config::kSleepinessGainFeed);
      break;
    case FOOD_NEUTRAL:
    default:
      ++gState.neutralFoodCount;
      gState.pet.hunger = clampStat(gState.pet.hunger + 11);
      applyHappinessDelta(+2);
      gState.pet.sleepiness = clampStat(gState.pet.sleepiness + Config::kSleepinessGainFeed);
      break;
  }

  maybeSchedulePoopAfterFeed();

  updateMood();
  startPostFeedReaction(preference);
}

void updateFaceByMood();

bool isSleepBlocked() {
  if (gState.dirtLevel != DIRT_NONE) {
    return true;
  }

  if (gState.pet.hunger <= 10) {
    return true;
  }

  return false;
}

void rollBlobbCharacter() {
  gState.baseAnimationId = pickRandomAnimationByPrefix("blobb_bases_", Assets::defaultPetAnimationId());
  gState.accessoryAnimationId = pickRandomAnimationByPrefix("blobb_accessiories_", kInvalidAnimationId);
  updateFaceByMood();
}

uint8_t chooseDefaultAnimationId() {
  return Assets::defaultPetAnimationId();
}

void updateMood() {
  if (gState.sleeping) {
    gState.pet.mood = MOOD_NEUTRAL;
    updateFaceByMood();
    return;
  }

  if (gState.pet.hunger <= 20) {
    gState.pet.mood = MOOD_HUNGRY;
    updateFaceByMood();
    return;
  }

  if (gState.pet.happiness <= 25) {
    gState.pet.mood = MOOD_SAD;
    updateFaceByMood();
    return;
  }

  if (gState.pet.hunger >= 70 && gState.pet.happiness >= 70) {
    gState.pet.mood = MOOD_HAPPY;
    updateFaceByMood();
    return;
  }

  gState.pet.mood = MOOD_NEUTRAL;
  updateFaceByMood();
}

void updateFaceByMood() {
  if (gState.sleeping) {
    gState.faceAnimationId = findAnimationByNamePart("blobb_faces_blobb_sleeping", Assets::defaultPetAnimationId());
    return;
  }

  uint8_t fallbackFace = findAnimationByNamePart("blobb_faces_blobb_neutral", Assets::defaultPetAnimationId());
  switch (gState.pet.mood) {
    case MOOD_HAPPY:
      gState.faceAnimationId = findAnimationByNamePart("blobb_faces_blobb_happy", fallbackFace);
      break;
    case MOOD_HUNGRY:
      gState.faceAnimationId = findAnimationByNamePart("blobb_faces_blobb_very_sad", fallbackFace);
      break;
    case MOOD_SAD:
      gState.faceAnimationId = findAnimationByNamePart("blobb_faces_blobb_sad", fallbackFace);
      break;
    case MOOD_NEUTRAL:
    default:
      gState.faceAnimationId = fallbackFace;
      break;
  }
}

void resetState() {
  gState.scene = SCENE_SPLASH;
  gState.menuIndex = 0;
  gState.selectedMenuAction = MENU_FEED;
  gState.splashAnimationId = Assets::splashAnimationId();
  gState.baseAnimationId = Assets::defaultPetAnimationId();
  gState.faceAnimationId = Assets::defaultPetAnimationId();
  gState.accessoryAnimationId = kInvalidAnimationId;
  gState.feedMenuIndex = 0;
  gState.selectedFoodAnimationId = Assets::splashAnimationId();
  gState.activeFoodAnimationId = Assets::splashAnimationId();
  gState.foodFrameIndex = 0;
  gState.poopSmallAnimationId = findAnimationByNamePart("object_shit_small", Assets::splashAnimationId());
  gState.poopBigAnimationId = findAnimationByNamePart("object_shit_big", gState.poopSmallAnimationId);
  gState.reactionFaceAnimationId = Assets::defaultPetAnimationId();
  gState.reactionCyclesRemaining = 0;
  gState.reactionLastFrameIndex = 0;
  gState.foodPreferenceCount = 0;
  memset(gState.foodPreferenceAnimationIds, 0, sizeof(gState.foodPreferenceAnimationIds));
  memset(gState.foodPreferenceValues, 0, sizeof(gState.foodPreferenceValues));
  gState.splashEndMs = 0;
  gState.foodLastAdvanceMs = 0;
  gState.foodPlaybackEndMs = 0;
  gState.petWanderNextMs = 0;
  gState.nextPoopAtMs = 0;
  gState.rumbleEndMs = 0;
  gState.nextRumbleAllowedMs = 0;
  gState.rumbleStepNextMs = 0;
  gState.likedFoodCount = 0;
  gState.neutralFoodCount = 0;
  gState.dislikedFoodCount = 0;
  gState.feedCountSinceLastPoop = 0;
  gState.petOffsetX = 0;
  gState.petTargetOffsetX = 0;
  gState.reactionActive = false;
  gState.rumbleActive = false;
  gState.poopPending = false;
  gState.pendingBigPoop = false;
  gState.rumbleStepIndex = 0;
  gState.dirtLevel = DIRT_NONE;
  gState.lightsOn = true;
  gState.sleeping = false;
  gState.brightnessMode = BRIGHTNESS_NORMAL;
  gState.canClean = false;
  gState.pet.hunger = 74;
  gState.pet.happiness = 78;
  gState.pet.sleepiness = 35;
  gState.pet.ageTicks = 0;
  gState.pet.mood = MOOD_HAPPY;
  gState.pet.accessory = ACCESSORY_NONE;
  gState.animation.animationId = gState.splashAnimationId;
  gState.animation.frameIndex = 0;
  gState.animation.lastAdvanceMs = 0;
  gState.lastLogicMs = 0;
  gState.lastRenderMs = 0;
  gState.lastSaveMs = 0;
  gState.statTickCounter = 0;
  gState.dirty = true;

  rollBlobbCharacter();
  normalizeFeedSelection();
}

bool actionVisible(MenuActionId action) {
  if (action == MENU_CLEAN) {
    return gState.canClean;
  }

  return true;
}

uint8_t visibleActionCount() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MENU_ACTION_COUNT; ++i) {
    if (actionVisible(kMenuOrder[i])) {
      ++count;
    }
  }
  return count;
}

MenuActionId actionAtVisibleIndex(uint8_t visibleIndex) {
  uint8_t slot = 0;
  for (uint8_t i = 0; i < MENU_ACTION_COUNT; ++i) {
    const MenuActionId action = kMenuOrder[i];
    if (!actionVisible(action)) {
      continue;
    }

    if (slot == visibleIndex) {
      return action;
    }
    ++slot;
  }

  return MENU_BACK;
}

void normalizeMenuSelection() {
  const uint8_t count = visibleActionCount();
  if (count == 0) {
    gState.menuIndex = 0;
    gState.selectedMenuAction = MENU_BACK;
    return;
  }

  gState.menuIndex %= count;
  gState.selectedMenuAction = actionAtVisibleIndex(gState.menuIndex);
}

void feedPet() {
  gState.pet.hunger = clampStat(gState.pet.hunger + 12);
  applyHappinessDelta(+3);
  gState.pet.sleepiness = clampStat(gState.pet.sleepiness + Config::kSleepinessGainFeed);
  updateMood();
  gState.dirty = true;
}

void executeMenuAction(MenuActionId action) {
  switch (action) {
    case MENU_FEED:
      normalizeFeedSelection();
      gState.scene = SCENE_FEED_MENU;
      break;
    case MENU_PET:
      applyHappinessDelta(+2);
      updateMood();
      gState.scene = SCENE_PET;
      break;
    case MENU_PLAY:
      applyHappinessDelta(+5);
      gState.pet.hunger = clampStat(gState.pet.hunger - 2);
      gState.pet.sleepiness = clampStat(gState.pet.sleepiness + Config::kSleepinessGainPlay);
      updateMood();
      gState.scene = SCENE_PET;
      break;
    case MENU_DEV_INFO:
      gState.scene = SCENE_DEV_INFO;
      break;
    case MENU_STATUS:
      gState.scene = SCENE_STATUS;
      break;
    case MENU_LIGHTS_TOGGLE:
      if (gState.lightsOn) {
        gState.lightsOn = false;
        gState.sleeping = true;
        gState.petTargetOffsetX = 0;
        gState.petWanderNextMs = gNowMs;
      } else {
        gState.lightsOn = true;
        gState.sleeping = false;
      }
      updateMood();
      gState.scene = SCENE_PET;
      break;
    case MENU_BRIGHTNESS:
      if (gState.brightnessMode == BRIGHTNESS_NORMAL) {
        gState.brightnessMode = BRIGHTNESS_DIM;
      } else {
        gState.brightnessMode = BRIGHTNESS_NORMAL;
      }
      gState.scene = SCENE_PET;
      break;
    case MENU_CLEAN:
      setDirtLevel(DIRT_NONE);
      gState.poopPending = false;
      gState.pendingBigPoop = false;
      gState.scene = SCENE_PET;
      break;
    case MENU_BACK:
    default:
      gState.scene = SCENE_PET;
      break;
  }

  normalizeMenuSelection();
  gState.dirty = true;
}

void handlePetEvent(const InputEvent& event) {
  if (event.type != INPUT_SHORT_PRESS) {
    return;
  }

  if (event.button == BUTTON_SECONDARY) {
    gState.scene = SCENE_MENU;
    normalizeMenuSelection();
    gState.dirty = true;
  }
}

void handleDetailEvent(const InputEvent& event) {
  if (event.type != INPUT_SHORT_PRESS) {
    return;
  }

  if (event.button == BUTTON_SECONDARY) {
    gState.scene = SCENE_MENU;
    normalizeMenuSelection();
    gState.dirty = true;
  }
}

void handleMenuEvent(const InputEvent& event) {
  if (event.type != INPUT_SHORT_PRESS) {
    return;
  }

  const uint8_t count = visibleActionCount();
  if (count == 0) {
    gState.scene = SCENE_PET;
    gState.dirty = true;
    return;
  }

  if (event.button == BUTTON_PRIMARY) {
    gState.menuIndex = (gState.menuIndex + count - 1) % count;
    normalizeMenuSelection();
    gState.dirty = true;
    return;
  }

  if (event.button == BUTTON_TERTIARY) {
    gState.menuIndex = (gState.menuIndex + 1) % count;
    normalizeMenuSelection();
    gState.dirty = true;
    return;
  }

  if (event.button == BUTTON_SECONDARY) {
    executeMenuAction(gState.selectedMenuAction);
  }
}

void startFeedPlayback() {
  gState.petTargetOffsetX = 0;
  gState.petWanderNextMs = gNowMs;
  gState.activeFoodAnimationId = gState.selectedFoodAnimationId;
  gState.foodFrameIndex = 0;
  gState.foodLastAdvanceMs = gNowMs;

  const AnimationDef& foodAnim = Assets::animationOrSplash(gState.activeFoodAnimationId);
  const uint8_t frameCount = (foodAnim.frameCount == 0) ? 1 : foodAnim.frameCount;
  const uint32_t duration = static_cast<uint32_t>(frameCount) * Config::kFoodAnimationTickMs * Config::kFoodPlaybackCycles;
  gState.foodPlaybackEndMs = gNowMs + duration;
  gState.scene = SCENE_FEED_PLAYBACK;
  gState.dirty = true;
}

void handleFeedMenuEvent(const InputEvent& event) {
  if (event.type != INPUT_SHORT_PRESS) {
    return;
  }

  const uint8_t count = foodAnimationCount();
  if (count == 0) {
    gState.scene = SCENE_MENU;
    gState.dirty = true;
    return;
  }

  if (event.button == BUTTON_PRIMARY) {
    gState.feedMenuIndex = (gState.feedMenuIndex + count - 1) % count;
    normalizeFeedSelection();
    gState.dirty = true;
    return;
  }

  if (event.button == BUTTON_TERTIARY) {
    gState.feedMenuIndex = (gState.feedMenuIndex + 1) % count;
    normalizeFeedSelection();
    gState.dirty = true;
    return;
  }

  if (event.button == BUTTON_SECONDARY) {
    startFeedPlayback();
  }
}

void handleFeedPlaybackEvent(const InputEvent& event) {
  if (event.type != INPUT_SHORT_PRESS || event.button != BUTTON_SECONDARY) {
    return;
  }

  gState.scene = SCENE_PET;
  gState.dirty = true;
}

void handleEvent(const InputEvent& event) {
  if (event.type == INPUT_NONE) {
    return;
  }

  if (gState.scene == SCENE_MENU) {
    handleMenuEvent(event);
    return;
  }

  if (gState.scene == SCENE_FEED_MENU) {
    handleFeedMenuEvent(event);
    return;
  }

  if (gState.scene == SCENE_FEED_PLAYBACK) {
    handleFeedPlaybackEvent(event);
    return;
  }

  if (gState.scene == SCENE_DEV_INFO || gState.scene == SCENE_STATUS) {
    handleDetailEvent(event);
    return;
  }

  handlePetEvent(event);
}

void tickStats() {
  ++gState.pet.ageTicks;
  ++gState.statTickCounter;

  if (!gState.sleeping && (gState.statTickCounter % 16) == 0) {
    gState.pet.sleepiness = clampStat(gState.pet.sleepiness + Config::kSleepinessTickGain);
  }

  if (gState.sleeping) {
    gState.pet.sleepiness = clampStat(gState.pet.sleepiness - Config::kSleepinessSleepRecover);
    if (gState.pet.sleepiness == 0 || isSleepBlocked()) {
      gState.sleeping = false;
      gState.lightsOn = true;
      updateMood();
    }
  }

  if ((gState.statTickCounter % 20) == 0) {
    gState.pet.hunger = clampStat(gState.pet.hunger - 1);
  }

  if (gState.pet.hunger <= 35 && (gState.statTickCounter % 12) == 0) {
    applyHappinessDelta(-1);
  }

  if (gState.dirtLevel == DIRT_BIG && (gState.statTickCounter % Config::kBigPoopDropEveryTicks) == 0) {
    applyHappinessDelta(-Config::kBigPoopHappinessDrop);
  }

  updateMood();
  gState.dirty = true;
}

void updatePoopState(uint32_t nowMs) {
  if (!gState.poopPending || nowMs < gState.nextPoopAtMs) {
    return;
  }

  gState.poopPending = false;

  if (gState.dirtLevel == DIRT_SMALL || gState.pendingBigPoop) {
    setDirtLevel(DIRT_BIG);
  } else {
    setDirtLevel(DIRT_SMALL);
  }

  gState.pendingBigPoop = false;
  gState.dirty = true;
}

void updatePetWander(uint32_t nowMs) {
  if (gState.scene == SCENE_SPLASH) {
    return;
  }

  if (nowMs < gState.petWanderNextMs) {
    return;
  }

  // Re-center smoothly before/while eating or sleeping.
  if (gState.sleeping || gState.scene == SCENE_FEED_PLAYBACK) {
    gState.petTargetOffsetX = 0;
    if (gState.petOffsetX < 0) {
      ++gState.petOffsetX;
      gState.dirty = true;
    } else if (gState.petOffsetX > 0) {
      --gState.petOffsetX;
      gState.dirty = true;
    }

    gState.petWanderNextMs = nowMs + 80;
    return;
  }

  if (isHungryForRumble()) {
    gState.petTargetOffsetX = 0;
    if (gState.petOffsetX < 0) {
      ++gState.petOffsetX;
      gState.dirty = true;
    } else if (gState.petOffsetX > 0) {
      --gState.petOffsetX;
      gState.dirty = true;
    }

    gState.petWanderNextMs = nowMs + 90;
    return;
  }

  int8_t activeRange = Config::kPetWanderRangePx;
  uint16_t pauseMin = Config::kPetWanderPauseMinMs;
  uint16_t pauseMax = Config::kPetWanderPauseMaxMs;
  uint16_t stepMin = Config::kPetWanderStepMinMs;
  uint16_t stepMax = Config::kPetWanderStepMaxMs;

  if (gState.pet.mood == MOOD_HAPPY) {
    // Happy pets move around more often.
    pauseMin = static_cast<uint16_t>(pauseMin * 7 / 10);
    pauseMax = static_cast<uint16_t>(pauseMax * 7 / 10);
    stepMin = static_cast<uint16_t>(stepMin * 3 / 4);
    stepMax = static_cast<uint16_t>(stepMax * 3 / 4);
  } else if (gState.pet.mood == MOOD_SAD) {
    // Sad pets move less and stay closer to center.
    activeRange = 3;
    pauseMin = static_cast<uint16_t>(pauseMin * 14 / 10);
    pauseMax = static_cast<uint16_t>(pauseMax * 14 / 10);
    stepMin = static_cast<uint16_t>(stepMin * 13 / 10);
    stepMax = static_cast<uint16_t>(stepMax * 13 / 10);
  }

  if (gState.petOffsetX == gState.petTargetOffsetX) {
    const int16_t spread = (activeRange * 2) + 1;
    const int8_t candidate = static_cast<int8_t>(static_cast<int16_t>(random(spread)) - activeRange);

    if (candidate == gState.petOffsetX) {
      gState.petWanderNextMs = nowMs + randomInRange(pauseMin, pauseMax);
      return;
    }

    gState.petTargetOffsetX = candidate;
  }

  if (gState.petOffsetX < gState.petTargetOffsetX) {
    ++gState.petOffsetX;
  } else if (gState.petOffsetX > gState.petTargetOffsetX) {
    --gState.petOffsetX;
  }

  if (gState.petOffsetX == gState.petTargetOffsetX) {
    gState.petWanderNextMs = nowMs + randomInRange(pauseMin, pauseMax);
  } else {
    gState.petWanderNextMs = nowMs + randomInRange(stepMin, stepMax);
  }

  gState.dirty = true;
}

void updateRumbleState(uint32_t nowMs) {
  if (!isHungryForRumble()) {
    gState.rumbleActive = false;
    gState.rumbleStepIndex = 0;
    gState.rumbleEndMs = 0;
    gState.rumbleStepNextMs = 0;
    gState.nextRumbleAllowedMs = nowMs;
    return;
  }

  if (gState.rumbleActive) {
    const uint32_t stepDuration = (Config::kRumbleDurationMs / 5) == 0 ? 1 : (Config::kRumbleDurationMs / 5);
    while (nowMs >= gState.rumbleStepNextMs && gState.rumbleStepIndex < 4) {
      ++gState.rumbleStepIndex;
      gState.rumbleStepNextMs += stepDuration;
      gState.dirty = true;
    }

    if (nowMs >= gState.rumbleEndMs) {
      gState.rumbleActive = false;
      gState.rumbleStepIndex = 0;
      gState.rumbleStepNextMs = 0;
      gState.nextRumbleAllowedMs = nowMs + Config::kRumbleCooldownMs;
      gState.dirty = true;
    }
    return;
  }

  if (nowMs >= gState.nextRumbleAllowedMs) {
    const uint32_t stepDuration = (Config::kRumbleDurationMs / 5) == 0 ? 1 : (Config::kRumbleDurationMs / 5);
    gState.rumbleActive = true;
    gState.rumbleStepIndex = 0;
    gState.rumbleEndMs = nowMs + Config::kRumbleDurationMs;
    gState.rumbleStepNextMs = nowMs + stepDuration;
    gState.dirty = true;
  }
}

void updateAnimation(uint32_t nowMs) {
  if ((nowMs - gState.animation.lastAdvanceMs) < Config::kAnimationTickMs) {
    return;
  }

  const AnimationDef& animation = Assets::animationAt(gState.animation.animationId);
  if (animation.frameCount == 0) {
    return;
  }

  gState.animation.lastAdvanceMs = nowMs;
  gState.animation.frameIndex = (gState.animation.frameIndex + 1) % animation.frameCount;
  gState.dirty = true;
}

void updateReactionState() {
  if (!gState.reactionActive || gState.scene != SCENE_PET) {
    return;
  }

  const uint8_t previous = gState.reactionLastFrameIndex;
  const uint8_t current = gState.animation.frameIndex;
  gState.reactionLastFrameIndex = current;

  if (current < previous && gState.reactionCyclesRemaining > 0) {
    --gState.reactionCyclesRemaining;
    if (gState.reactionCyclesRemaining == 0) {
      gState.reactionActive = false;
    }
  }
}

void updateFoodAnimation(uint32_t nowMs) {
  if (gState.scene != SCENE_FEED_PLAYBACK) {
    return;
  }

  const AnimationDef& foodAnim = Assets::animationOrSplash(gState.activeFoodAnimationId);
  if (foodAnim.frameCount == 0) {
    return;
  }

  if ((nowMs - gState.foodLastAdvanceMs) < Config::kFoodAnimationTickMs) {
    return;
  }

  gState.foodLastAdvanceMs = nowMs;
  gState.foodFrameIndex = (gState.foodFrameIndex + 1) % foodAnim.frameCount;
  gState.dirty = true;
}

void updateFeedPlayback(uint32_t nowMs) {
  if (gState.scene != SCENE_FEED_PLAYBACK) {
    return;
  }

  updateFoodAnimation(nowMs);
  if (nowMs < gState.foodPlaybackEndMs) {
    return;
  }

  applyFoodOutcome();
  gState.scene = SCENE_PET;
  gState.dirty = true;
}

void updateLogic(uint32_t nowMs) {
  while ((nowMs - gState.lastLogicMs) >= Config::kLogicTickMs) {
    gState.lastLogicMs += Config::kLogicTickMs;
    tickStats();
  }
}

void maybeSave(uint32_t nowMs) {
  if ((nowMs - gState.lastSaveMs) < Config::kAutoSaveMs) {
    return;
  }

  Persistence::save(gState);
  gState.lastSaveMs = nowMs;
}

}  // namespace

namespace App {

void begin() {
  Serial.begin(115200);
  randomSeed(micros());

  resetState();
  Persistence::begin();
  Persistence::load(gState);
  gState.pet.accessory = ACCESSORY_NONE;
  updateMood();

  gRendererReady = Renderer::begin();
  if (!gRendererReady) {
    Serial.println(F("OLED init failed, retrying..."));
  }

  gInput.begin();

  const uint32_t nowMs = millis();
  gNowMs = nowMs;
  normalizeMenuSelection();
  normalizeFeedSelection();
  gState.splashEndMs = nowMs + Config::kSplashDurationMs;
  gState.petWanderNextMs = nowMs + randomInRange(Config::kPetWanderPauseMinMs, Config::kPetWanderPauseMaxMs);
  gState.lastLogicMs = nowMs;
  gState.lastRenderMs = 0;
  gState.lastSaveMs = nowMs;
  gState.animation.lastAdvanceMs = nowMs;
  gState.dirty = true;
  gRendererRetryMs = nowMs;
}

void update() {
  const uint32_t nowMs = millis();
  gNowMs = nowMs;

  if (!gRendererReady) {
    if ((nowMs - gRendererRetryMs) >= 1000) {
      gRendererRetryMs = nowMs;
      gRendererReady = Renderer::begin();
      if (gRendererReady) {
        Serial.println(F("OLED init recovered"));
        gState.dirty = true;
      }
    }
    return;
  }

  if (gState.scene == SCENE_SPLASH) {
    updateAnimation(nowMs);

    if (nowMs >= gState.splashEndMs) {
      gState.scene = SCENE_PET;
      gState.animation.animationId = chooseDefaultAnimationId();
      gState.animation.frameIndex = 0;
      gState.animation.lastAdvanceMs = nowMs;
      gState.dirty = true;
    }

    if (!gState.dirty && (nowMs - gState.lastRenderMs) < Config::kRenderTickMs) {
      return;
    }

    Renderer::render(gState);
    gState.lastRenderMs = nowMs;
    gState.dirty = false;
    return;
  }

  InputEvent event = {INPUT_NONE, BUTTON_PRIMARY};
  while (gInput.poll(event, nowMs)) {
    handleEvent(event);
  }

  updateLogic(nowMs);
  updatePoopState(nowMs);
  updateRumbleState(nowMs);
  updatePetWander(nowMs);
  updateAnimation(nowMs);
  updateReactionState();
  updateFeedPlayback(nowMs);
  maybeSave(nowMs);

  if (!gState.dirty && (nowMs - gState.lastRenderMs) < Config::kRenderTickMs) {
    return;
  }

  Renderer::render(gState);
  gState.lastRenderMs = nowMs;
  gState.dirty = false;
}

}  // namespace App