#pragma once

#include "game/maps/authored_map.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace underworld::game::maps {

inline constexpr unsigned authoredWorldVersion = 1;

struct AuthoredWorldSource final {
    simulation::MapId entryMapId{};
    std::vector<AuthoredMapSource> maps;
};

enum class AuthoredWorldDiagnosticStage { decode, validation, compile, io };

struct AuthoredWorldDiagnostic final {
    AuthoredWorldDiagnosticStage stage{AuthoredWorldDiagnosticStage::decode};
    std::string code;
    std::string message;
    std::string path;
    std::string mapId;
    std::string linkId;
    std::string targetMapId;
    std::string targetSpawnId;
    std::size_t line{1};
    std::size_t column{1};
};

struct AuthoredWorldDecodeResult final {
    std::optional<AuthoredWorldSource> source;
    std::vector<AuthoredWorldDiagnostic> diagnostics;
};

struct WorldValidationResult final {
    std::vector<AuthoredWorldDiagnostic> diagnostics;
    [[nodiscard]] bool valid() const noexcept { return diagnostics.empty(); }
};

struct CompiledWorldMap final {
    simulation::MapId id{};
    MapData data;
};

struct WorldCompileResult final {
    std::vector<CompiledWorldMap> maps;
    std::vector<AuthoredWorldDiagnostic> diagnostics;
    [[nodiscard]] bool valid() const noexcept { return diagnostics.empty(); }
};

[[nodiscard]] AuthoredWorldDecodeResult decodeAuthoredWorldJson(std::string_view json);
[[nodiscard]] std::string encodeAuthoredWorldJson(const AuthoredWorldSource& source);
[[nodiscard]] AuthoredWorldDecodeResult readAuthoredWorldFile(const std::filesystem::path& path);
[[nodiscard]] bool writeAuthoredWorldFile(const std::filesystem::path& path,
                                           const AuthoredWorldSource& source,
                                           std::string& error);
[[nodiscard]] WorldValidationResult validateAuthoredWorld(const AuthoredWorldSource& source);
[[nodiscard]] WorldCompileResult compileAuthoredWorld(const AuthoredWorldSource& source,
                                                       const game::GameContentRegistry& content);

} // namespace underworld::game::maps
