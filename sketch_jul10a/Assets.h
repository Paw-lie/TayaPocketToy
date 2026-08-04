#pragma once

// this is the comment

#include <Arduino.h>

#include "AppTypes.h"

struct SpriteFrame {
  const uint8_t* bitmap;
  uint8_t width;
  uint8_t height;
  int8_t accessoryAnchorX;
  int8_t accessoryAnchorY;
  bool isVerticalLsb;
};

struct AnimationDef {
  const SpriteFrame* frames;
  uint8_t frameCount;
};

struct AccessorySprite {
  const uint8_t* bitmap;
  uint8_t width;
  uint8_t height;
  int8_t offsetX;
  int8_t offsetY;
};

namespace Assets {

const AnimationDef& blobIdle();
const AnimationDef& animationAt(uint8_t index);
const AnimationDef& animationOrSplash(uint8_t index);
uint8_t animationCount();
const char* animationName(uint8_t index);
uint8_t splashAnimationId();
uint8_t eggAnimationId();
uint8_t hatchAnimationId();
uint8_t rebirthAnimationId();
uint8_t defaultPetAnimationId();
uint8_t characterAnimationId(CharacterForm form);
uint8_t randomCharacterAnimationId();
const AccessorySprite* getAccessory(AccessoryId accessoryId);

}  // namespace Assets