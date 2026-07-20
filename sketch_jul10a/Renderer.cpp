#include "Renderer.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <string.h>

#include "Assets.h"
#include "Config.h"

namespace {

Adafruit_SSD1306 gDisplay(Config::kScreenWidth, Config::kScreenHeight, &Wire, Config::kOledReset);
bool gDisplayDimmed = false;

void drawMoodFace(PetMood mood, int16_t petX, int16_t petY, uint8_t frameWidth, uint8_t frameHeight, uint8_t scale);

bool pixelOn(const uint8_t* bitmap, uint8_t width, uint8_t px, uint8_t py, bool isVerticalLsb) {
  if (isVerticalLsb) {
    const uint16_t byteIndex = (static_cast<uint16_t>(py / 8) * width) + px;
    const uint8_t packed = pgm_read_byte(bitmap + byteIndex);
    const uint8_t bitMask = static_cast<uint8_t>(1U << (py & 0x07));
    return (packed & bitMask) != 0;
  }

  const uint8_t bytesPerRow = (width + 7) / 8;
  const uint16_t byteIndex = (static_cast<uint16_t>(py) * bytesPerRow) + (px / 8);
  const uint8_t packed = pgm_read_byte(bitmap + byteIndex);
  const uint8_t bitMask = static_cast<uint8_t>(0x80 >> (px & 0x07));
  return (packed & bitMask) != 0;
}

void drawBitmapScaled(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t width, uint8_t height, uint8_t scale,
                      uint16_t color, bool isVerticalLsb) {
  for (uint8_t py = 0; py < height; ++py) {
    for (uint8_t px = 0; px < width; ++px) {
      if (!pixelOn(bitmap, width, px, py, isVerticalLsb)) {
        continue;
      }

      gDisplay.fillRect(x + (px * scale), y + (py * scale), scale, scale, color);
    }
  }
}

uint8_t fittingScale(uint8_t width, uint8_t height, uint8_t preferredScale) {
  if (preferredScale == 0) {
    preferredScale = 1;
  }

  for (uint8_t scale = preferredScale; scale >= 1; --scale) {
    const uint16_t scaledWidth = static_cast<uint16_t>(width) * scale;
    const uint16_t scaledHeight = static_cast<uint16_t>(height) * scale;
    if (scaledWidth <= Config::kScreenWidth && scaledHeight <= Config::kScreenHeight) {
      return scale;
    }

    if (scale == 1) {
      break;
    }
  }

  return 1;
}

void drawAnimationFrame(uint8_t animationId, uint8_t frameIndex, bool drawFace, PetMood mood, int8_t offsetX = 0) {
  const AnimationDef& animation = Assets::animationOrSplash(animationId);
  if (animation.frameCount == 0) {
    return;
  }

  const SpriteFrame& frame = animation.frames[frameIndex % animation.frameCount];
  const uint8_t scale = fittingScale(frame.width, frame.height, Config::kPetScale);
  const int16_t drawX = ((Config::kScreenWidth - static_cast<int16_t>(frame.width) * scale) / 2) + offsetX;
  const int16_t drawY = (Config::kScreenHeight - static_cast<int16_t>(frame.height) * scale) / 2;

  drawBitmapScaled(drawX, drawY, frame.bitmap, frame.width, frame.height, scale, SSD1306_WHITE, frame.isVerticalLsb);
  if (drawFace && scale >= 2) {
    drawMoodFace(mood, drawX, drawY, frame.width, frame.height, scale);
  }
}

void drawBar(int16_t x, int16_t y, int16_t width, uint8_t value) {
  gDisplay.drawRect(x, y, width, 6, SSD1306_WHITE);
  const int16_t fillWidth = (value * (width - 2)) / 100;
  gDisplay.fillRect(x + 1, y + 1, fillWidth, 4, SSD1306_WHITE);
}

const __FlashStringHelper* menuLabel(MenuActionId action, bool lightsOn, ScreenBrightnessMode brightnessMode) {
  switch (action) {
    case MENU_FEED:
      return F("Feed");
    case MENU_PET:
      return F("Pet");
    case MENU_PLAY:
      return F("Play");
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
    case MENU_BACK:
    default:
      return F("Back");
  }
}

void drawMoodFace(PetMood mood, int16_t petX, int16_t petY, uint8_t frameWidth, uint8_t frameHeight, uint8_t scale) {
  const int16_t leftEyeX = petX + (frameWidth * scale) / 3;
  const int16_t rightEyeX = petX + ((frameWidth * scale) * 2) / 3;
  const int16_t eyeY = petY + (frameHeight * scale) / 3;
  const int16_t mouthY = petY + ((frameHeight * scale) * 3) / 4;
  const int16_t mouthLeftX = petX + (frameWidth * scale) / 3;
  const int16_t mouthRightX = petX + ((frameWidth * scale) * 2) / 3;
  const int16_t eyeSize = scale;

  gDisplay.fillRect(leftEyeX, eyeY, eyeSize, eyeSize, SSD1306_BLACK);
  gDisplay.fillRect(rightEyeX, eyeY, eyeSize, eyeSize, SSD1306_BLACK);

  switch (mood) {
    case MOOD_HAPPY:
      gDisplay.drawPixel(mouthLeftX - scale, mouthY - scale, SSD1306_BLACK);
      gDisplay.drawLine(mouthLeftX, mouthY, mouthRightX, mouthY, SSD1306_BLACK);
      gDisplay.drawPixel(mouthRightX + scale, mouthY - scale, SSD1306_BLACK);
      break;
    case MOOD_HUNGRY:
      gDisplay.drawLine(mouthLeftX, mouthY, mouthRightX, mouthY, SSD1306_BLACK);
      break;
    case MOOD_SAD:
      gDisplay.drawPixel(mouthLeftX - scale, mouthY + scale, SSD1306_BLACK);
      gDisplay.drawLine(mouthLeftX, mouthY, mouthRightX, mouthY, SSD1306_BLACK);
      gDisplay.drawPixel(mouthRightX + scale, mouthY + scale, SSD1306_BLACK);
      break;
    case MOOD_NEUTRAL:
    default:
      gDisplay.drawLine(mouthLeftX, mouthY, mouthRightX, mouthY, SSD1306_BLACK);
      break;
  }
}

void drawPet(const AppState& state) {
  uint8_t displayFrameIndex = state.animation.frameIndex;
  int8_t displayOffsetX = state.petOffsetX;

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

void drawDirtOverlay(const AppState& state) {
  if (state.dirtLevel == DIRT_NONE) {
    return;
  }

  const uint8_t dirtAnimationId = (state.dirtLevel == DIRT_BIG) ? state.poopBigAnimationId : state.poopSmallAnimationId;
  drawAnimationFrame(dirtAnimationId, state.animation.frameIndex, false, state.pet.mood, 0);
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

void drawSpriteScene(const AppState& state) {
  drawPet(state);
  drawDirtOverlay(state);

  if (state.lightsOn) {
    gDisplay.setTextSize(1);
    gDisplay.setCursor(0, 56);
    gDisplay.println(F("B2:Menu"));
  }
}

void drawMenuScene(const AppState& state) {
  const __FlashStringHelper* label = menuLabel(state.selectedMenuAction, state.lightsOn, state.brightnessMode);

  char menuText[22] = {};
  strncpy_P(menuText, reinterpret_cast<PGM_P>(label), sizeof(menuText) - 1);

  const uint8_t textSize = 2;
  const int16_t textWidth = static_cast<int16_t>(strlen(menuText)) * 6 * textSize;
  int16_t x = (Config::kScreenWidth - textWidth) / 2;
  if (x < 0) {
    x = 0;
  }
  const int16_t y = (Config::kScreenHeight / 2) - 8;

  gDisplay.setTextSize(1);
  gDisplay.setCursor(0, 0);
  gDisplay.println(F("Menu"));

  gDisplay.setTextSize(textSize);
  gDisplay.setCursor(x, y);
  gDisplay.print(menuText);

  gDisplay.setTextSize(1);
  gDisplay.setCursor(0, 56);
  gDisplay.println(F("B1< B2 OK B3>"));
}

void drawFeedMenuScene(const AppState& state) {
  char foodName[24] = {};
  foodDisplayName(state.selectedFoodAnimationId, foodName, sizeof(foodName));

  const uint8_t textSize = 2;
  const int16_t textWidth = static_cast<int16_t>(strlen(foodName)) * 6 * textSize;
  int16_t x = (Config::kScreenWidth - textWidth) / 2;
  if (x < 0) {
    x = 0;
  }
  const int16_t y = (Config::kScreenHeight / 2) - 8;

  gDisplay.setTextSize(1);
  gDisplay.setCursor(0, 0);
  gDisplay.println(F("Feed Select"));

  gDisplay.setTextSize(textSize);
  gDisplay.setCursor(x, y);
  gDisplay.print(foodName);

  gDisplay.setTextSize(1);
  gDisplay.setCursor(0, 56);
  gDisplay.println(F("B1< B2 Feed B3>"));
}

void drawFeedPlaybackScene(const AppState& state) {
  if (state.lightsOn) {
    drawPet(state);
    drawDirtOverlay(state);
    drawAnimationFrame(state.activeFoodAnimationId, state.foodFrameIndex, false, state.pet.mood);
  }

  gDisplay.setTextSize(1);
  gDisplay.setCursor(0, 56);
  gDisplay.println(F("Feeding... B2 Skip"));
}

void drawSplashScene(const AppState& state) {
  drawAnimationFrame(state.splashAnimationId, state.animation.frameIndex, false, state.pet.mood);
}

void drawStatusScene(const AppState& state) {
  gDisplay.setTextSize(1);
  gDisplay.setCursor(0, 0);
  gDisplay.println(F("Status"));

  gDisplay.setCursor(0, 16);
  gDisplay.print(F("Mood: "));
  gDisplay.println(moodLabel(state.pet.mood));

  gDisplay.setCursor(0, 28);
  gDisplay.print(F("Full"));
  drawBar(26, 28, 90, state.pet.hunger);

  gDisplay.setCursor(0, 38);
  gDisplay.print(F("Joy"));
  drawBar(26, 38, 90, state.pet.happiness);

  gDisplay.setCursor(0, 48);
  gDisplay.print(F("Sleep"));
  drawBar(34, 48, 82, state.pet.sleepiness);

  gDisplay.setCursor(0, 56);
  gDisplay.println(F("B2:Menu"));
}

void drawDevInfoScene(const AppState& state) {
  gDisplay.setTextSize(1);
  gDisplay.setCursor(0, 0);
  gDisplay.println(F("DevInfo"));

  gDisplay.setCursor(0, 10);
  gDisplay.print(F("Age: "));
  gDisplay.println(state.pet.ageTicks);

  gDisplay.setCursor(0, 20);
  gDisplay.print(F("Anim: "));
  gDisplay.println(Assets::animationName(state.animation.animationId));

  gDisplay.setCursor(0, 30);
  gDisplay.print(F("Frame: "));
  gDisplay.println(state.animation.frameIndex);

  gDisplay.setCursor(0, 40);
  gDisplay.print(F("Lights: "));
  gDisplay.println(state.lightsOn ? F("ON") : F("OFF"));

  gDisplay.setCursor(0, 50);
  gDisplay.print(F("Clean: "));
  gDisplay.println(state.canClean ? F("YES") : F("NO"));

  gDisplay.setCursor(78, 50);
  gDisplay.print(F("Rdy:"));
  gDisplay.println(Assets::animationCount());

  gDisplay.setCursor(0, 58);
  gDisplay.print(F("F L/N/D: "));
  gDisplay.print(state.likedFoodCount);
  gDisplay.print(F("/"));
  gDisplay.print(state.neutralFoodCount);
  gDisplay.print(F("/"));
  gDisplay.print(state.dislikedFoodCount);
}

}  // namespace

namespace Renderer {

bool begin() {
  if (Config::kSdaPin >= 0 && Config::kSclPin >= 0) {
    Wire.begin(Config::kSdaPin, Config::kSclPin);
  } else {
    Wire.begin();
  }

  if (gDisplay.begin(SSD1306_SWITCHCAPVCC, Config::kScreenAddressPrimary)) {
    return true;
  }

  if (Config::kScreenAddressSecondary != Config::kScreenAddressPrimary) {
    if (gDisplay.begin(SSD1306_SWITCHCAPVCC, Config::kScreenAddressSecondary)) {
      return true;
    }
  }

  return false;
}

void render(const AppState& state) {
  const bool shouldDim = (!state.lightsOn) || (state.brightnessMode == BRIGHTNESS_DIM);
  if (shouldDim != gDisplayDimmed) {
    gDisplay.dim(shouldDim);
    gDisplayDimmed = shouldDim;
  }

  gDisplay.clearDisplay();
  gDisplay.setTextColor(SSD1306_WHITE);

  if (state.scene == SCENE_SPLASH) {
    drawSplashScene(state);
  } else if (state.scene == SCENE_PET) {
    drawSpriteScene(state);
  } else if (state.scene == SCENE_FEED_MENU) {
    drawFeedMenuScene(state);
  } else if (state.scene == SCENE_FEED_PLAYBACK) {
    drawFeedPlaybackScene(state);
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