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
AppState gPreDemoState;
bool gHasPreDemoState = false;
uint32_t gDemoStartMs = 0;
InputManager gInput;
bool gRendererReady = false;
uint32_t gRendererRetryMs = 0;
constexpr uint8_t kInvalidAnimationId = 0xFF;
uint32_t gNowMs = 0;
constexpr uint8_t kDemoUnlockWraps = 10;

constexpr MenuActionId kMenuOrder[] = {
  MENU_FEED,
  MENU_PET,
  MENU_PLAY,
  //MENU_DEV_INFO,
  MENU_STATUS,
  MENU_LIGHTS_TOGGLE,
  //MENU_BRIGHTNESS,
  MENU_CLEAN,
  MENU_DEMO,
  MENU_EXIT_DEMO,
  MENU_BACK,
};
constexpr uint8_t kMenuOrderCount = static_cast<uint8_t>(sizeof(kMenuOrder) / sizeof(kMenuOrder[0]));

constexpr uint16_t kEggStageDurationMs = 4500;
constexpr uint16_t kHatchingStageDurationMs = 3800;
constexpr uint16_t kDyingStageDurationMs = 2600;
constexpr uint16_t kRebirthStageDurationMs = 4200;
constexpr uint16_t kEvolutionWindowTicks = 60;       // 60 * 1000ms = 60s
constexpr uint8_t kEvolutionHappyThreshold = 70;
constexpr uint16_t kEvolutionLowHappinessTicksForPawlie = 20;
constexpr uint16_t kDeathByHungerTicks = 9;          // 9s continuous hunger=0
constexpr uint16_t kDeathByHappinessTicks = 12;      // 12s continuous happiness=0
constexpr uint16_t kDeathBySleepinessTicks = 12;     // 12s continuous max exhaustion
constexpr uint32_t kDeathByAgeTicks = 900;           // 15 minutes in logic ticks

uint32_t stageDurationForFullAnimationCycle(uint8_t animationId, uint32_t minimumMs) {
  const AnimationDef& animation = Assets::animationOrSplash(animationId);
  const uint32_t cycleMs = static_cast<uint32_t>(max<uint8_t>(animation.frameCount, 1)) * Config::kAnimationTickMs;
  return max(minimumMs, cycleMs);
}

void updateMood();
void updateFaceByMood();
void updateNeedIndicator();
void updateIndicatorState(uint32_t nowMs);
void enterLifeStage(LifeStage stage, uint32_t nowMs);
void beginNewLifeCycle(uint32_t nowMs);
void resolveEvolutionIfNeeded();
void updateLifeStage(uint32_t nowMs);
void updateEvolutionWindowTracking();
void updateDeathTracking();
void updateToyState(uint32_t nowMs);
bool isAliveAndInteractive();
void applyDemoEmotion();
void applyDemoCharacter();
void enterDemoMode(uint32_t nowMs);
void exitDemoMode(uint32_t nowMs);
uint16_t computeLoopSleepMs(uint32_t nowMs);

uint32_t shiftTimestamp(uint32_t stamp, uint32_t deltaMs) {
  if (stamp == 0) {
    return 0;
  }
  return stamp + deltaMs;
}

uint8_t chooseWorstNeedIndicator() {
  if (!isAliveAndInteractive()) {
    return kInvalidAnimationId;
  }

  uint8_t selected = kInvalidAnimationId;
  uint8_t severity = 0;

  if (gState.dirtLevel == DIRT_BIG) {
    selected = gState.indicatorSickAnimationId;
    severity = 4;
  }

  if (gState.pet.hunger <= 8 && severity < 3) {
    selected = gState.indicatorHungryAnimationId;
    severity = 3;
  }

  if (gState.pet.sleepiness >= 92 && severity < 3) {
    selected = gState.indicatorTiredAnimationId;
    severity = 3;
  }

  if (gState.pet.happiness <= 8 && severity < 2) {
    selected = gState.indicatorWorriedAnimationId;
    severity = 2;
  }

  return selected;
}

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

uint8_t toyAnimationCount() {
  uint8_t count = 0;
  const uint8_t total = Assets::animationCount();
  for (uint8_t i = 0; i < total; ++i) {
    if (startsWith(Assets::animationName(i), "toys_")) {
      ++count;
    }
  }

  return count;
}

uint8_t toyAnimationAt(uint8_t index) {
  uint8_t slot = 0;
  const uint8_t total = Assets::animationCount();
  for (uint8_t i = 0; i < total; ++i) {
    if (!startsWith(Assets::animationName(i), "toys_")) {
      continue;
    }

    if (slot == index) {
      return i;
    }
    ++slot;
  }

  return Assets::splashAnimationId();
}

