#include "Renderer.h"

#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <string.h>

#include "Assets.h"
#include "Config.h"

namespace {

Adafruit_PCD8544 gDisplay(
    Config::kLcdClkPin,
    Config::kLcdDinPin,
    Config::kLcdDcPin,
    Config::kLcdCsPin,
    Config::kLcdRstPin);

uint8_t gAppliedContrast = 0xFF;

constexpr uint16_t kColorOn = BLACK;
constexpr uint16_t kColorCutout = WHITE;
uint16_t gColorOn = kColorOn;
uint16_t gColorCutout = kColorCutout;

struct FrameLayout {
  uint16_t sourceWidth;
  uint16_t sourceHeight;
  uint16_t orientedWidth;
  uint16_t orientedHeight;
  bool rotate90Cw;
};

void drawMoodFace(PetMood mood, int16_t petX, int16_t petY, int16_t frameWidth, int16_t frameHeight);

bool pixelOnPacked(const uint8_t* bitmap, uint16_t width, uint16_t px, uint16_t py, bool isVerticalLsb) {
  if (isVerticalLsb) {
    const uint16_t byteIndex = (static_cast<uint16_t>(py / 8) * width) + px;
    const uint8_t packed = pgm_read_byte(bitmap + byteIndex);
    const uint8_t bitMask = static_cast<uint8_t>(1U << (py & 0x07));
    return (packed & bitMask) != 0;
  }

  const uint16_t bytesPerRow = static_cast<uint16_t>((width + 7) / 8);
  const uint16_t byteIndex = (py * bytesPerRow) + (px / 8);
  const uint8_t packed = pgm_read_byte(bitmap + byteIndex);
  const uint8_t bitMask = static_cast<uint8_t>(0x80 >> (px & 0x07));
  return (packed & bitMask) != 0;
}

FrameLayout layoutForFrame(const SpriteFrame& frame) {
  FrameLayout layout = {
      frame.width,
      frame.height,
      frame.width,
      frame.height,
      false,
  };

  // Auto-rotation is only useful when the active screen coordinates are landscape.
  // In portrait mode (48x84), frames should be drawn as-is.
  const bool screenIsLandscape = Config::kScreenWidth > Config::kScreenHeight;
  const bool canRotateToScreen = screenIsLandscape &&
                                 frame.isVerticalLsb &&
                                 frame.height > frame.width &&
                                 frame.height <= Config::kScreenWidth &&
                                 frame.width <= Config::kScreenHeight;
  if (canRotateToScreen) {
    layout.orientedWidth = frame.height;
    layout.orientedHeight = frame.width;
    layout.rotate90Cw = true;
  }

  return layout;
}

void fitDimensionsToScreen(uint16_t srcW, uint16_t srcH, uint16_t& drawW, uint16_t& drawH) {
  drawW = srcW;
  drawH = srcH;

  if (drawW <= Config::kScreenWidth && drawH <= Config::kScreenHeight) {
    return;
  }

  uint32_t fittedW = Config::kScreenWidth;
  uint32_t fittedH = (static_cast<uint32_t>(srcH) * Config::kScreenWidth) / srcW;

  if (fittedH > Config::kScreenHeight) {
    fittedH = Config::kScreenHeight;
    fittedW = (static_cast<uint32_t>(srcW) * Config::kScreenHeight) / srcH;
  }

  drawW = static_cast<uint16_t>(fittedW == 0 ? 1 : fittedW);
  drawH = static_cast<uint16_t>(fittedH == 0 ? 1 : fittedH);
}

void drawFrameMapped(int16_t x, int16_t y, const SpriteFrame& frame, uint16_t drawW, uint16_t drawH, uint16_t color) {
  const FrameLayout layout = layoutForFrame(frame);

  for (uint16_t dy = 0; dy < drawH; ++dy) {
    for (uint16_t dx = 0; dx < drawW; ++dx) {
      const uint16_t orientedX = (static_cast<uint32_t>(dx) * layout.orientedWidth) / drawW;
      const uint16_t orientedY = (static_cast<uint32_t>(dy) * layout.orientedHeight) / drawH;

      uint16_t srcX = orientedX;
      uint16_t srcY = orientedY;
      if (layout.rotate90Cw) {
        srcX = orientedY;
        srcY = static_cast<uint16_t>(layout.sourceHeight - 1U - orientedX);
      }

      if (!pixelOnPacked(frame.bitmap, layout.sourceWidth, srcX, srcY, frame.isVerticalLsb)) {
        continue;
      }

      gDisplay.drawPixel(x + static_cast<int16_t>(dx), y + static_cast<int16_t>(dy), color);
    }
  }
}

void drawAnimationFrame(uint8_t animationId, uint8_t frameIndex, bool drawFace, PetMood mood, int8_t offsetX = 0) {
  const AnimationDef& animation = Assets::animationOrSplash(animationId);
  if (animation.frameCount == 0) {
    return;
  }

  const SpriteFrame& frame = animation.frames[frameIndex % animation.frameCount];
  const FrameLayout layout = layoutForFrame(frame);
  const bool hasCropOffset = (frame.accessoryAnchorX != 0) || (frame.accessoryAnchorY != 0);

  uint16_t drawW = layout.orientedWidth;
  uint16_t drawH = layout.orientedHeight;
  if (!hasCropOffset) {
    fitDimensionsToScreen(layout.orientedWidth, layout.orientedHeight, drawW, drawH);
  }

  int16_t drawX = ((Config::kScreenWidth - static_cast<int16_t>(drawW)) / 2) + offsetX;
  int16_t drawY = (Config::kScreenHeight - static_cast<int16_t>(drawH)) / 2;

  if (hasCropOffset && !layout.rotate90Cw) {
    // Cropped sprites keep source-space offsets so layered animations align.
    drawX = static_cast<int16_t>(frame.accessoryAnchorX) + offsetX;
    drawY = static_cast<int16_t>(frame.accessoryAnchorY);
  }

  drawFrameMapped(drawX, drawY, frame, drawW, drawH, gColorOn);
  if (drawFace && drawW >= 24 && drawH >= 24) {
    drawMoodFace(mood, drawX, drawY, drawW, drawH);
  }
}

void drawBar(int16_t x, int16_t y, int16_t width, uint8_t value) {
  gDisplay.drawRect(x, y, width, 6, gColorOn);
  const int16_t fillWidth = (value * (width - 2)) / 100;
  gDisplay.fillRect(x + 1, y + 1, fillWidth, 4, gColorOn);
}

int16_t footerY() {
  return static_cast<int16_t>(Config::kScreenHeight) - 8;
}

const __FlashStringHelper* menuLabel(MenuActionId action, bool lightsOn, ScreenBrightnessMode brightnessMode) {
  switch (action) {
    case MENU_FEED:
      return F("Feed");
    case MENU_PET:
      return F("Pet");
    case MENU_PLAY:
      return F("Toy");
    case MENU_DEV_INFO:
      return F("DevInfo");
    case MENU_STATUS:
      return F("Status");
    case MENU_LIGHTS_TOGGLE:
      return lightsOn ? F("Lights OFF") : F("Lights ON");
    case MENU_BRIGHTNESS:
      return (brightnessMode == BRIGHTNESS_DIM) ? F("Brightness:Dim") : F("Brightness:Norm");
    case MENU_CLEAN:
      return F("Clean");
    case MENU_DEMO:
      return F("Demo");
    case MENU_EXIT_DEMO:
      return F("Exit Demo");
    case MENU_BACK:
    default:
      return F("Back");
  }
}

void drawMoodFace(PetMood mood, int16_t petX, int16_t petY, int16_t frameWidth, int16_t frameHeight) {
  const int16_t leftEyeX = petX + frameWidth / 3;
  const int16_t rightEyeX = petX + (frameWidth * 2) / 3;
  const int16_t eyeY = petY + frameHeight / 3;
  const int16_t mouthY = petY + (frameHeight * 3) / 4;
  const int16_t mouthLeftX = petX + frameWidth / 3;
  const int16_t mouthRightX = petX + (frameWidth * 2) / 3;

  gDisplay.fillRect(leftEyeX, eyeY, 1, 1, gColorCutout);
  gDisplay.fillRect(rightEyeX, eyeY, 1, 1, gColorCutout);

  switch (mood) {
    case MOOD_HAPPY:
      gDisplay.drawPixel(mouthLeftX - 1, mouthY - 1, gColorCutout);
      gDisplay.drawLine(mouthLeftX, mouthY, mouthRightX, mouthY, gColorCutout);
      gDisplay.drawPixel(mouthRightX + 1, mouthY - 1, gColorCutout);
      break;
    case MOOD_HUNGRY:
      gDisplay.drawLine(mouthLeftX, mouthY, mouthRightX, mouthY, gColorCutout);
      break;
    case MOOD_SAD:
      gDisplay.drawPixel(mouthLeftX - 1, mouthY + 1, gColorCutout);
      gDisplay.drawLine(mouthLeftX, mouthY, mouthRightX, mouthY, gColorCutout);
      gDisplay.drawPixel(mouthRightX + 1, mouthY + 1, gColorCutout);
      break;
    case MOOD_NEUTRAL:
    default:
      gDisplay.drawLine(mouthLeftX, mouthY, mouthRightX, mouthY, gColorCutout);
      break;
  }
}

void drawPet(const AppState& state, int8_t extraOffsetX = 0) {
  if (state.lifeStage != LIFE_BLOBB) {
    drawAnimationFrame(state.baseAnimationId, state.animation.frameIndex, false, state.pet.mood, state.petOffsetX + extraOffsetX);
    return;
  }

  uint8_t displayFrameIndex = state.animation.frameIndex;
  int8_t displayOffsetX = state.petOffsetX + extraOffsetX;

  if (state.rumbleActive) {
    const AnimationDef& baseAnim = Assets::animationOrSplash(state.baseAnimationId);
    const uint8_t frameOne = 0;
    const uint8_t frameTwo = (baseAnim.frameCount >= 2) ? 1 : 0;

    switch (state.rumbleStepIndex) {
      case 0:
        displayFrameIndex = frameOne;
        displayOffsetX = 0;
        break;
      case 1:
        displayFrameIndex = frameTwo;
        displayOffsetX = 0;
        break;
      case 2:
        displayFrameIndex = frameTwo;
        displayOffsetX = -1;
        break;
      case 3:
        displayFrameIndex = frameTwo;
        displayOffsetX = 0;
        break;
      case 4:
      default:
        displayFrameIndex = frameTwo;
        displayOffsetX = 1;
        break;
    }
  }

  drawAnimationFrame(state.baseAnimationId, displayFrameIndex, false, state.pet.mood, displayOffsetX);
  if (state.accessoryAnimationId != 0xFF) {
    drawAnimationFrame(state.accessoryAnimationId, displayFrameIndex, false, state.pet.mood, displayOffsetX);
  }
  const uint8_t visibleFace = state.reactionActive ? state.reactionFaceAnimationId : state.faceAnimationId;
  drawAnimationFrame(visibleFace, displayFrameIndex, false, state.pet.mood, displayOffsetX);
}

void drawDirtOverlay(const AppState& state, int8_t extraOffsetX = 0) {
  if (state.lifeStage != LIFE_BLOBB && state.lifeStage != LIFE_CHARACTER) {
    return;
  }

  if (state.dirtLevel == DIRT_NONE) {
    return;
  }

  const uint8_t dirtAnimationId = (state.dirtLevel == DIRT_BIG) ? state.poopBigAnimationId : state.poopSmallAnimationId;
  drawAnimationFrame(dirtAnimationId, state.animation.frameIndex, false, state.pet.mood, extraOffsetX);
}

const __FlashStringHelper* moodLabel(PetMood mood) {
  switch (mood) {
    case MOOD_HAPPY:
      return F("Happy");
    case MOOD_HUNGRY:
      return F("Hungry");
    case MOOD_SAD:
      return F("Sad");
    case MOOD_NEUTRAL:
    default:
      return F("Neutral");
  }
}

void foodDisplayName(uint8_t animationId, char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }

