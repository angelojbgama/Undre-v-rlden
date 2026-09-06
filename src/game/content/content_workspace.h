#pragma once

#include "game/content/content_json.h"
#include "game/game_content.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace underworld::game::content {

struct ContentSourceLocation final {
    std::filesystem::path sourcePath;
    std::string category;
    simulation::DefinitionId definitionId{};
    std::size_t line{};
    std::size_t column{};
    std::string jsonPath;
};

struct ContentSourceMap final {
    std::vector<ContentSourceLocation> definitions;
    [[nodiscard]] const ContentSourceLocation* find(
        std::string_view category, const simulation::DefinitionId& id) const noexcept;
};

enum class ContentWorkspaceDiagnosticStage { io, decode, merge, validation, compile };

struct ContentWorkspaceDiagnostic final {
    ContentWorkspaceDiagnosticStage stage{ContentWorkspaceDiagnosticStage::io};
    std::filesystem::path sourcePath;
    std::filesystem::path relatedSourcePath;
    std::size_t line{};
    std::size_t column{};
    std::string jsonPath;
    std::string code;
    std::string category;
    simulation::DefinitionId definitionId{};
    std::string message;
};

struct LoadedContentWorkspace final {
    AuthoredContentPack authored;
    GameContentRegistry registry;
    ContentSourceMap sources;
};

struct ContentWorkspaceLoadResult final {
    std::optional<LoadedContentWorkspace> workspace;
    std::vector<ContentWorkspaceDiagnostic> diagnostics;
    [[nodiscard]] explicit operator bool() const noexcept { return workspace.has_value() && diagnostics.empty(); }
};

[[nodiscard]] ContentWorkspaceLoadResult loadContentWorkspaceFiles(
    std::span<const std::filesystem::path> files);

} // namespace underworld::game::content
