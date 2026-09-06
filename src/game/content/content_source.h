#pragma once

#include "game/content/content_workspace.h"

#include <filesystem>
#include <optional>
#include <string>

namespace underworld::game::content {

enum class ContentSourceKind { builtin, workspaceDirectory };

struct ContentSourceSelection final {
    ContentSourceKind kind{ContentSourceKind::builtin};
    std::filesystem::path workspaceRoot;
};

struct LoadedContentBundle final {
    ContentSourceKind sourceKind{ContentSourceKind::builtin};
    std::filesystem::path sourceRoot;
    std::size_t sourceFileCount{};
    AuthoredContentPack authored;
    GameContentRegistry registry;
    ContentSourceMap provenance;
};

struct ContentSourceLoadResult final {
    std::optional<LoadedContentBundle> content;
    std::vector<ContentWorkspaceDiagnostic> diagnostics;
    [[nodiscard]] explicit operator bool() const noexcept {
        return content.has_value() && diagnostics.empty();
    }
};

[[nodiscard]] ContentSourceLoadResult loadContentSource(
    const ContentSourceSelection& selection);

[[nodiscard]] std::string formatContentWorkspaceDiagnostic(
    const ContentWorkspaceDiagnostic& diagnostic);

} // namespace underworld::game::content