  out[0] = '\0';
  const char* raw = Assets::animationName(animationId);
  if (raw == nullptr) {
    strncpy(out, "food", outSize - 1);
    out[outSize - 1] = '\0';
    return;
  }

  const char* prefix = "food_";
  const size_t prefixLen = strlen(prefix);
  if (strncmp(raw, prefix, prefixLen) == 0) {
    raw += prefixLen;
  }

  strncpy(out, raw, outSize - 1);
  out[outSize - 1] = '\0';
}

void toyDisplayName(uint8_t animationId, char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }

  out[0] = '\0';
  const char* raw = Assets::animationName(animationId);
  if (raw == nullptr) {
    strncpy(out, "toy", outSize - 1);
    out[outSize - 1] = '\0';
    return;
  }

  const char* prefix = "toys_";
  const size_t prefixLen = strlen(prefix);
  if (strncmp(raw, prefix, prefixLen) == 0) {
    raw += prefixLen;
  }

  strncpy(out, raw, outSize - 1);
  out[outSize - 1] = '\0';
}

void drawSpriteScene(const AppState& state) {
  drawPet(state);
  drawDirtOverlay(state);

  if (state.activeToyAnimationId != 0xFF) {
    drawAnimationFrame(state.activeToyAnimationId, state.animation.frameIndex, false, state.pet.mood);
  }

  if (state.activeIndicatorAnimationId != 0xFF) {
    drawAnimationFrame(state.activeIndicatorAnimationId, state.animation.frameIndex, false, state.pet.mood);
  }
}

