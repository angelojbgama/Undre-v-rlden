#pragma once

#include "editor/editor_document.h"
#include "game/authoring/authoring_semantics.h"

#include <memory>
#include <optional>
#include <string_view>
#include <variant>

namespace underworld::editor {

struct TileCoordinate final { std::uint32_t x{}; std::uint32_t y{}; };

// Editor-only palette selection.  The authored map still stores ordinary
// MapTileReferences; this pattern is expanded into one compound command when
// it is painted.
struct TileBrushSelection final {
    simulation::DefinitionId tilesetId{};
    std::uint32_t width{1};
    std::uint32_t height{1};
    std::vector<maps::MapTileReference> cells;
    [[nodiscard]] bool valid() const noexcept {
        return width != 0 && height != 0 &&
               cells.size() == static_cast<std::size_t>(width) * height;
    }
};

[[nodiscard]] std::vector<std::pair<TileCoordinate, maps::MapTileReference>>
brushPlacements(const TileBrushSelection& brush, TileCoordinate origin,
               const maps::MapData& data);

class PaintTilesCommand final : public EditorCommand {
public:
    PaintTilesCommand(std::size_t layer, std::vector<TileCoordinate> cells,
                      std::optional<maps::MapTileReference> value);
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return "Paint Tiles"; }
private:
    struct Previous final { std::size_t index{}; std::optional<std::uint32_t> value; };
    std::size_t layer_{};
    std::vector<TileCoordinate> cells_;
    std::optional<maps::MapTileReference> desired_;
    std::vector<Previous> previous_;
    std::optional<std::uint32_t> referenceIndex_;
    bool ownsReference_{};
};

using EraseTilesCommand = PaintTilesCommand;

class AddLayerCommand final : public EditorCommand {
public:
    AddLayerCommand(std::size_t index, std::string name);
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return "Add Layer"; }
private:
    std::size_t index_{};
    std::string name_;
    bool applied_{};
};

class RemoveLayerCommand final : public EditorCommand {
public:
    explicit RemoveLayerCommand(std::size_t index) : index_(index) {}
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return "Remove Layer"; }
private:
    std::size_t index_{};
    std::optional<maps::MapTileLayer> removed_;
    std::optional<EditorLayerState> removedState_;
    std::size_t activeBefore_{};
};

class RenameLayerCommand final : public EditorCommand {
public:
    RenameLayerCommand(std::size_t index, std::string name)
        : index_(index), name_(std::move(name)) {}
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return "Rename Layer"; }
private:
    std::size_t index_{};
    std::string name_;
    std::string previous_;
};

class MoveLayerCommand final : public EditorCommand {
public:
    MoveLayerCommand(std::size_t from, std::size_t to) : from_(from), to_(to) {}
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return "Move Layer"; }
private:
    std::size_t from_{};
    std::size_t to_{};
    std::size_t activeBefore_{};
};

class SetCollisionCommand final : public EditorCommand {
public:
    SetCollisionCommand(std::vector<TileCoordinate> cells, bool solid);
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return "Set Collision"; }
private:
    struct Previous final { std::size_t index{}; std::uint8_t value{}; };
    std::vector<TileCoordinate> cells_;
    bool solid_{};
    std::vector<Previous> previous_;
};

using AuthoredPlacement = std::variant<maps::EnemyPlacement, maps::NpcPlacement,
    maps::ObjectPlacement, maps::PickupPlacement, maps::PlayerSpawn, maps::MapLink,
    RegionPlacement>;

class PlaceEntityCommand final : public EditorCommand {
public:
    explicit PlaceEntityCommand(AuthoredPlacement placement,
                                std::optional<PropertyOverrideSet> overrides = std::nullopt)
        : placement_(std::move(placement)), overrides_(std::move(overrides)) {}
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return "Place Entity"; }
private:
    AuthoredPlacement placement_;
    std::optional<PropertyOverrideSet> overrides_;
};

