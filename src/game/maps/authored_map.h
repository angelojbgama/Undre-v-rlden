#pragma once

#include "engine/data/json.h"
#include "game/game_content.h"
#include "game/maps/map_data.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace underworld::game::maps {

struct AuthoredMapSource final {
    MapData map;
};

enum class AuthoredMapDiagnosticStage { decode, validation, compile, io };
struct AuthoredMapDiagnostic final {
    AuthoredMapDiagnosticStage stage{AuthoredMapDiagnosticStage::decode};
    std::string code;
    std::string message;
    std::string path;
    std::size_t line{1};
    std::size_t column{1};
};
struct AuthoredMapDecodeResult final {
    std::optional<AuthoredMapSource> source;
    std::vector<AuthoredMapDiagnostic> diagnostics;
};
struct MapCompileResult final {
    std::optional<MapData> map;
    std::vector<AuthoredMapDiagnostic> diagnostics;
};

[[nodiscard]] AuthoredMapDecodeResult decodeAuthoredMapJson(std::string_view json);
[[nodiscard]] std::string encodeAuthoredMapJson(const AuthoredMapSource& source);
[[nodiscard]] AuthoredMapDecodeResult readAuthoredMapFile(const std::filesystem::path& path);
[[nodiscard]] bool writeAuthoredMapFile(const std::filesystem::path& path,
                                        const AuthoredMapSource& source, std::string& error);
[[nodiscard]] MapCompileResult compileAuthoredMap(const AuthoredMapSource& source,
                                                  const game::GameContentRegistry& content);

} // namespace underworld::game::maps