void drawMenuScene(const AppState& state) {
  const __FlashStringHelper* label = menuLabel(state.selectedMenuAction, state.lightsOn, state.brightnessMode);

  char menuText[22] = {};
  strncpy_P(menuText, reinterpret_cast<PGM_P>(label), sizeof(menuText) - 1);

  const uint8_t textSize = 1;
  const int16_t textWidth = static_cast<int16_t>(strlen(menuText)) * 6 * textSize;
  int16_t x = (Config::kScreenWidth - textWidth) / 2;
  if (x < 0) {
    x = 0;
  }
  const int16_t y = (Config::kScreenHeight / 2) - 4;

  gDisplay.setTextSize(1);
  gDisplay.setCursor(0, 0);
  gDisplay.println(F("Menu"));

  gDisplay.setCursor(x, y);
  gDisplay.print(menuText);

  gDisplay.setCursor(0, footerY());
  gDisplay.println(F("B1< B2 OK B3>"));
}

void drawFeedMenuScene(const AppState& state) {
  char foodName[24] = {};
  foodDisplayName(state.selectedFoodAnimationId, foodName, sizeof(foodName));

  const uint8_t textSize = 1;
  const int16_t textWidth = static_cast<int16_t>(strlen(foodName)) * 6 * textSize;
  int16_t x = (Config::kScreenWidth - textWidth) / 2;
  if (x < 0) {
    x = 0;
  }
  const int16_t y = (Config::kScreenHeight / 2) - 4;

  gDisplay.setTextSize(1);
  gDisplay.setCursor(0, 0);
  gDisplay.println(F("Feed Select"));

  gDisplay.setCursor(x, y);
  gDisplay.print(foodName);

  gDisplay.setCursor(0, footerY());
  gDisplay.println(F("B1< B2 Feed B3>"));
}

