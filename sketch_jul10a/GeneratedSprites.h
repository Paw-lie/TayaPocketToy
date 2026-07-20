#pragma once

#include <Arduino.h>
#include "Assets.h"

namespace GeneratedSprites {

const AnimationDef& animationAt(uint8_t index);
uint8_t animationCount();
const char* animationName(uint8_t index);

}  // namespace GeneratedSprites
