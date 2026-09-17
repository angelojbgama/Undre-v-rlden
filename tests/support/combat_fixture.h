// Combat content fixture for tests and playtest tooling.
//
// The builtin content pack no longer carries concrete attack or
// projectile definitions: those are authored content. Tests that need a
// playable combat content set load this fixture workspace through the
// real production pipeline (builtin base + authored overlay), exactly
// like the game loads content/definitions.
#pragma once

#include "game/content/builtin_content.h"
#include "game/content/content_compiler.h"
#include "game/content/content_source.h"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace underworld::game::content {
namespace combat_fixture {

[[nodiscard]] inline std::filesystem::path fixtureRoot() {
    // Resolved from the current working directory, matching how the
    // portable suite and build.bat run the test binary (repo root).
    return std::filesystem::path{"tests/fixtures/combat-content/definitions"};
}

} // namespace combat_fixture

// Equivalent of the old builtin-only combat pack: builtin base layer
// with the authored combat fixture overlaid on top.
[[nodiscard]] inline AuthoredContentPack makeCombatAuthoredContent() {
    const auto loaded = loadContentSource(
        {ContentSourceKind::workspaceDirectory,
         combat_fixture::fixtureRoot()});
    if (!loaded) {
        std::string message = "combat fixture workspace failed to load";
        for (const auto& diagnostic : loaded.diagnostics) {
            message += "\n  " + formatContentWorkspaceDiagnostic(diagnostic);
        }
        throw std::runtime_error(message);
    }
    return loaded.content->authored;
}

[[nodiscard]] inline GameContentRegistry compileCombatContentOrThrow() {
    const auto result = compileContent(makeCombatAuthoredContent());
    if (!result) {
        std::string message = "combat fixture compilation failed";
        for (const auto& diagnostic : result.report.diagnostics) {
            if (diagnostic.severity == ContentDiagnosticSeverity::error) {
                message += " [" + diagnostic.code + "] " + diagnostic.message;
            }
        }
        throw std::runtime_error(message);
    }
    return *result.registry;
}

} // namespace underworld::game::content
