#pragma once

#include "game/content/content_validation.h"
#include "game/game_content.h"

#include <optional>

namespace underworld::game::content {

struct ContentCompileResult final {
    std::optional<GameContentRegistry> registry;
    ContentValidationReport report;
    [[nodiscard]] explicit operator bool() const noexcept {
        return registry.has_value() && report.valid();
    }
};

class ContentCompiler final {
public:
    [[nodiscard]] ContentCompileResult compile(const AuthoredContentPack& authored) const;
};

[[nodiscard]] ContentCompileResult compileContent(const AuthoredContentPack& authored);

} // namespace underworld::game::content
