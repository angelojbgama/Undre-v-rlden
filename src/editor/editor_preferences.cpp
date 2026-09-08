#include "editor/editor_preferences.h"

#include "editor/editor_layout.h"

#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <charconv>

namespace underworld::editor {
namespace {
int readWidth(const std::string& text, std::string_view key, int fallback, int minimum) noexcept {
    const auto marker = text.find('"' + std::string(key) + '"');
    if (marker == std::string::npos) return fallback;
    const auto colon = text.find(':', marker + key.size() + 2);
    if (colon == std::string::npos) return fallback;
    const auto begin = text.find_first_of("-0123456789", colon + 1);
    if (begin == std::string::npos) return fallback;
    const auto end = text.find_first_not_of("0123456789", begin + 1);
    int value = fallback;
    const auto parsed = std::from_chars(text.data() + begin,
        text.data() + (end == std::string::npos ? text.size() : end), value);
    if (parsed.ec != std::errc{}) return fallback;
    return std::clamp(value, minimum, EditorLayoutMetrics::maximumPanel);
}
}

std::filesystem::path defaultEditorPreferencesPath() {
#ifdef _WIN32
    if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA"); local && *local) {
        return std::filesystem::path(local) / "DungeonUnderworld" / "ContentStudio" / "settings.json";
    }
#else
    if (const char* config = std::getenv("XDG_CONFIG_HOME"); config && *config) {
        return std::filesystem::path(config) / "DungeonUnderworld" / "ContentStudio" / "settings.json";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config" / "DungeonUnderworld" / "ContentStudio" / "settings.json";
    }
#endif
    std::error_code error;
    return std::filesystem::temp_directory_path(error) / "DungeonUnderworld" / "ContentStudio" / "settings.json";
}

EditorPreferences loadEditorPreferences(const std::filesystem::path& path) noexcept {
    EditorPreferences preferences;
    std::ifstream input(path, std::ios::binary);
    if (!input) return preferences;
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string value = contents.str();
    const auto language = value.find("\"language\"");
    const auto colon = language == std::string::npos ? std::string::npos : value.find(':', language + 10);
    const auto firstQuote = colon == std::string::npos ? std::string::npos : value.find('"', colon + 1);
    const auto secondQuote = firstQuote == std::string::npos ? std::string::npos : value.find('"', firstQuote + 1);
    if (firstQuote != std::string::npos && secondQuote != std::string::npos) {
        const std::string code = value.substr(firstQuote + 1, secondQuote - firstQuote - 1);
        if (code == editorLanguageCode(EditorLanguage::englishUnitedStates)) {
            preferences.language = EditorLanguage::englishUnitedStates;
        }
    }
    preferences.leftPanelWidth = readWidth(value, "leftPanelWidth", preferences.leftPanelWidth,
                                           EditorLayoutMetrics::minimumLeft);
    preferences.rightPanelWidth = readWidth(value, "rightPanelWidth", preferences.rightPanelWidth,
                                            EditorLayoutMetrics::minimumRight);
    return preferences;
}

bool saveEditorPreferences(const std::filesystem::path& path,
                           const EditorPreferences& preferences,
                           std::string& error) noexcept {
    error.clear();
    std::error_code filesystemError;
    std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError) { error = "Could not create editor preferences directory: " + filesystemError.message(); return false; }
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) { error = "Could not write editor preferences"; return false; }
        const auto widths = clampPanelWidths(EditorLayoutMetrics::minimumViewport +
                                             preferences.leftPanelWidth + preferences.rightPanelWidth,
                                             {preferences.leftPanelWidth, preferences.rightPanelWidth});
        output << "{\n  \"language\": \"" << editorLanguageCode(preferences.language)
               << "\",\n  \"leftPanelWidth\": " << widths.left
               << ",\n  \"rightPanelWidth\": " << widths.right << "\n}\n";
        output.flush();
        if (!output) { error = "Could not flush editor preferences"; output.close(); std::filesystem::remove(temporary, filesystemError); return false; }
    }
    std::filesystem::remove(path, filesystemError);
    filesystemError.clear();
    std::filesystem::rename(temporary, path, filesystemError);
    if (filesystemError) {
        error = "Could not replace editor preferences: " + filesystemError.message();
        std::filesystem::remove(temporary, filesystemError);
        return false;
    }
    return true;
}

} // namespace underworld::editor
