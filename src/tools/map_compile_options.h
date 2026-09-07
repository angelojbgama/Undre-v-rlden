#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace underworld::tools {

struct MapCompileOptions final {
    std::optional<std::filesystem::path> contentRoot;
    std::filesystem::path source;
    std::filesystem::path output;
};

[[nodiscard]] inline std::optional<MapCompileOptions> parseMapCompileOptions(
    int argc, const char* const* argv, std::string& error) {
    error.clear();
    MapCompileOptions result;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] == nullptr ? "" : argv[index];
        if (argument == "--content" || argument.rfind("--content=", 0) == 0) {
            if (result.contentRoot) {
                error = "duplicate --content option";
                return std::nullopt;
            }
            std::string value;
            if (argument == "--content") {
                if (++index >= argc || argv[index] == nullptr ||
                    std::string(argv[index]).empty()) {
                    error = "--content requires a directory";
                    return std::nullopt;
                }
                value = argv[index];
            } else {
                value = argument.substr(std::string{"--content="}.size());
            }
            if (value.empty()) {
                error = "--content requires a non-empty directory";
                return std::nullopt;
            }
            result.contentRoot = std::filesystem::path{std::move(value)};
        } else if (!argument.empty() && argument[0] == '-') {
            error = "unknown option: " + argument;
            return std::nullopt;
        } else if (result.source.empty()) {
            result.source = argument;
        } else if (result.output.empty()) {
            result.output = argument;
        } else {
            error = "too many positional arguments";
            return std::nullopt;
        }
    }
    if (result.source.empty() || result.output.empty()) {
        error = "source and output paths are required";
        return std::nullopt;
    }
    return result;
}

} // namespace underworld::tools
