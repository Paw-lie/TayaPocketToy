#include "Persistence.h"

#include <Preferences.h>

#include "Config.h"

namespace {

struct SavedState {
  uint8_t version;
  uint8_t hunger;
  uint8_t happiness;
  uint8_t mood;
  uint8_t accessory;
  uint32_t ageTicks;
  uint32_t checksum;
};

Preferences gPreferences;

uint32_t checksumFor(const SavedState& state) {
  return 0xB10B5000UL ^
         static_cast<uint32_t>(state.version) ^
         (static_cast<uint32_t>(state.hunger) << 8) ^
         (static_cast<uint32_t>(state.happiness) << 16) ^
         (static_cast<uint32_t>(state.mood) << 24) ^
         state.ageTicks ^
         static_cast<uint32_t>(state.accessory);
}

}  // namespace

namespace Persistence {

void begin() {
  gPreferences.begin(Config::kSaveNamespace, false);
}

void load(AppState& state) {
  if (gPreferences.getBytesLength(Config::kSaveKey) != sizeof(SavedState)) {
    return;
  }

  SavedState saved = {};
  const size_t bytesRead = gPreferences.getBytes(Config::kSaveKey, &saved, sizeof(saved));
  if (bytesRead != sizeof(saved)) {
    return;
  }

  if (saved.version != Config::kSaveVersion) {
    return;
  }

  if (saved.checksum != checksumFor(saved)) {
    return;
  }

  state.pet.hunger = saved.hunger;
  state.pet.happiness = saved.happiness;
  state.pet.mood = static_cast<PetMood>(saved.mood);
  state.pet.accessory = static_cast<AccessoryId>(saved.accessory);
  state.pet.ageTicks = saved.ageTicks;
}

void save(const AppState& state) {
  SavedState saved = {};
  saved.version = Config::kSaveVersion;
  saved.hunger = state.pet.hunger;
  saved.happiness = state.pet.happiness;
  saved.mood = state.pet.mood;
  saved.accessory = state.pet.accessory;
  saved.ageTicks = state.pet.ageTicks;
  saved.checksum = checksumFor(saved);

  gPreferences.putBytes(Config::kSaveKey, &saved, sizeof(saved));
}

}  // namespace Persistence