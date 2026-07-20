#include "Assets.h"

#include <Arduino.h>
#include <string.h>

#include "GeneratedSprites.h"

namespace {

const uint8_t kBowAccessory[] PROGMEM = {
  0x24,
  0x7E,
  0xFF,
  0x7E,
  0x18,
  0x3C,
  0x7E,
  0x5A,
};

const AccessorySprite kBow = {kBowAccessory, 8, 8, 0, 0};

int8_t findAnimationByContains(const char* token) {
  const uint8_t count = GeneratedSprites::animationCount();
  for (uint8_t i = 0; i < count; ++i) {
    const char* name = GeneratedSprites::animationName(i);
    if (name != nullptr && strstr(name, token) != nullptr) {
      return static_cast<int8_t>(i);
    }
  }

  return -1;
}

}  // namespace

namespace Assets {

const AnimationDef& blobIdle() {
  return GeneratedSprites::animationAt(0);
}

const AnimationDef& animationAt(uint8_t index) {
  return GeneratedSprites::animationAt(index);
}

const AnimationDef& animationOrSplash(uint8_t index) {
  const AnimationDef& selected = GeneratedSprites::animationAt(index);
  if (selected.frameCount > 0) {
    return selected;
  }

  return GeneratedSprites::animationAt(splashAnimationId());
}

uint8_t animationCount() {
  return GeneratedSprites::animationCount();
}

const char* animationName(uint8_t index) {
  return GeneratedSprites::animationName(index);
}

uint8_t splashAnimationId() {
  const int8_t exact = findAnimationByContains("splash_screen__taiyakisplash");
  if (exact >= 0) {
    return static_cast<uint8_t>(exact);
  }

  const int8_t named = findAnimationByContains("taiyakisplash");
  if (named >= 0) {
    return static_cast<uint8_t>(named);
  }

  const int8_t generic = findAnimationByContains("splash");
  if (generic >= 0) {
    return static_cast<uint8_t>(generic);
  }

  return 0;
}

uint8_t defaultPetAnimationId() {
  const int8_t baseExact = findAnimationByContains("blobb_bases__blobb_base");
  if (baseExact >= 0) {
    return static_cast<uint8_t>(baseExact);
  }

  const int8_t baseGeneric = findAnimationByContains("blobb_base");
  if (baseGeneric >= 0) {
    return static_cast<uint8_t>(baseGeneric);
  }

  const int8_t anyBlobb = findAnimationByContains("blobb");
  if (anyBlobb >= 0) {
    return static_cast<uint8_t>(anyBlobb);
  }

  return splashAnimationId();
}

const AccessorySprite* getAccessory(AccessoryId accessoryId) {
  switch (accessoryId) {
    case ACCESSORY_BOW:
      return &kBow;
    case ACCESSORY_NONE:
    default:
      return nullptr;
  }
}

}  // namespace Assets