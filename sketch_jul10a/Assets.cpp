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

uint8_t randomCharacterAnimationMatch(const char* tokenPrefix) {
  uint8_t selected = 0xFF;
  uint16_t matches = 0;
  const uint8_t count = GeneratedSprites::animationCount();
  for (uint8_t i = 0; i < count; ++i) {
    const char* name = GeneratedSprites::animationName(i);
    if (name == nullptr || strstr(name, tokenPrefix) == nullptr) {
      continue;
    }

    ++matches;
    if (random(matches) == 0) {
      selected = i;
    }
  }

  return selected;
}

uint8_t firstCharacterIdleFallback() {
  const int8_t exact = findAnimationByContains("character_chiino_chiino_idle");
  if (exact >= 0) {
    return static_cast<uint8_t>(exact);
  }

  const int8_t generic = findAnimationByContains("character_");
  if (generic >= 0) {
    return static_cast<uint8_t>(generic);
  }

  return 0;
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

uint8_t eggAnimationId() {
  const int8_t exact = findAnimationByContains("egg_egg_idle");
  if (exact >= 0) {
    return static_cast<uint8_t>(exact);
  }

  const int8_t legacyLayer = findAnimationByContains("egg_animation_layer");
  if (legacyLayer >= 0) {
    return static_cast<uint8_t>(legacyLayer);
  }

  const int8_t generic = findAnimationByContains("egg");
  if (generic >= 0) {
    return static_cast<uint8_t>(generic);
  }

  return defaultPetAnimationId();
}

uint8_t hatchAnimationId() {
  const int8_t exact = findAnimationByContains("hatch_animation_egg_hatch");
  if (exact >= 0) {
    return static_cast<uint8_t>(exact);
  }

  const int8_t legacyLayer = findAnimationByContains("hatch_animation_layer");
  if (legacyLayer >= 0) {
    return static_cast<uint8_t>(legacyLayer);
  }

  const int8_t generic = findAnimationByContains("hatch");
  if (generic >= 0) {
    return static_cast<uint8_t>(generic);
  }

  return eggAnimationId();
}

uint8_t rebirthAnimationId() {
  const int8_t exact = findAnimationByContains("rebirth_animation_rebirth");
  if (exact >= 0) {
    return static_cast<uint8_t>(exact);
  }

  const int8_t legacyLayer = findAnimationByContains("rebirth_animation_layer");
  if (legacyLayer >= 0) {
    return static_cast<uint8_t>(legacyLayer);
  }

  const int8_t generic = findAnimationByContains("rebirth");
  if (generic >= 0) {
    return static_cast<uint8_t>(generic);
  }

  return hatchAnimationId();
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

uint8_t characterAnimationId(CharacterForm form) {
  int8_t resolved = -1;
  switch (form) {
    case FORM_TAIYAKI:
      resolved = findAnimationByContains("character_taiykai_taiyaki_idle");
      break;
    case FORM_PAWLIE:
      resolved = findAnimationByContains("character_pawlie_pawlie_idle");
      break;
    case FORM_CHIINO:
      resolved = findAnimationByContains("character_chiino_chiino_idle");
      break;
    case FORM_TOFU:
      resolved = findAnimationByContains("character_tofu_tofu_idle");
      break;
    case FORM_YSHAAR:
      resolved = findAnimationByContains("character_yshaar_yshaar_idle");
      break;
    case FORM_BLOBB:
    case FORM_NONE:
    default:
      break;
  }

  if (resolved >= 0) {
    return static_cast<uint8_t>(resolved);
  }

  return firstCharacterIdleFallback();
}

uint8_t randomCharacterAnimationId() {
  const uint8_t candidate = randomCharacterAnimationMatch("character_");
  if (candidate != 0xFF) {
    return candidate;
  }

  return firstCharacterIdleFallback();
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