#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/persistent_id.h"
#include "engine/world/tile.h"
#include "game/maps/map_data.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace underworld::game::authoring {

enum class SemanticConfidence { confirmed, probable, unverified };
enum class TileRole { floor, wall, corner, ledge, opening, detail, unknown };
enum class TileTopology { unknown, interior, straightHorizontal, straightVertical, outerCorner,
                          innerCorner, cap, junction, architecturalDetail };
enum class EdgeProfile { unknown, floor, masonry, voidEdge, terminal };

struct TileSemanticDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId tilesetId{};
    std::uint32_t sourceIndex{};
    std::string family;
    TileRole role{TileRole::unknown};
    TileTopology topology{TileTopology::unknown};
    EdgeProfile north{EdgeProfile::unknown};
    EdgeProfile east{EdgeProfile::unknown};
    EdgeProfile south{EdgeProfile::unknown};
    EdgeProfile west{EdgeProfile::unknown};
    std::string preferredLayer;
    bool flipXAllowed{};
    SemanticConfidence visualConfidence{SemanticConfidence::confirmed};
    SemanticConfidence semanticConfidence{SemanticConfidence::unverified};
    SemanticConfidence gameplayConfidence{SemanticConfidence::unverified};
};

struct StampCell final { int x{}; int y{}; simulation::DefinitionId tileId{}; };
struct StampDefinition final {
    simulation::DefinitionId id{};
    std::string displayName;
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<StampCell> cells;
    core::PointI anchor{};
    bool flipXAllowed{};
    bool atomic{};
    SemanticConfidence confidence{SemanticConfidence::unverified};
};

class AuthoringSemanticRegistry final {
public:
    AuthoringSemanticRegistry();
    [[nodiscard]] const TileSemanticDefinition* findTile(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const TileSemanticDefinition* findTile(const simulation::DefinitionId& tileset,
                                                          std::uint32_t sourceIndex) const noexcept;
    [[nodiscard]] const StampDefinition* findStamp(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const std::vector<TileSemanticDefinition>& tiles() const noexcept { return tiles_; }
    [[nodiscard]] const std::vector<StampDefinition>& stamps() const noexcept { return stamps_; }
    [[nodiscard]] std::vector<const TileSemanticDefinition*> tilesByFamily(const std::string& family) const;
    [[nodiscard]] bool edgesCompatible(EdgeProfile first, EdgeProfile second) const noexcept;
private:
    void addTile(TileSemanticDefinition definition);
    void addStamp(StampDefinition definition);
    std::vector<TileSemanticDefinition> tiles_;
    std::vector<StampDefinition> stamps_;
    std::unordered_map<std::string, std::size_t> tileById_;
    std::unordered_map<std::string, std::size_t> tileByReference_;
    std::unordered_map<std::string, std::size_t> stampById_;
};

enum class SemanticIssueSeverity { error, warning, info };
struct SemanticIssue final {
    SemanticIssueSeverity severity{SemanticIssueSeverity::info};
    std::string code;
    std::string message;
    std::optional<core::WorldPointI> location;
    std::optional<std::size_t> layer;
    simulation::DefinitionId semanticId{};
};
struct SemanticValidationReport final {
    std::vector<SemanticIssue> issues;
    [[nodiscard]] std::size_t warningCount() const noexcept;
    [[nodiscard]] std::size_t infoCount() const noexcept;
};

class MapSemanticValidator final {
public:
    [[nodiscard]] SemanticValidationReport validate(const maps::MapData& map,
        const AuthoringSemanticRegistry& semantics) const;
};

[[nodiscard]] maps::MapTileReference tileReferenceFor(const TileSemanticDefinition& definition,
                                                       world::TileFlags flags = world::TileFlags::none);
[[nodiscard]] const char* toString(TileRole value) noexcept;
[[nodiscard]] const char* toString(TileTopology value) noexcept;
[[nodiscard]] const char* toString(SemanticConfidence value) noexcept;

} // namespace underworld::game::authoring
