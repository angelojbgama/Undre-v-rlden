#pragma once

#include "editor/editor_localization.h"

#include <filesystem>
#include <string>

namespace underworld::editor {

struct EditorPreferences final {
    EditorLanguage language{EditorLanguage::portugueseBrazil};
};

[[nodiscard]] std::filesystem::path defaultEditorPreferencesPath();
[[nodiscard]] EditorPreferences loadEditorPreferences(const std::filesystem::path& path) noexcept;
[[nodiscard]] bool saveEditorPreferences(const std::filesystem::path& path,
                                          const EditorPreferences& preferences,
                                          std::string& error) noexcept;

} // namespace underworld::editor
