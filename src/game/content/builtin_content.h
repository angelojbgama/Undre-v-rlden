#pragma once

#include "game/content/content_dto.h"
#include "game/game_content.h"

namespace underworld::game::content {

[[nodiscard]] AuthoredContentPack makeBuiltinAuthoredContent();
[[nodiscard]] GameContentRegistry compileBuiltinContentOrThrow();

} // namespace underworld::game::content
