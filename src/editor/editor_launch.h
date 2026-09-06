#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace underworld::editor {

struct EditorLaunchOptions final {
    std::optional<std::filesystem::path> assetRoot;
    std::optional<std::filesystem::path> contentRoot;
};

[[nodiscard]] std::optional<EditorLaunchOptions> parseEditorLaunchOptions(
    int argc, const wchar_t* const* argv, std::string& error);
[[nodiscard]] std::optional<EditorLaunchOptions> parseEditorLaunchOptions(
    int argc, const char* const* argv, std::string& error);

} // namespace underworld::editor
