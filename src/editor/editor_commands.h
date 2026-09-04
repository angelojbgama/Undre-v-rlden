#pragma once

#include "editor/editor_document.h"

#include <memory>
#include <optional>
#include <variant>

namespace underworld::editor {

struct TileCoordinate final { std::uint32_t x{}; std::uint32_t y{}; };

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

using AuthoredPlacement = std::variant<maps::EnemyPlacement, maps::ObjectPlacement,
    maps::PickupPlacement, maps::PlayerSpawn, maps::MapLink, RegionPlacement>;

class PlaceEntityCommand final : public EditorCommand {
public:
    explicit PlaceEntityCommand(AuthoredPlacement placement) : placement_(std::move(placement)) {}
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return "Place Entity"; }
private:
    AuthoredPlacement placement_;
};

class MoveEntityCommand final : public EditorCommand {
public:
    MoveEntityCommand(SelectionKind kind, simulation::PersistentInstanceId id,
                      core::WorldPointI before, core::WorldPointI after);
    bool apply(EditorDocument& document, std::string& error) override;
    void revert(EditorDocument& document) noexcept override;
    [[nodiscard]] const char* label() const noexcept override { return "Move Entity"; }
private:
    bool set(EditorDocument& document, core::WorldPointI value) noexcept;
    SelectionKind kind_{};
    simulation::PersistentInstanceId id_{};
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

[[nodiscard]] std::vector<TileCoordinate> rectangleCells(int x0, int y0, int x1, int y1,
                                                         const maps::MapData& data);
[[nodiscard]] std::vector<TileCoordinate> tileFloodCells(const maps::MapData& data,
    std::size_t layer, std::uint32_t startX, std::uint32_t startY);
[[nodiscard]] std::vector<TileCoordinate> collisionFloodCells(const maps::MapData& data,
    std::uint32_t startX, std::uint32_t startY);
[[nodiscard]] std::optional<AuthoredPlacement> duplicatePlacement(
    const EditorDocument& document, SelectionKind kind, simulation::PersistentInstanceId id,
    simulation::PersistentInstanceId newId, int offset);

} // namespace underworld::editor