void normalizeToySelection() {
  const uint8_t count = toyAnimationCount();
  if (count == 0) {
    gState.toyMenuIndex = 0;
    gState.selectedToyAnimationId = Assets::splashAnimationId();
    return;
  }

  gState.toyMenuIndex %= count;
  gState.selectedToyAnimationId = toyAnimationAt(gState.toyMenuIndex);
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

bool isTaiyakiFood(uint8_t animationId) {
  const char* name = Assets::animationName(animationId);
  return (name != nullptr) && (strstr(name, "food_taiyaki") != nullptr);
}

bool isAliveAndInteractive() {
  return gState.lifeStage == LIFE_BLOBB || gState.lifeStage == LIFE_CHARACTER;
}

bool canSleepNow() {
  if (!isAliveAndInteractive()) {
    return false;
  }

  if (gState.lightsOn) {
    return false;
  }

  if (gState.pet.sleepiness < Config::kSleepStartSleepinessThreshold) {
    return false;
  }

  return true;
}

CharacterForm randomCharacterForm() {
  const CharacterForm options[] = {
    FORM_CHIINO,
    FORM_PAWLIE,
    FORM_TAIYAKI,
    FORM_TOFU,
    FORM_YSHAAR,
  };
  const uint8_t pick = static_cast<uint8_t>(random(sizeof(options) / sizeof(options[0])));
  return options[pick];
}

CharacterForm resolveEvolutionByFactorsTemplate() {
  // Evolution policy template:
  // 1) Keep each candidate in a self-contained block.
  // 2) Read only from gState evolution counters and pet stats.
  // 3) Return FORM_NONE to allow later candidates to decide.
  // 4) Fallback should remain random for unknown future states.

  // Candidate: Taiyaki form
  // Condition: all feeds in the window are Taiyakis and happiness never dropped below threshold.
  if (gState.totalFoodCount > 0 &&
      gState.totalFoodCount == gState.taiyakiFoodCount &&
      gState.evolutionHappinessAlwaysAboveThreshold) {
    return FORM_TAIYAKI;
  }

  // Candidate: Pawlie form
  // Condition: happiness was low frequently during the evolution window.
  if (gState.evolutionLowHappinessTicks >= kEvolutionLowHappinessTicksForPawlie) {
    return FORM_PAWLIE;
  }

  // Future extension example:
  // if (someConditionBasedOnHungerAndSleep) {
  //   return FORM_TOFU;
  // }

  return FORM_NONE;
}

void applyCharacterForm(CharacterForm form) {
  if (form == FORM_NONE || form == FORM_BLOBB) {
    form = randomCharacterForm();
  }

  gState.characterForm = form;
  gState.lifeStageAnimationId = Assets::characterAnimationId(form);
  gState.baseAnimationId = gState.lifeStageAnimationId;
  gState.accessoryAnimationId = kInvalidAnimationId;
  gState.reactionActive = false;
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

  // Two-stage dirt progression: none -> small -> big.
  gState.pendingBigPoop = (gState.dirtLevel == DIRT_SMALL);

  gState.feedCountSinceLastPoop = 0;
}

void applyFoodOutcome() {
  const FoodPreference preference = foodPreferenceFor(gState.activeFoodAnimationId);
  ++gState.totalFoodCount;
  if (isTaiyakiFood(gState.activeFoodAnimationId)) {
    ++gState.taiyakiFoodCount;
  }

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
  gState.characterForm = FORM_BLOBB;
  gState.baseAnimationId = pickRandomAnimationByPrefix("blobb_bases_", Assets::defaultPetAnimationId());
  gState.accessoryAnimationId = pickRandomAnimationByPrefix("blobb_accessiories_", kInvalidAnimationId);
  gState.lifeStageAnimationId = gState.baseAnimationId;
  updateFaceByMood();
}

uint8_t chooseDefaultAnimationId() {
  return gState.baseAnimationId;
}

void updateMood() {
  if (gState.demoModeActive) {
    updateFaceByMood();
    updateNeedIndicator();
    return;
  }

  if (!isAliveAndInteractive()) {
    updateNeedIndicator();
    return;
  }

  if (gState.sleeping) {
    gState.pet.mood = MOOD_NEUTRAL;
    updateFaceByMood();
    updateNeedIndicator();
    return;
  }

  const bool depletedHunger = gState.pet.hunger <= 20;
  const bool depletedSleep = gState.pet.sleepiness >= 70;
  const bool depletedHappiness = gState.pet.happiness <= 25;
  const bool dirtyAndUnhappy = (gState.dirtLevel == DIRT_BIG) && (gState.pet.happiness <= 40);

  if (depletedHunger) {
    gState.pet.mood = MOOD_HUNGRY;
    updateFaceByMood();
    updateNeedIndicator();
    return;
  }

  if (depletedHappiness || depletedSleep || dirtyAndUnhappy) {
    gState.pet.mood = MOOD_SAD;
    updateFaceByMood();
    updateNeedIndicator();
    return;
  }

  if (gState.pet.hunger >= 70 && gState.pet.happiness >= 70 && gState.pet.sleepiness <= 40 && gState.dirtLevel == DIRT_NONE) {
    gState.pet.mood = MOOD_HAPPY;
    updateFaceByMood();
    updateNeedIndicator();
    return;
  }

  gState.pet.mood = MOOD_NEUTRAL;
  updateFaceByMood();
  updateNeedIndicator();
}

void updateNeedIndicator() {
  if (gState.indicatorOverrideAnimationId != kInvalidAnimationId) {
    gState.activeIndicatorAnimationId = gState.indicatorOverrideAnimationId;
    return;
  }

  if (gState.demoModeActive) {
    gState.activeIndicatorAnimationId = gState.sleeping ? gState.indicatorNightAnimationId : kInvalidAnimationId;
    return;
  }

  gState.activeIndicatorAnimationId = kInvalidAnimationId;

  if (gState.lifeStage == LIFE_TOMBSTONE) {
    if (gState.tombstoneNightIndicator) {
      gState.activeIndicatorAnimationId = gState.indicatorNightAnimationId;
    }
    return;
  }

  if (!isAliveAndInteractive()) {
    return;
  }

  // Show a need icon only after needs have degraded beyond basic sad/hungry mood levels.
  if (gState.pet.mood != MOOD_HUNGRY && gState.pet.mood != MOOD_SAD) {
    return;
  }

  gState.activeIndicatorAnimationId = chooseWorstNeedIndicator();
}

void updateIndicatorState(uint32_t nowMs) {
  if (gState.indicatorOverrideAnimationId != kInvalidAnimationId && nowMs >= gState.indicatorOverrideUntilMs) {
    gState.indicatorOverrideAnimationId = kInvalidAnimationId;
    gState.indicatorOverrideUntilMs = 0;
    updateNeedIndicator();
    gState.dirty = true;
  }

  if (gState.pendingPetOutcomeIndicatorId == kInvalidAnimationId || nowMs < gState.pendingPetOutcomeAtMs) {
    return;
  }

  gState.indicatorOverrideAnimationId = gState.pendingPetOutcomeIndicatorId;
  gState.indicatorOverrideUntilMs = gState.pendingPetOutcomeUntilMs;
  gState.pendingPetOutcomeIndicatorId = kInvalidAnimationId;
  gState.pendingPetOutcomeAtMs = 0;
  gState.pendingPetOutcomeUntilMs = 0;
  updateNeedIndicator();
  gState.dirty = true;
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

void enterLifeStage(LifeStage stage, uint32_t nowMs) {
  gState.lifeStage = stage;
  gState.animation.frameIndex = 0;
  gState.animation.lastAdvanceMs = nowMs;
  gState.petTargetOffsetX = 0;
  gState.petOffsetX = 0;
  gState.reactionActive = false;
  gState.darkAwakeTicks = 0;
  gState.scene = SCENE_PET;

  switch (stage) {
    case LIFE_EGG:
      gState.lifeStageAnimationId = gState.eggAnimationId;
      gState.baseAnimationId = gState.lifeStageAnimationId;
      gState.accessoryAnimationId = kInvalidAnimationId;
      gState.lifeStageEndMs = nowMs + kEggStageDurationMs;
      gState.activeToyAnimationId = kInvalidAnimationId;
      gState.toyBoredAtMs = 0;
      break;
    case LIFE_HATCHING:
      gState.lifeStageAnimationId = gState.hatchAnimationId;
      gState.baseAnimationId = gState.lifeStageAnimationId;
      gState.accessoryAnimationId = kInvalidAnimationId;
      gState.lifeStageEndMs = nowMs + stageDurationForFullAnimationCycle(gState.hatchAnimationId, kHatchingStageDurationMs);
      gState.activeToyAnimationId = kInvalidAnimationId;
      gState.toyBoredAtMs = 0;
      break;
    case LIFE_BLOBB:
      rollBlobbCharacter();
      gState.lifeStageEndMs = 0;
      gState.evolutionWindowTicks = 0;
      gState.evolutionLowHappinessTicks = 0;
      gState.evolutionMinHappinessObserved = 100;
      gState.evolutionHappinessAlwaysAboveThreshold = true;
      gState.evolutionResolved = false;
      break;
    case LIFE_CHARACTER:
      gState.lifeStageEndMs = 0;
      gState.deathHungerZeroTicks = 0;
      gState.deathHappinessZeroTicks = 0;
      gState.deathSleepinessHighTicks = 0;
      break;
    case LIFE_DYING:
      gState.sleeping = false;
      gState.lightsOn = true;
      gState.lifeStageEndMs = nowMs + kDyingStageDurationMs;
      gState.lifeStageAnimationId = gState.rebirthAnimationId;
      gState.baseAnimationId = gState.lifeStageAnimationId;
      gState.accessoryAnimationId = kInvalidAnimationId;
      break;
    case LIFE_REBIRTHING:
      gState.lifeStageAnimationId = gState.rebirthAnimationId;
      gState.baseAnimationId = gState.lifeStageAnimationId;
      gState.accessoryAnimationId = kInvalidAnimationId;
      gState.lifeStageEndMs = nowMs + stageDurationForFullAnimationCycle(gState.rebirthAnimationId, kRebirthStageDurationMs);
      gState.activeToyAnimationId = kInvalidAnimationId;
      gState.toyBoredAtMs = 0;
      break;
    case LIFE_TOMBSTONE:
      gState.sleeping = false;
      gState.lightsOn = true;
      gState.lifeStageAnimationId = gState.tombstoneAnimationId;
      gState.baseAnimationId = gState.lifeStageAnimationId;
      gState.accessoryAnimationId = kInvalidAnimationId;
      gState.tombstoneNightIndicator = random(2) == 0;
      gState.lifeStageEndMs = 0;
      gState.activeToyAnimationId = kInvalidAnimationId;
      gState.toyBoredAtMs = 0;
      break;
  }

  gState.animation.animationId = gState.lifeStageAnimationId;
  updateNeedIndicator();
  gState.dirty = true;
}

void beginNewLifeCycle(uint32_t nowMs) {
  gState.pet.hunger = 74;
  gState.pet.happiness = 78;
  gState.pet.sleepiness = 35;
  gState.pet.ageTicks = 0;
  gState.pet.mood = MOOD_HAPPY;

  gState.totalFoodCount = 0;
  gState.taiyakiFoodCount = 0;
  gState.likedFoodCount = 0;
  gState.neutralFoodCount = 0;
  gState.dislikedFoodCount = 0;
  gState.foodPreferenceCount = 0;
  memset(gState.foodPreferenceAnimationIds, 0, sizeof(gState.foodPreferenceAnimationIds));
  memset(gState.foodPreferenceValues, 0, sizeof(gState.foodPreferenceValues));

  gState.feedCountSinceLastPoop = 0;
  gState.poopPending = false;
  gState.pendingBigPoop = false;
  setDirtLevel(DIRT_NONE);

  gState.deathHungerZeroTicks = 0;
  gState.deathHappinessZeroTicks = 0;
  gState.deathSleepinessHighTicks = 0;
  gState.darkAwakeTicks = 0;
  gState.sleeping = false;
  gState.lightsOn = true;
  gState.activeToyAnimationId = kInvalidAnimationId;
  gState.toyBoredAtMs = 0;
  gState.petInteractionStage = 0;
  gState.indicatorOverrideAnimationId = kInvalidAnimationId;
  gState.indicatorOverrideUntilMs = 0;
  gState.pendingPetOutcomeIndicatorId = kInvalidAnimationId;
  gState.pendingPetOutcomeAtMs = 0;
  gState.pendingPetOutcomeUntilMs = 0;
  gState.demoModeActive = false;
  gState.demoEmotionIndex = 0;
  gState.demoCharacterIndex = 0;

  updateMood();
  enterLifeStage(LIFE_EGG, nowMs);
}

void applyDemoCharacter() {
  if (!gState.demoModeActive) {
    return;
  }

  switch (gState.demoCharacterIndex % 7) {
    case 0:
      gState.characterForm = FORM_BLOBB;
      gState.lifeStage = LIFE_BLOBB;
      rollBlobbCharacter();
      break;
    case 1:
      gState.characterForm = FORM_CHIINO;
      gState.lifeStage = LIFE_CHARACTER;
      applyCharacterForm(FORM_CHIINO);
      break;
    case 2:
      gState.characterForm = FORM_PAWLIE;
      gState.lifeStage = LIFE_CHARACTER;
      applyCharacterForm(FORM_PAWLIE);
      break;
    case 3:
      gState.characterForm = FORM_TAIYAKI;
      gState.lifeStage = LIFE_CHARACTER;
      applyCharacterForm(FORM_TAIYAKI);
      break;
    case 4:
      gState.characterForm = FORM_TOFU;
      gState.lifeStage = LIFE_CHARACTER;
      applyCharacterForm(FORM_TOFU);
      break;
    case 5:
      gState.characterForm = FORM_YSHAAR;
      gState.lifeStage = LIFE_CHARACTER;
      applyCharacterForm(FORM_YSHAAR);
      break;
    case 6:
    default:
      gState.characterForm = FORM_NONE;
      gState.lifeStage = LIFE_CHARACTER;
      gState.baseAnimationId = gState.splashAnimationId;
      gState.lifeStageAnimationId = gState.baseAnimationId;
      gState.accessoryAnimationId = kInvalidAnimationId;
      gState.reactionActive = false;
      break;
  }

  gState.animation.animationId = gState.baseAnimationId;
  gState.animation.frameIndex = 0;
  gState.animation.lastAdvanceMs = gNowMs;
  gState.dirty = true;
}

uint8_t demoCharacterEmotionAnimation(CharacterForm form, uint8_t emotionIndex, uint8_t fallback) {
  const bool isHappy = (emotionIndex == 1);
  const bool isSad = (emotionIndex == 2);
  const bool isSleeping = (emotionIndex == 3);

  const char* token = nullptr;
  const char* fallbackToken = nullptr;

  switch (form) {
    case FORM_CHIINO:
      if (isHappy) {
        token = "character_chiino_chiino_happy";
      } else if (isSad) {
        token = "character_chiino_chiino_down";
      } else if (isSleeping) {
        token = "character_chiino_chiino_sleeping";
      } else {
        token = "character_chiino_chiino_idle";
      }
      fallbackToken = "character_chiino_chiino_idle";
      break;
    case FORM_PAWLIE:
      if (isHappy) {
        token = "character_pawlie_pawlie_happy";
      } else if (isSad) {
        token = "character_pawlie_pawlie_down";
      } else if (isSleeping) {
        token = "character_pawlie_pawlie_sleeping";
      } else {
        token = "character_pawlie_pawlie_idle";
      }
      fallbackToken = "character_pawlie_pawlie_idle";
      break;
    case FORM_TAIYAKI:
      if (isHappy) {
        token = "character_taiykai_taiyaki_happy";
      } else if (isSad) {
        token = "character_taiykai_taiyaki_down";
      } else if (isSleeping) {
        token = "character_taiykai_taiyaki_sleeping";
      } else {
        token = "character_taiykai_taiyaki_idle";
      }
      fallbackToken = "character_taiykai_taiyaki_idle";
      break;
    case FORM_TOFU:
      if (isHappy) {
        token = "character_tofu_tofu_happy";
      } else if (isSad) {
        token = "character_tofu_tofu_down";
      } else if (isSleeping) {
        token = "character_tofu_tofu_sleeping";
      } else {
        token = "character_tofu_tofu_idle";
      }
      fallbackToken = "character_tofu_tofu_idle";
      break;
    case FORM_YSHAAR:
      if (isHappy) {
        token = "character_yshaar_yshaar_happy";
      } else if (isSad) {
        token = "character_yshaar_yshaar_down";
      } else if (isSleeping) {
        token = "character_yshaar_yshaar_sleeping";
      } else {
        token = "character_yshaar_yshaar_idle";
      }
      fallbackToken = "character_yshaar_yshaar_idle";
      break;
    default:
      return fallback;
  }

  uint8_t resolved = findAnimationByNamePart(token, fallback);
  if (resolved == fallback && fallbackToken != nullptr) {
    resolved = findAnimationByNamePart(fallbackToken, fallback);
  }
  return resolved;
}

void applyDemoEmotion() {
  if (!gState.demoModeActive) {
    return;
  }

  const uint8_t mode = gState.demoEmotionIndex % 4;
  switch (mode) {
    case 0:
      gState.sleeping = false;
      gState.lightsOn = true;
      gState.pet.mood = MOOD_NEUTRAL;
      break;
    case 1:
      gState.sleeping = false;
      gState.lightsOn = true;
      gState.pet.mood = MOOD_HAPPY;
      break;
    case 2:
      gState.sleeping = false;
      gState.lightsOn = true;
      gState.pet.mood = MOOD_SAD;
      break;
    case 3:
    default:
      gState.sleeping = true;
      gState.lightsOn = false;
      gState.pet.mood = MOOD_NEUTRAL;
      break;
  }

  if (gState.characterForm != FORM_BLOBB && gState.lifeStage == LIFE_CHARACTER) {
    gState.baseAnimationId = demoCharacterEmotionAnimation(gState.characterForm, mode, gState.baseAnimationId);
    gState.lifeStageAnimationId = gState.baseAnimationId;
    gState.animation.animationId = gState.baseAnimationId;
    gState.animation.frameIndex = 0;
    gState.animation.lastAdvanceMs = gNowMs;
  }

  gState.indicatorOverrideAnimationId = kInvalidAnimationId;
  gState.indicatorOverrideUntilMs = 0;
  gState.pendingPetOutcomeIndicatorId = kInvalidAnimationId;
  gState.pendingPetOutcomeAtMs = 0;
  gState.pendingPetOutcomeUntilMs = 0;
  updateMood();
  gState.dirty = true;
}

uint16_t computeLoopSleepMs(uint32_t nowMs) {
  constexpr uint16_t kMaxLoopSleepMs = 120;
  constexpr uint16_t kMaxSleepingLoopSleepMs = 220;
  const uint16_t baseSleep = Config::kIdleLoopSleepMs;

  // Keep menu/detail interaction responsive by avoiding long adaptive sleeps.
  if (gState.scene == SCENE_MENU ||
      gState.scene == SCENE_FEED_MENU ||
      gState.scene == SCENE_TOY_MENU ||
      gState.scene == SCENE_DEV_INFO ||
      gState.scene == SCENE_STATUS) {
    return baseSleep;
  }

  const uint16_t maxSleep = gState.sleeping ? kMaxSleepingLoopSleepMs : kMaxLoopSleepMs;

  uint32_t nextDeadline = nowMs + maxSleep;
  auto considerDeadline = [&](uint32_t stamp) {
    if (stamp > nowMs && stamp < nextDeadline) {
      nextDeadline = stamp;
    }
  };

  considerDeadline(gState.lastLogicMs + Config::kLogicTickMs);
  considerDeadline(gState.animation.lastAdvanceMs + Config::kAnimationTickMs);

  if (gState.scene == SCENE_SPLASH) {
    considerDeadline(gState.splashEndMs);
  }

  if (gState.scene == SCENE_FEED_PLAYBACK) {
    considerDeadline(gState.foodLastAdvanceMs + Config::kFoodAnimationTickMs);
    considerDeadline(gState.foodPlaybackEndMs);
  }

  if (gState.lifeStageEndMs != 0) {
    considerDeadline(gState.lifeStageEndMs);
  }

  considerDeadline(gState.petWanderNextMs);

  if (gState.poopPending) {
    considerDeadline(gState.nextPoopAtMs);
  }

  if (gState.rumbleActive) {
    considerDeadline(gState.rumbleStepNextMs);
    considerDeadline(gState.rumbleEndMs);
  }

  if (gState.toyBoredAtMs != 0) {
    considerDeadline(gState.toyBoredAtMs);
  }

  if (gState.indicatorOverrideUntilMs != 0) {
    considerDeadline(gState.indicatorOverrideUntilMs);
  }

  if (gState.pendingPetOutcomeAtMs != 0) {
    considerDeadline(gState.pendingPetOutcomeAtMs);
  }

  if (!gState.demoModeActive) {
    considerDeadline(gState.lastSaveMs + Config::kAutoSaveMs);
  }

  if (nextDeadline <= nowMs) {
    return baseSleep;
  }

  const uint32_t delta = nextDeadline - nowMs;
  if (delta < baseSleep) {
    return baseSleep;
  }

  return static_cast<uint16_t>(delta);
}

void enterDemoMode(uint32_t nowMs) {
  if (gState.demoModeActive) {
    return;
  }

  gPreDemoState = gState;
  gHasPreDemoState = true;
  gDemoStartMs = nowMs;

  gState.demoModeActive = true;
  gState.scene = SCENE_PET;
  gState.petTargetOffsetX = 0;
  gState.petWanderNextMs = nowMs;
  gState.demoCharacterIndex = 0;
  gState.demoEmotionIndex = 0;
  gState.darkAwakeTicks = 0;
  applyDemoCharacter();
  applyDemoEmotion();
}

void exitDemoMode(uint32_t nowMs) {
  if (!gState.demoModeActive) {
    return;
  }

  const bool unlocked = gState.demoUnlocked;
  const uint8_t wrapCount = gState.demoMenuWrapCount;
  const uint32_t elapsedMs = nowMs - gDemoStartMs;

  if (gHasPreDemoState) {
    AppState restored = gPreDemoState;

    restored.splashEndMs = shiftTimestamp(restored.splashEndMs, elapsedMs);
    restored.lifeStageEndMs = shiftTimestamp(restored.lifeStageEndMs, elapsedMs);
    restored.foodLastAdvanceMs = shiftTimestamp(restored.foodLastAdvanceMs, elapsedMs);
    restored.foodPlaybackEndMs = shiftTimestamp(restored.foodPlaybackEndMs, elapsedMs);
    restored.toyBoredAtMs = shiftTimestamp(restored.toyBoredAtMs, elapsedMs);
    restored.indicatorOverrideUntilMs = shiftTimestamp(restored.indicatorOverrideUntilMs, elapsedMs);
    restored.pendingPetOutcomeAtMs = shiftTimestamp(restored.pendingPetOutcomeAtMs, elapsedMs);
    restored.pendingPetOutcomeUntilMs = shiftTimestamp(restored.pendingPetOutcomeUntilMs, elapsedMs);
    restored.petWanderNextMs = shiftTimestamp(restored.petWanderNextMs, elapsedMs);
    restored.nextPoopAtMs = shiftTimestamp(restored.nextPoopAtMs, elapsedMs);
    restored.rumbleEndMs = shiftTimestamp(restored.rumbleEndMs, elapsedMs);
    restored.nextRumbleAllowedMs = shiftTimestamp(restored.nextRumbleAllowedMs, elapsedMs);
    restored.rumbleStepNextMs = shiftTimestamp(restored.rumbleStepNextMs, elapsedMs);
    restored.animation.lastAdvanceMs = nowMs;
    restored.lastLogicMs = nowMs;
    restored.lastRenderMs = nowMs;
    restored.lastSaveMs = nowMs;
    restored.demoModeActive = false;
    restored.demoUnlocked = unlocked;
    restored.demoMenuWrapCount = wrapCount;
    restored.dirty = true;
    gState = restored;
  } else {
    gState.demoModeActive = false;
    gState.scene = SCENE_PET;
    gState.dirty = true;
  }

  gHasPreDemoState = false;
}

void resolveEvolutionIfNeeded() {
  if (gState.lifeStage != LIFE_BLOBB || gState.evolutionResolved) {
    return;
  }

  if (gState.evolutionWindowTicks < kEvolutionWindowTicks) {
    return;
  }

  CharacterForm resolved = resolveEvolutionByFactorsTemplate();
  if (resolved == FORM_NONE) {
    resolved = randomCharacterForm();
  }

  gState.evolutionResolved = true;
  applyCharacterForm(resolved);
  enterLifeStage(LIFE_CHARACTER, gNowMs);
}

void updateLifeStage(uint32_t nowMs) {
  if (gState.lifeStage == LIFE_EGG && nowMs >= gState.lifeStageEndMs) {
    enterLifeStage(LIFE_HATCHING, nowMs);
    return;
  }

  if (gState.lifeStage == LIFE_HATCHING && nowMs >= gState.lifeStageEndMs) {
    enterLifeStage(LIFE_BLOBB, nowMs);
    return;
  }

  if (gState.lifeStage == LIFE_DYING && nowMs >= gState.lifeStageEndMs) {
    enterLifeStage(LIFE_REBIRTHING, nowMs);
    return;
  }

  if (gState.lifeStage == LIFE_REBIRTHING && nowMs >= gState.lifeStageEndMs) {
    beginNewLifeCycle(nowMs);
  }
}

void resetState() {
  gState.scene = SCENE_SPLASH;
  gState.lifeStage = LIFE_EGG;
  gState.characterForm = FORM_NONE;
  gState.menuIndex = 0;
  gState.selectedMenuAction = MENU_FEED;
  gState.splashAnimationId = Assets::splashAnimationId();
  gState.eggAnimationId = Assets::eggAnimationId();
  gState.hatchAnimationId = Assets::hatchAnimationId();
  gState.rebirthAnimationId = Assets::rebirthAnimationId();
  gState.lifeStageAnimationId = gState.eggAnimationId;
  gState.baseAnimationId = Assets::defaultPetAnimationId();
  gState.faceAnimationId = Assets::defaultPetAnimationId();
  gState.accessoryAnimationId = kInvalidAnimationId;
  gState.feedMenuIndex = 0;
  gState.toyMenuIndex = 0;
  gState.selectedFoodAnimationId = Assets::splashAnimationId();
  gState.selectedToyAnimationId = Assets::splashAnimationId();
  gState.activeFoodAnimationId = Assets::splashAnimationId();
  gState.activeToyAnimationId = kInvalidAnimationId;
  gState.foodFrameIndex = 0;
  gState.poopSmallAnimationId = findAnimationByNamePart("object_shit_small", Assets::splashAnimationId());
  gState.poopBigAnimationId = findAnimationByNamePart("object_shit_big", gState.poopSmallAnimationId);
  gState.tombstoneAnimationId = findAnimationByNamePart("tombstone_tombstone", Assets::defaultPetAnimationId());
  gState.indicatorHungryAnimationId = findAnimationByNamePart("indicator_hungry", Assets::defaultPetAnimationId());
  gState.indicatorTiredAnimationId = findAnimationByNamePart("indicator_tired", gState.indicatorHungryAnimationId);
  gState.indicatorBoredAnimationId = findAnimationByNamePart("indicator_bored", gState.indicatorHungryAnimationId);
  gState.indicatorWorriedAnimationId = findAnimationByNamePart("indicator_worried", gState.indicatorHungryAnimationId);
  gState.indicatorSickAnimationId = findAnimationByNamePart("indicator_sick", gState.indicatorHungryAnimationId);
  gState.indicatorNightAnimationId = findAnimationByNamePart("indicator_night", gState.indicatorHungryAnimationId);
  gState.indicatorPetAnimationId = findAnimationByNamePart("indicator_pet", gState.indicatorHungryAnimationId);
  gState.indicatorSatisfiedAnimationId = findAnimationByNamePart("indicator_satisfied", gState.indicatorHungryAnimationId);
  gState.activeIndicatorAnimationId = kInvalidAnimationId;
  gState.indicatorOverrideAnimationId = kInvalidAnimationId;
  gState.pendingPetOutcomeIndicatorId = kInvalidAnimationId;
  gState.reactionFaceAnimationId = Assets::defaultPetAnimationId();
  gState.reactionCyclesRemaining = 0;
  gState.reactionLastFrameIndex = 0;
  gState.foodPreferenceCount = 0;
  memset(gState.foodPreferenceAnimationIds, 0, sizeof(gState.foodPreferenceAnimationIds));
  memset(gState.foodPreferenceValues, 0, sizeof(gState.foodPreferenceValues));
  gState.splashEndMs = 0;
  gState.lifeStageEndMs = 0;
  gState.foodLastAdvanceMs = 0;
  gState.foodPlaybackEndMs = 0;
  gState.toyBoredAtMs = 0;
  gState.indicatorOverrideUntilMs = 0;
  gState.pendingPetOutcomeAtMs = 0;
  gState.pendingPetOutcomeUntilMs = 0;
  gState.petWanderNextMs = 0;
  gState.nextPoopAtMs = 0;
  gState.rumbleEndMs = 0;
  gState.nextRumbleAllowedMs = 0;
  gState.rumbleStepNextMs = 0;
  gState.likedFoodCount = 0;
  gState.neutralFoodCount = 0;
  gState.dislikedFoodCount = 0;
  gState.totalFoodCount = 0;
  gState.taiyakiFoodCount = 0;
  gState.feedCountSinceLastPoop = 0;
  gState.evolutionWindowTicks = 0;
  gState.evolutionLowHappinessTicks = 0;
  gState.evolutionMinHappinessObserved = 100;
  gState.evolutionHappinessAlwaysAboveThreshold = true;
  gState.evolutionResolved = false;
  gState.deathHungerZeroTicks = 0;
  gState.deathHappinessZeroTicks = 0;
  gState.deathSleepinessHighTicks = 0;
  gState.darkAwakeTicks = 0;
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
  gState.tombstoneNightIndicator = false;
  gState.demoUnlocked = false;
  gState.demoModeActive = false;
  gState.demoEmotionIndex = 0;
  gState.demoCharacterIndex = 0;
  gState.demoMenuWrapCount = 0;
  gState.petInteractionStage = 0;
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
  normalizeToySelection();
}

bool actionVisible(MenuActionId action) {
  if (gState.demoModeActive) {
    return action == MENU_BACK || action == MENU_EXIT_DEMO;
  }

  if (action == MENU_EXIT_DEMO) {
    return false;
  }

  if (action == MENU_DEMO) {
    return gState.demoUnlocked;
  }

  if (action == MENU_CLEAN) {
    return gState.canClean;
  }

  if (action == MENU_PLAY) {
    return toyAnimationCount() > 0;
  }

  return true;
}

uint8_t visibleActionCount() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < kMenuOrderCount; ++i) {
    if (actionVisible(kMenuOrder[i])) {
      ++count;
    }
  }
  return count;
}