void drawToyMenuScene(const AppState& state) {
  char toyName[24] = {};
  toyDisplayName(state.selectedToyAnimationId, toyName, sizeof(toyName));

  const int16_t textWidth = static_cast<int16_t>(strlen(toyName)) * 6;
  int16_t x = (Config::kScreenWidth - textWidth) / 2;
  if (x < 0) {
    x = 0;
  }

  gDisplay.setTextSize(1);
  gDisplay.setCursor(0, 0);
  gDisplay.println(F("Toy Select"));

  gDisplay.setCursor(x, (Config::kScreenHeight / 2) - 4);
  gDisplay.print(toyName);

  gDisplay.setCursor(0, footerY());
  gDisplay.println(F("B1< B2 Set B3>"));
}

void drawFeedPlaybackScene(const AppState& state) {
  drawPet(state, Config::kFeedPetOffsetXPx);
  drawDirtOverlay(state, Config::kFeedPetOffsetXPx);
  drawAnimationFrame(state.activeFoodAnimationId, state.foodFrameIndex, false, state.pet.mood);

  gDisplay.setTextSize(1);
  gDisplay.setCursor(0, footerY());
  gDisplay.println(F("Feeding..."));
}

void drawSplashScene(const AppState& state) {
  drawAnimationFrame(state.splashAnimationId, state.animation.frameIndex, false, state.pet.mood);
}

