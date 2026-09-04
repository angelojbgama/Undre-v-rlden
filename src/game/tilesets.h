#pragma once

#include "engine/simulation/definition_id.h"
#include "engine/world/tile.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace underworld::assets { class AssetManager; }
namespace underworld::platform { class ImageDecoder; }
namespace underworld::render { class Image; }

namespace underworld::game {

struct TilesetDefinition final {
    simulation::DefinitionId id{};
    std::string displayName;
    std::string relativeAssetPath;
    std::uint16_t tileSize{};
    std::uint32_t columns{};
    std::uint32_t rows{};

    [[nodiscard]] std::uint32_t tileCount() const noexcept { return columns * rows; }
};

class TilesetCatalog final {
public:
    void add(TilesetDefinition definition);
    [[nodiscard]] const TilesetDefinition* find(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const TilesetDefinition& require(const simulation::DefinitionId& id) const;
    [[nodiscard]] const std::vector<TilesetDefinition>& definitions() const noexcept {
        return definitions_;
    }

private:
    std::vector<TilesetDefinition> definitions_;
};

// Runtime IDs are process-local. MapData and DMAP keep only TilesetDefinition IDs.
class RuntimeTilesetCatalog final {
public:
    explicit RuntimeTilesetCatalog(const TilesetCatalog& definitions);

    [[nodiscard]] const std::unordered_map<simulation::DefinitionId, world::TilesetId,
                                           simulation::DefinitionIdHash>& mapping() const noexcept {
        return mapping_;
    }
    [[nodiscard]] world::TilesetId requireRuntimeId(const simulation::DefinitionId& id) const;
    [[nodiscard]] const TilesetDefinition* definition(world::TilesetId id) const noexcept;

private:
    const TilesetCatalog* definitions_{};
    std::unordered_map<simulation::DefinitionId, world::TilesetId,
                       simulation::DefinitionIdHash> mapping_;
    std::unordered_map<world::TilesetId, const TilesetDefinition*> reverse_;
};

struct LoadedTilesetVisual final {
    std::shared_ptr<const render::Image> image;
    world::TileAtlasLayout atlas;

    LoadedTilesetVisual(std::shared_ptr<const render::Image> image, world::TileAtlasLayout atlas)
        : image(std::move(image)), atlas(std::move(atlas)) {}
};

// Loaded visual data remains separate from immutable content definitions.
class TilesetVisualCatalog final {
public:
    bool load(const TilesetDefinition& definition, world::TilesetId runtimeId,
              assets::AssetManager& assets, platform::ImageDecoder& decoder,
              const std::filesystem::path& assetRoot, std::string& error);
    void add(world::TilesetId runtimeId, std::shared_ptr<const render::Image> image,
             const TilesetDefinition& definition);
    [[nodiscard]] const LoadedTilesetVisual* find(world::TilesetId runtimeId) const noexcept;

private:
    std::unordered_map<world::TilesetId, std::unique_ptr<LoadedTilesetVisual>> visuals_;
};

} // namespace underworld::game
