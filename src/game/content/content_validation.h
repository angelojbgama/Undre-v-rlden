#pragma once

#include "game/content/content_dto.h"

#include <string>
#include <vector>

namespace underworld::game::content {

enum class ContentDiagnosticSeverity { error, warning };
enum class ContentKind {
    tileset, attack, projectile, behavior, enemy, item, object, pickup, npc,
    dialogue, quest, authoringDescriptor, tileSemantic, stamp, playerProgression, rewardProfile, rewardGrant, shop, npcVisual, presentationEffect
};

struct ContentDiagnostic final {
    ContentDiagnosticSeverity severity{ContentDiagnosticSeverity::error};
    std::string code;
    std::string message;
    ContentKind kind{ContentKind::tileset};
    simulation::DefinitionId definitionId{};
    std::string field;
    [[nodiscard]] bool operator==(const ContentDiagnostic&) const = default;
};

struct ContentValidationReport final {
    std::vector<ContentDiagnostic> diagnostics;
    [[nodiscard]] bool hasErrors() const noexcept;
    [[nodiscard]] bool valid() const noexcept { return !hasErrors(); }
};

class ContentValidator final {
public:
    [[nodiscard]] ContentValidationReport validate(const AuthoredContentPack& pack) const;
};

} // namespace underworld::game::content
