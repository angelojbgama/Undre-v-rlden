#pragma once

#include "engine/data/json.h"
#include "game/game_content.h"
#include "game/maps/map_data.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace underworld::game::maps {

struct AuthoredMapGeometry final {
    simulation::MapId id{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint16_t tileSize{};
    std::vector<MapTileReference> tileReferences;
    std::vector<MapTileLayer> layers;
    std::vector<std::uint8_t> collision;
    std::vector<PlayerSpawn> playerSpawns;
    std::vector<EnemyPlacement> enemies;
    std::vector<NpcPlacement> npcs;
    std::vector<ObjectPlacement> objects;
    std::vector<PickupPlacement> pickups;
    std::vector<MapLink> links;
    [[nodiscard]] bool operator==(const AuthoredMapGeometry&) const noexcept = default;
};

enum class AuthoredPropertyValueKind { boolean, integer, enumeration,
                                      definitionReference, instanceReference };
struct AuthoredPropertyValue final {
    AuthoredPropertyValueKind kind{AuthoredPropertyValueKind::integer};
    bool booleanValue{};
    std::int64_t integerValue{};
    std::string textValue;
    simulation::DefinitionId definitionValue{};
    simulation::PersistentInstanceId instanceValue{};
    [[nodiscard]] bool operator==(const AuthoredPropertyValue&) const noexcept = default;
};
struct AuthoredPlacementOverride final {
    simulation::PersistentInstanceId instanceId{};
    std::string propertyId;
    AuthoredPropertyValue value;
    [[nodiscard]] bool operator==(const AuthoredPlacementOverride&) const noexcept = default;
};

struct AuthoredMapSource final {
    AuthoredMapGeometry geometry;
    std::vector<MapRegionDefinition> regions;
    std::vector<WorldRuleDefinition> worldRules;
    std::vector<EncounterDefinition> encounters;
    std::vector<AuthoredPlacementOverride> placementOverrides;
    [[nodiscard]] bool operator==(const AuthoredMapSource&) const noexcept = default;
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
[[nodiscard]] MapData mapDataFromAuthored(const AuthoredMapSource& source);
[[nodiscard]] AuthoredMapSource authoredMapFromMapData(const MapData& data);

} // namespace underworld::game::maps
