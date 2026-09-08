#include "editor/editor_preferences.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace underworld::editor {

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
        output << "{\n  \"language\": \"" << editorLanguageCode(preferences.language) << "\"\n}\n";
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