MenuActionId actionAtVisibleIndex(uint8_t visibleIndex) {
  uint8_t slot = 0;
  for (uint8_t i = 0; i < kMenuOrderCount; ++i) {
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
      if (!isAliveAndInteractive()) {
        gState.scene = SCENE_PET;
        break;
      }

      gState.indicatorOverrideAnimationId = gState.indicatorPetAnimationId;
      gState.indicatorOverrideUntilMs = gNowMs + Config::kPetAnimMs;

      if (gState.petInteractionStage >= 2) {
        gState.pendingPetOutcomeIndicatorId = gState.indicatorWorriedAnimationId;
      } else {
        const long roll = random(100);
        if (gState.petInteractionStage == 0) {
          if (roll < 72) {
            gState.pendingPetOutcomeIndicatorId = gState.indicatorSatisfiedAnimationId;
            applyHappinessDelta(Config::kPetSatisfiedHappinessGain);
          } else {
            gState.pendingPetOutcomeIndicatorId = gState.indicatorBoredAnimationId;
            gState.petInteractionStage = 1;
          }
        } else {
          if (roll < 35) {
            gState.pendingPetOutcomeIndicatorId = gState.indicatorBoredAnimationId;
            gState.petInteractionStage = 1;
          } else {
            gState.pendingPetOutcomeIndicatorId = gState.indicatorWorriedAnimationId;
            gState.petInteractionStage = 2;
          }
        }
      }

      if (gState.pendingPetOutcomeIndicatorId == gState.indicatorWorriedAnimationId) {
        applyHappinessDelta(-Config::kPetWorriedHappinessDrop);
      }

      gState.pendingPetOutcomeAtMs = gState.indicatorOverrideUntilMs;
      gState.pendingPetOutcomeUntilMs = gState.pendingPetOutcomeAtMs + Config::kPetOutcomeMs;
      updateMood();
      gState.scene = SCENE_PET;
      break;
    case MENU_PLAY:
      normalizeToySelection();
      gState.scene = SCENE_TOY_MENU;
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
        gState.sleeping = canSleepNow();
        gState.darkAwakeTicks = 0;
        gState.petTargetOffsetX = 0;
        gState.petWanderNextMs = gNowMs;
      } else {
        gState.lightsOn = true;
        gState.sleeping = false;
        gState.darkAwakeTicks = 0;
      }
      updateMood();
      gState.scene = SCENE_PET;
      break;
    case MENU_DEMO:
      enterDemoMode(gNowMs);
      break;
    case MENU_EXIT_DEMO:
      exitDemoMode(gNowMs);
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

  if (gState.demoModeActive) {
    if (event.button == BUTTON_SECONDARY) {
      gState.scene = SCENE_MENU;
      normalizeMenuSelection();
      gState.dirty = true;
      return;
    }

    if (event.button == BUTTON_PRIMARY) {
      gState.demoEmotionIndex = static_cast<uint8_t>((gState.demoEmotionIndex + 1) % 4);
      applyDemoEmotion();
      return;
    }

    if (event.button == BUTTON_TERTIARY) {
      gState.demoCharacterIndex = static_cast<uint8_t>((gState.demoCharacterIndex + 1) % 7);
      applyDemoCharacter();
      applyDemoEmotion();
      return;
    }
  }

  if (gState.lifeStage == LIFE_TOMBSTONE) {
    if (event.button == BUTTON_SECONDARY) {
      enterLifeStage(LIFE_REBIRTHING, gNowMs);
    }
    return;
  }

  if (!isAliveAndInteractive()) {
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

  if (!isAliveAndInteractive()) {
    gState.scene = SCENE_PET;
    gState.dirty = true;
    return;
  }

  const uint8_t count = visibleActionCount();
  if (count == 0) {
    gState.scene = SCENE_PET;
    gState.dirty = true;
    return;
  }

  if (event.button == BUTTON_PRIMARY) {
    if (gState.menuIndex == 0) {
      ++gState.demoMenuWrapCount;
      if (gState.demoMenuWrapCount >= kDemoUnlockWraps) {
        gState.demoUnlocked = true;
      }
    }
    gState.menuIndex = (gState.menuIndex + count - 1) % count;
    normalizeMenuSelection();
    gState.dirty = true;
    return;
  }

  if (event.button == BUTTON_TERTIARY) {
    if (gState.menuIndex == (count - 1)) {
      ++gState.demoMenuWrapCount;
      if (gState.demoMenuWrapCount >= kDemoUnlockWraps) {
        gState.demoUnlocked = true;
      }
    }
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
  if (!isAliveAndInteractive()) {
    gState.scene = SCENE_PET;
    return;
  }

  gState.petTargetOffsetX = 0;
  gState.petWanderNextMs = gNowMs;
  gState.activeFoodAnimationId = gState.selectedFoodAnimationId;
  gState.foodFrameIndex = 0;
  gState.foodLastAdvanceMs = gNowMs;

  const AnimationDef& foodAnim = Assets::animationOrSplash(gState.activeFoodAnimationId);
  const uint8_t foodFrameCount = (foodAnim.frameCount == 0) ? 1 : foodAnim.frameCount;
  const uint32_t foodDurationMs = static_cast<uint32_t>(foodFrameCount) * Config::kFoodAnimationTickMs * Config::kFoodPlaybackCycles;

  // Keep feeding visible for at least one full avatar cycle so it does not end too fast.
  const AnimationDef& avatarAnim = Assets::animationOrSplash(gState.baseAnimationId);
  const uint8_t avatarFrameCount = (avatarAnim.frameCount == 0) ? 1 : avatarAnim.frameCount;
  const uint32_t avatarCycleMs = static_cast<uint32_t>(avatarFrameCount) * Config::kAnimationTickMs;

  gState.foodPlaybackEndMs = gNowMs + max(foodDurationMs, avatarCycleMs);
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
  (void)event;
}

void handleToyMenuEvent(const InputEvent& event) {
  if (event.type != INPUT_SHORT_PRESS) {
    return;
  }

  const uint8_t count = toyAnimationCount();
  if (count == 0) {
    gState.scene = SCENE_MENU;
    gState.dirty = true;
    return;
  }

  if (event.button == BUTTON_PRIMARY) {
    gState.toyMenuIndex = (gState.toyMenuIndex + count - 1) % count;
    normalizeToySelection();
    gState.dirty = true;
    return;
  }

  if (event.button == BUTTON_TERTIARY) {
    gState.toyMenuIndex = (gState.toyMenuIndex + 1) % count;
    normalizeToySelection();
    gState.dirty = true;
    return;
  }

  if (event.button == BUTTON_SECONDARY) {
    gState.activeToyAnimationId = gState.selectedToyAnimationId;
    gState.toyBoredAtMs = gNowMs + randomInRange(Config::kToyBoredMinMs, Config::kToyBoredMaxMs);
    gState.scene = SCENE_PET;
    gState.dirty = true;
  }
}

void handleEvent(const InputEvent& event) {
  if (event.type == INPUT_NONE) {
    return;
  }

  if (gState.lifeStage == LIFE_TOMBSTONE) {
    handlePetEvent(event);
    return;
  }

  if (!isAliveAndInteractive() && gState.scene != SCENE_SPLASH) {
    if (gState.scene != SCENE_PET) {
      gState.scene = SCENE_PET;
      gState.dirty = true;
    }
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

  if (gState.scene == SCENE_TOY_MENU) {
    handleToyMenuEvent(event);
    return;
  }

  if (gState.scene == SCENE_DEV_INFO || gState.scene == SCENE_STATUS) {
    handleDetailEvent(event);
    return;
  }

  handlePetEvent(event);
}

void tickStats() {
  if (gState.demoModeActive) {
    gState.dirty = true;
    return;
  }

  if (!isAliveAndInteractive()) {
    return;
  }

  ++gState.pet.ageTicks;
  ++gState.statTickCounter;

  if (!gState.sleeping && (gState.statTickCounter % 16) == 0) {
    gState.pet.sleepiness = clampStat(gState.pet.sleepiness + Config::kSleepinessTickGain);
  }

  if (gState.sleeping) {
    gState.pet.sleepiness = clampStat(gState.pet.sleepiness - Config::kSleepinessSleepRecover);
    if (gState.pet.sleepiness == 0 || gState.lightsOn) {
      gState.sleeping = false;
      gState.darkAwakeTicks = 0;
      updateMood();
    }
  } else {
    if (!gState.lightsOn) {
      if (canSleepNow()) {
        gState.sleeping = true;
        gState.darkAwakeTicks = 0;
        gState.petTargetOffsetX = 0;
        gState.petWanderNextMs = gNowMs;
      } else {
        ++gState.darkAwakeTicks;
        if (gState.darkAwakeTicks >= Config::kDarkAwakeBoredTicks) {
          gState.darkAwakeTicks = 0;
          applyHappinessDelta(-Config::kDarkAwakeBoredHappinessDrop);

          if (isAliveAndInteractive()) {
            gState.indicatorOverrideAnimationId = gState.indicatorBoredAnimationId;
            gState.indicatorOverrideUntilMs = gNowMs + Config::kPetOutcomeMs;
          }
        }
      }
    } else {
      gState.darkAwakeTicks = 0;
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

void updateEvolutionWindowTracking() {
  if (gState.lifeStage != LIFE_BLOBB) {
    return;
  }

  ++gState.evolutionWindowTicks;
  if (gState.pet.happiness < gState.evolutionMinHappinessObserved) {
    gState.evolutionMinHappinessObserved = gState.pet.happiness;
  }

  if (gState.pet.happiness < kEvolutionHappyThreshold) {
    gState.evolutionHappinessAlwaysAboveThreshold = false;
  }

  if (gState.pet.happiness <= 25) {
    ++gState.evolutionLowHappinessTicks;
  }
}

void updateDeathTracking() {
  if (!isAliveAndInteractive()) {
    return;
  }

  if (gState.pet.hunger == 0) {
    ++gState.deathHungerZeroTicks;
  } else {
    gState.deathHungerZeroTicks = 0;
  }

  if (gState.pet.happiness == 0) {
    ++gState.deathHappinessZeroTicks;
  } else {
    gState.deathHappinessZeroTicks = 0;
  }

  if (gState.pet.sleepiness >= 98) {
    ++gState.deathSleepinessHighTicks;
  } else {
    gState.deathSleepinessHighTicks = 0;
  }

  const bool starved = gState.deathHungerZeroTicks >= kDeathByHungerTicks;
  const bool despair = gState.deathHappinessZeroTicks >= kDeathByHappinessTicks;
  const bool exhausted = gState.deathSleepinessHighTicks >= kDeathBySleepinessTicks;
  const bool oldAge = gState.pet.ageTicks >= kDeathByAgeTicks;
  if (starved || despair || exhausted || oldAge) {
    enterLifeStage(LIFE_TOMBSTONE, gNowMs);
  }
}

void updatePoopState(uint32_t nowMs) {
  if (!gState.poopPending || nowMs < gState.nextPoopAtMs) {
    return;
  }

  gState.poopPending = false;

  if (gState.dirtLevel == DIRT_NONE) {
    setDirtLevel(DIRT_SMALL);
  } else if (gState.dirtLevel == DIRT_SMALL || gState.pendingBigPoop) {
    setDirtLevel(DIRT_BIG);
  }

  gState.pendingBigPoop = false;
  gState.dirty = true;
}

void updatePetWander(uint32_t nowMs) {
  if (gState.scene == SCENE_SPLASH) {
    return;
  }

  if (!isAliveAndInteractive()) {
    gState.petTargetOffsetX = 0;
    if (gState.petOffsetX < 0) {
      ++gState.petOffsetX;
      gState.dirty = true;
    } else if (gState.petOffsetX > 0) {
      --gState.petOffsetX;
      gState.dirty = true;
    }
    gState.petWanderNextMs = nowMs + 120;
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
  if (gState.lifeStage != LIFE_BLOBB && gState.lifeStage != LIFE_CHARACTER) {
    gState.rumbleActive = false;
    gState.rumbleStepIndex = 0;
    gState.rumbleEndMs = 0;
    gState.rumbleStepNextMs = 0;
    gState.nextRumbleAllowedMs = nowMs;
    return;
  }

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

  if (!isAliveAndInteractive()) {
    gState.scene = SCENE_PET;
    gState.dirty = true;
    return;
  }

  updateFoodAnimation(nowMs);
  if (nowMs < gState.foodPlaybackEndMs) {
    return;
  }

  applyFoodOutcome();
  // After feeding, animate a return from side placement to center.
  gState.petOffsetX = Config::kFeedPetOffsetXPx;
  gState.petTargetOffsetX = 0;
  gState.petWanderNextMs = nowMs;
  gState.scene = SCENE_PET;
  gState.dirty = true;
}

void updateToyState(uint32_t nowMs) {
  if (gState.activeToyAnimationId == kInvalidAnimationId || gState.toyBoredAtMs == 0) {
    return;
  }

  if (nowMs < gState.toyBoredAtMs) {
    return;
  }

  gState.activeToyAnimationId = kInvalidAnimationId;
  gState.toyBoredAtMs = 0;
  if (gState.petInteractionStage < 1) {
    gState.petInteractionStage = 1;
  }

  gState.indicatorOverrideAnimationId = gState.indicatorBoredAnimationId;
  gState.indicatorOverrideUntilMs = nowMs + Config::kPetOutcomeMs;
  updateNeedIndicator();
  gState.dirty = true;
}

void updateLogic(uint32_t nowMs) {
  if (gState.demoModeActive) {
    return;
  }

  while ((nowMs - gState.lastLogicMs) >= Config::kLogicTickMs) {
    gState.lastLogicMs += Config::kLogicTickMs;
    tickStats();
    updateEvolutionWindowTracking();
    resolveEvolutionIfNeeded();
    updateDeathTracking();
    updateToyState(gState.lastLogicMs);
  }
}

void maybeSave(uint32_t nowMs) {
  if (gState.demoModeActive) {
    return;
  }

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
    Serial.println(F("Display init failed, retrying..."));
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
        Serial.println(F("Display init recovered"));
        gState.dirty = true;
      }
    }
    delay(50);
    return;
  }

  if (gState.scene == SCENE_SPLASH) {
    updateAnimation(nowMs);

    if (nowMs >= gState.splashEndMs) {
      beginNewLifeCycle(nowMs);
    }

    if (gState.dirty) {
      Renderer::render(gState);
      gState.lastRenderMs = nowMs;
      gState.dirty = false;
    }

    delay(computeLoopSleepMs(nowMs));
    return;
  }

  InputEvent event = {INPUT_NONE, BUTTON_PRIMARY};
  while (gInput.poll(event, nowMs)) {
    handleEvent(event);
  }

  updateLifeStage(nowMs);
  updateLogic(nowMs);
  updatePoopState(nowMs);
  updateRumbleState(nowMs);
  updatePetWander(nowMs);
  updateAnimation(nowMs);
  updateReactionState();
  updateFeedPlayback(nowMs);
  updateIndicatorState(nowMs);
  maybeSave(nowMs);

  if (gState.dirty) {
    Renderer::render(gState);
    gState.lastRenderMs = nowMs;
    gState.dirty = false;
  }

  delay(computeLoopSleepMs(nowMs));
}

}  // namespace App