#include "editor/editor_launch.h"

namespace underworld::editor {
namespace {
bool consume(const std::string& argument, const char* name, int& index, int argc,
             const char* const* argv, std::optional<std::filesystem::path>& target,
             std::string& error) {
    const std::string option{name}; std::string value;
    if (argument == option) {
        if (index + 1 >= argc || argv[index + 1] == nullptr || std::string(argv[index + 1]).empty()) { error = option + " requires a value"; return false; }
        value = argv[++index];
    } else if (argument.rfind(option + "=", 0) == 0) { value = argument.substr(option.size() + 1); if (value.empty()) { error = option + " requires a value"; return false; } }
    else return true;
    if (target) { error = "duplicate " + option + " option"; return false; }
    target = std::filesystem::path(value); return true;
}
bool consume(const std::wstring& argument, const wchar_t* name, int& index, int argc,
             const wchar_t* const* argv, std::optional<std::filesystem::path>& target,
             std::string& error) {
    const std::wstring option{name}; std::wstring value;
    if (argument == option) {
        if (index + 1 >= argc || argv[index + 1] == nullptr || std::wstring(argv[index + 1]).empty()) { error = "editor option requires a value"; return false; }
        value = argv[++index];
    } else if (argument.rfind(option + L"=", 0) == 0) { value = argument.substr(option.size() + 1); if (value.empty()) { error = "editor option requires a value"; return false; } }
    else return true;
    if (target) { error = "duplicate editor option"; return false; }
    target = std::filesystem::path(value); return true;
}
}

std::optional<EditorLaunchOptions> parseEditorLaunchOptions(int argc, const wchar_t* const* argv, std::string& error) {
    error.clear(); EditorLaunchOptions result;
    for (int index = 1; index < argc; ++index) {
        const std::wstring arg = argv[index] ? argv[index] : L"";
        if (arg == L"--asset-root" || arg.rfind(L"--asset-root=", 0) == 0) { if (!consume(arg, L"--asset-root", index, argc, argv, result.assetRoot, error)) return std::nullopt; }
        else if (arg == L"--content" || arg.rfind(L"--content=", 0) == 0) { if (!consume(arg, L"--content", index, argc, argv, result.contentRoot, error)) return std::nullopt; }
        else { error = "unknown editor option"; return std::nullopt; }
    }
    return result;
}

std::optional<EditorLaunchOptions> parseEditorLaunchOptions(int argc, const char* const* argv, std::string& error) {
    error.clear(); EditorLaunchOptions result;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index] ? argv[index] : "";
        if (arg == "--asset-root" || arg.rfind("--asset-root=", 0) == 0) { if (!consume(arg, "--asset-root", index, argc, argv, result.assetRoot, error)) return std::nullopt; }
        else if (arg == "--content" || arg.rfind("--content=", 0) == 0) { if (!consume(arg, "--content", index, argc, argv, result.contentRoot, error)) return std::nullopt; }
        else { error = "unknown editor option: " + arg; return std::nullopt; }
    }
    return result;
}
} // namespace underworld::editor