void drawStatusScene(const AppState& state) {
  gDisplay.setTextSize(1);
  gDisplay.setCursor(0, 0);
  gDisplay.print(F("Mood:"));
  gDisplay.println(moodLabel(state.pet.mood));

  gDisplay.setCursor(0, 12);
  gDisplay.print(F("Ful"));
  drawBar(18, 12, 64, state.pet.hunger);

  gDisplay.setCursor(0, 22);
  gDisplay.print(F("Joy"));
  drawBar(18, 22, 64, state.pet.happiness);

  gDisplay.setCursor(0, 32);
  gDisplay.print(F("Slp"));
  drawBar(18, 32, 64, state.pet.sleepiness);

  gDisplay.setCursor(0, footerY());
  gDisplay.println(F("B2:Menu"));
}

void drawDevInfoScene(const AppState& state) {
  gDisplay.setTextSize(1);
  gDisplay.setCursor(0, 0);
  gDisplay.print(F("Age:"));
  gDisplay.println(state.pet.ageTicks);

  gDisplay.setCursor(0, 8);
  gDisplay.print(F("Anim:"));
  gDisplay.print(state.animation.animationId);
  gDisplay.print(F(" F:"));
  gDisplay.println(state.animation.frameIndex);

  gDisplay.setCursor(0, 16);
  gDisplay.print(F("Lights:"));
  gDisplay.println(state.lightsOn ? F("ON") : F("OFF"));

  gDisplay.setCursor(0, 24);
  gDisplay.print(F("Clean:"));
  gDisplay.println(state.canClean ? F("Y") : F("N"));

  gDisplay.setCursor(0, 32);
  gDisplay.print(F("F L/N/D"));

  gDisplay.setCursor(0, 40);
  gDisplay.print(state.likedFoodCount);
  gDisplay.print(F("/"));
  gDisplay.print(state.neutralFoodCount);
  gDisplay.print(F("/"));
  gDisplay.println(state.dislikedFoodCount);
}

}  // namespace

namespace Renderer {

bool begin() {
  gDisplay.begin();
  gDisplay.setRotation(Config::kDisplayRotation);
  gDisplay.setContrast(Config::kLcdContrastNormal);
  gAppliedContrast = Config::kLcdContrastNormal;
  gDisplay.clearDisplay();
  gDisplay.display();
  return true;
}

void render(const AppState& state) {
  const bool shouldDim = (!state.lightsOn) || (state.brightnessMode == BRIGHTNESS_DIM);
  const uint8_t targetContrast = shouldDim ? Config::kLcdContrastDim : Config::kLcdContrastNormal;
  const bool invertDisplay = !state.lightsOn;
  if (targetContrast != gAppliedContrast) {
    gDisplay.setContrast(targetContrast);
    gAppliedContrast = targetContrast;
  }

  gDisplay.clearDisplay();
  if (invertDisplay) {
    gDisplay.fillRect(0, 0, Config::kScreenWidth, Config::kScreenHeight, BLACK);
    gColorOn = WHITE;
    gColorCutout = BLACK;
  } else {
    gColorOn = BLACK;
    gColorCutout = WHITE;
  }
  gDisplay.setTextColor(gColorOn);

  if (state.scene == SCENE_SPLASH) {
    drawSplashScene(state);
  } else if (state.scene == SCENE_PET) {
    drawSpriteScene(state);
  } else if (state.scene == SCENE_FEED_MENU) {
    drawFeedMenuScene(state);
  } else if (state.scene == SCENE_FEED_PLAYBACK) {
    drawFeedPlaybackScene(state);
  } else if (state.scene == SCENE_TOY_MENU) {
    drawToyMenuScene(state);
  } else if (state.scene == SCENE_DEV_INFO) {
    drawDevInfoScene(state);
  } else if (state.scene == SCENE_STATUS) {
    drawStatusScene(state);
  } else {
    drawMenuScene(state);
  }

  gDisplay.display();
}

}  // namespace Renderer
