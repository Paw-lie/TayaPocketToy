#pragma once

#include "AppTypes.h"

namespace Persistence {

void begin();
void load(AppState& state);
void save(const AppState& state);

}  // namespace Persistence