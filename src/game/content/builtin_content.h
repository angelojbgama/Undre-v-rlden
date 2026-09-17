#pragma once

#include "game/content/content_dto.h"
#include "game/game_content.h"

namespace underworld::game::content {

// Fallback base layer for non-combat content (tiles, enemies, behaviors,
// visuals, dialogs, quests). Concrete attacks and projectiles are authored
// content and must come from a workspace overlay.
[[nodiscard]] AuthoredContentPack makeBuiltinAuthoredContent();

} // namespace underworld::game::content
