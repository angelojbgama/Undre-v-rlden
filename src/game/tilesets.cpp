#include "game/tilesets.h"

#include "engine/assets/asset_manager.h"
#include "engine/platform/image_decoder.h"
#include "engine/render/image.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace underworld::game {

void TilesetCatalog::add(TilesetDefinition definition) {
    if (definition.id.empty() || definition.displayName.empty() ||
        definition.relativeAssetPath.empty() || definition.tileSize == 0 ||
        definition.columns == 0 || definition.rows == 0 ||
        definition.columns > std::numeric_limits<std::uint32_t>::max() / definition.rows) {
        throw std::invalid_argument("tileset definition metadata is invalid");
    }
    const auto tileCount = definition.tileCount();

    for (std::size_t index = 0;
         index < definition.tileCollisions.size();
         ++index) {
        const auto& collision =
            definition.tileCollisions[index];

        if (collision.sourceIndex >= tileCount) {
            throw std::invalid_argument(
                "tileset tile collision source index is invalid");
        }

        for (std::size_t previous = 0;
             previous < index;
             ++previous) {
            if (definition.tileCollisions[previous].sourceIndex ==
                collision.sourceIndex) {
                throw std::invalid_argument(
                    "tileset tile collision source index is duplicated");
            }
        }

        for (const auto region : collision.regions) {
            const auto right =
                static_cast<std::int64_t>(region.x) +
                region.width;

            const auto bottom =
                static_cast<std::int64_t>(region.y) +
                region.height;

            if (region.x < 0 ||
                region.y < 0 ||
                region.width <= 0 ||
                region.height <= 0 ||
                right > definition.tileSize ||
                bottom > definition.tileSize) {
                throw std::invalid_argument(
                    "tileset tile collision region is outside tile bounds");
            }
        }
    }

    if (find(definition.id)) {
        throw std::logic_error(
            "duplicate tileset definition id");
    }

    definitions_.push_back(
        std::move(definition));
}

const TilesetDefinition* TilesetCatalog::find(const simulation::DefinitionId& id) const noexcept {
    for (const auto& definition : definitions_) {
        if (definition.id == id) { return &definition; }
    }
    return nullptr;
}

const TilesetDefinition& TilesetCatalog::require(const simulation::DefinitionId& id) const {
    const auto* result = find(id);
    if (!result) { throw std::out_of_range(std::string("unknown tileset definition: ") + std::string(id.value())); }
    return *result;
}

RuntimeTilesetCatalog::RuntimeTilesetCatalog(const TilesetCatalog& definitions)
    : definitions_(&definitions) {
    world::TilesetId next = 1;
    for (const auto& definition : definitions.definitions()) {
        mapping_.emplace(definition.id, next);
        reverse_.emplace(next, &definition);
        ++next;
    }
}

world::TilesetId RuntimeTilesetCatalog::requireRuntimeId(
    const simulation::DefinitionId& id) const {
    const auto found = mapping_.find(id);
    if (found == mapping_.end()) { throw std::out_of_range("unknown runtime tileset definition"); }
    return found->second;
}

const TilesetDefinition* RuntimeTilesetCatalog::definition(world::TilesetId id) const noexcept {
    const auto found = reverse_.find(id);
    return found == reverse_.end() ? nullptr : found->second;
}

bool TilesetVisualCatalog::load(const TilesetDefinition& definition, world::TilesetId runtimeId,
                                assets::AssetManager& assets, platform::ImageDecoder& decoder,
                                const std::filesystem::path& assetRoot, std::string& error) {
    try {
        const auto image = assets.loadImage(std::string("tileset.") + std::string(definition.id.value()),
                                            assetRoot / definition.relativeAssetPath, decoder);
        add(runtimeId, image, definition);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

void TilesetVisualCatalog::add(world::TilesetId runtimeId,
                               std::shared_ptr<const render::Image> image,
                               const TilesetDefinition& definition) {
    if (!image) { throw std::invalid_argument("tileset image is null"); }
    if (image->width() != static_cast<int>(definition.columns) * definition.tileSize ||
        image->height() != static_cast<int>(definition.rows) * definition.tileSize) {
        throw std::invalid_argument("tileset image dimensions do not match registered metadata");
    }
    visuals_[runtimeId] = std::make_unique<LoadedTilesetVisual>(
        std::move(image), world::TileAtlasLayout(
            static_cast<int>(definition.columns) * definition.tileSize,
            static_cast<int>(definition.rows) * definition.tileSize, definition.tileSize));
}

const LoadedTilesetVisual* TilesetVisualCatalog::find(world::TilesetId runtimeId) const noexcept {
    const auto found = visuals_.find(runtimeId);
    return found == visuals_.end() ? nullptr : found->second.get();
}

} // namespace underworld::game
