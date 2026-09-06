#pragma once
#include "game/content/content_dto.h"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace underworld::game::content {
struct ContentJsonDefinitionOrigin final {
    std::string category;
    simulation::DefinitionId definitionId{};
    std::size_t line{};
    std::size_t column{};
    std::string jsonPath;
};
struct ContentJsonDiagnostic final { std::size_t line{}; std::size_t column{}; std::string path; std::string message; };
struct ContentJsonDecodeResult final {
    std::optional<AuthoredContentPack> content;
    std::vector<ContentJsonDiagnostic> diagnostics;
    std::vector<ContentJsonDefinitionOrigin> origins;
    [[nodiscard]] explicit operator bool() const noexcept { return content.has_value() && diagnostics.empty(); }
};
[[nodiscard]] ContentJsonDecodeResult decodeAuthoredContentJson(std::string_view text);
[[nodiscard]] std::string encodeAuthoredContentJson(const AuthoredContentPack& content);
[[nodiscard]] ContentJsonDecodeResult readAuthoredContentJsonFile(const std::filesystem::path& path);
[[nodiscard]] bool writeAuthoredContentJsonFile(const std::filesystem::path& path, const AuthoredContentPack& content, std::string& error);
} // namespace underworld::game::content