class MoveEntityCommand final : public EditorCommand {
public:
    MoveEntityCommand(SelectionKind kind, simulation::PersistentInstanceId id,
                      core::WorldPointI before, core::WorldPointI after,
                      std::string authoredId = {});
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return "Move Entity"; }
private:
    bool set(EditorDocument& document, core::WorldPointI value) noexcept;
    SelectionKind kind_{};
    simulation::PersistentInstanceId id_{};
    std::string authoredId_;
    core::WorldPointI before_{};
    core::WorldPointI after_{};
};

class DeleteEntityCommand final : public EditorCommand {
public:
    DeleteEntityCommand(SelectionKind kind, simulation::PersistentInstanceId id,
                        std::string authoredId = {});
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return "Delete Entity"; }
private:
    SelectionKind kind_{};
    simulation::PersistentInstanceId id_{};
    std::string authoredId_;
    std::optional<AuthoredPlacement> removed_;
    std::optional<PropertyOverrideSet> removedOverrides_;
    std::size_t index_{};
};

class SetPropertyCommand final : public EditorCommand {
public:
    SetPropertyCommand(simulation::PersistentInstanceId source, PropertySchema schema,
                       std::optional<PropertyValue> value,
                       const game::GameContentRegistry& content);
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return "Set Property"; }
private:
    simulation::PersistentInstanceId source_{};
    PropertySchema schema_;
    std::optional<PropertyValue> value_;
    std::optional<PropertyValue> previous_;
    const game::GameContentRegistry* content_{};
};

class ResizeRegionCommand final : public EditorCommand {
public:
    ResizeRegionCommand(simulation::PersistentInstanceId id, world::AabbI before,
                        world::AabbI after) : id_(id), before_(before), after_(after) {}
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return "Resize Region"; }
private:
    bool set(EditorDocument& document, world::AabbI bounds) noexcept;
    simulation::PersistentInstanceId id_{};
    world::AabbI before_{};
    world::AabbI after_{};
};

class CompoundEditorCommand final : public EditorCommand {
public:
    explicit CompoundEditorCommand(std::string label = "Compound Edit") : label_(std::move(label)) {}
    void add(std::unique_ptr<EditorCommand> command);
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return label_.c_str(); }
private:
    std::string label_;
    std::vector<std::unique_ptr<EditorCommand>> commands_;
};

class PlaceStampCommand final : public EditorCommand {
public:
    PlaceStampCommand(std::size_t layer, const game::authoring::StampDefinition& stamp,
                      TileCoordinate origin, const game::authoring::AuthoringSemanticRegistry& semantics);
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return "Place Stamp"; }
private:
    CompoundEditorCommand command_{"Place Stamp"};
    std::vector<TileCoordinate> destinations_;
    std::size_t layer_{};
    bool valid_{};
};

[[nodiscard]] std::vector<TileCoordinate> rectangleCells(int x0, int y0, int x1, int y1,
                                                         const maps::MapData& data);
[[nodiscard]] std::vector<TileCoordinate> tileFloodCells(const maps::MapData& data,
    std::size_t layer, std::uint32_t startX, std::uint32_t startY);
[[nodiscard]] std::vector<TileCoordinate> collisionFloodCells(const maps::MapData& data,
    std::uint32_t startX, std::uint32_t startY);
[[nodiscard]] std::optional<AuthoredPlacement> duplicatePlacement(
    const EditorDocument& document, SelectionKind kind, simulation::PersistentInstanceId id,
    simulation::PersistentInstanceId newId, int offset);
[[nodiscard]] std::optional<AuthoredPlacement> duplicateAuthoredPlacement(
    const EditorDocument& document, SelectionKind kind, std::string_view authoredId, int offset);
[[nodiscard]] std::optional<TileCoordinate> worldPointToTile(
    const maps::MapData& data, core::WorldPointI point) noexcept;

} // namespace underworld::editor
