#pragma once

#include "engine/core/coordinates.h"
#include "engine/core/geometry.h"
#include "engine/simulation/persistent_id.h"
#include "engine/world/collision.h"
#include "game/game_content.h"
#include "game/maps/map_data.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace underworld::editor {

namespace maps = underworld::game::maps;

enum class SelectionKind { none, enemy, object, pickup, npc, playerSpawn, mapLink, region };

struct EditorSelection final {
    SelectionKind kind{SelectionKind::none};
    simulation::PersistentInstanceId instanceId{};
    std::string authoredId;
    void clear() noexcept { *this = {}; }
};

enum class EditorTool {
    select, tilePencil, tileErase, tileRectangle, tileFill, tileEyedropper, collisionPaint,
    collisionErase, collisionRectangle, collisionRectangleErase, collisionFill,
    collisionFillErase, entityPlace, regionCreate, stampPlace
};

struct EditorViewportState final {
    double worldX{};
    double worldY{};
    std::size_t zoomStep{2};
    bool showGrid{true};
};

struct EditorLayerState final { bool visible{true}; bool locked{}; };

class PropertyId final {
public:
    PropertyId() = default;
    explicit PropertyId(std::string value);
    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }
    [[nodiscard]] bool operator==(const PropertyId&) const noexcept = default;
private:
    std::string value_;
};

struct PropertyIdHash final {
    [[nodiscard]] std::size_t operator()(const PropertyId& id) const noexcept;
};

enum class PropertyType { boolean, integer, enumeration, definitionReference, instanceReference };
struct EnumPropertyValue final { std::string value; [[nodiscard]] bool operator==(const EnumPropertyValue&) const noexcept = default; };
struct DefinitionReference final {
    simulation::DefinitionId id{};
    game::AuthoringCategory category{game::AuthoringCategory::enemy};
    [[nodiscard]] bool operator==(const DefinitionReference&) const noexcept = default;
};
struct InstanceReference final {
    simulation::PersistentInstanceId id{};
    [[nodiscard]] bool operator==(const InstanceReference&) const noexcept = default;
};
using PropertyValue = std::variant<bool, std::int64_t, EnumPropertyValue,
                                   DefinitionReference, InstanceReference>;

struct PropertySchema final {
    PropertyId id;
    std::string displayName;
    PropertyType type{PropertyType::integer};
    std::optional<std::int64_t> minimum;
    std::optional<std::int64_t> maximum;
    std::vector<std::string> enumValues;
    std::optional<game::AuthoringCategory> definitionCategory;
    PropertyValue defaultValue{std::int64_t{0}};
};

using PropertyOverrideSet = std::unordered_map<PropertyId, PropertyValue, PropertyIdHash>;

struct RegionPlacement final {
    simulation::PersistentInstanceId id{};
    std::string regionId;
    world::AabbI bounds{};
};

enum class ValidationSeverity { error, warning };
struct ValidationIssue final {
    ValidationSeverity severity{ValidationSeverity::error};
    std::string message;
    std::optional<simulation::PersistentInstanceId> source;
    std::optional<core::WorldPointI> location;
};
struct ValidationReport final {
    std::vector<ValidationIssue> issues;
    [[nodiscard]] bool hasErrors() const noexcept;
    [[nodiscard]] std::size_t errorCount() const noexcept;
    [[nodiscard]] std::size_t warningCount() const noexcept;
};

class EditorDocument;

class EditorCommand {
public:
    virtual ~EditorCommand() = default;
    virtual bool apply(EditorDocument& document, std::string& error) = 0;
    virtual void revert(EditorDocument& document) noexcept = 0;
    [[nodiscard]] virtual const char* label() const noexcept = 0;
};

class CommandHistory final {
public:
    bool execute(std::unique_ptr<EditorCommand> command, EditorDocument& document,
                 std::string& error);
    bool undo(EditorDocument& document);
    bool redo(EditorDocument& document, std::string& error);
    void clear() noexcept;
    [[nodiscard]] bool canUndo() const noexcept { return cursor_ > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return cursor_ < commands_.size(); }
    [[nodiscard]] std::size_t size() const noexcept { return commands_.size(); }
private:
    std::vector<std::unique_ptr<EditorCommand>> commands_;
    std::size_t cursor_{};
};

class EditorDocument final {
public:
    EditorDocument();
    explicit EditorDocument(maps::MapData data);

    static EditorDocument newMap(simulation::MapId id, std::uint32_t width,
                                 std::uint32_t height, std::uint16_t tileSize = 16,
                                 bool includePlayerSpawn = false);
    // Creates a content-aware authored canvas.  The generic newMap() remains empty
    // for tests and low-level document fixtures; the editor uses this entry point so
    // a newly created map is immediately paintable with the registered dataset.
    static EditorDocument newAuthoredMap(simulation::MapId id, std::uint32_t width,
                                         std::uint32_t height, std::uint16_t tileSize,
                                         const game::GameContentRegistry& content,
                                         bool includePlayerSpawn = true);
    static std::optional<EditorDocument> open(const std::filesystem::path& path,
                                               const game::GameContentRegistry& content,
                                               std::string& error);
    bool save(const game::GameContentRegistry& content, std::string& error);
    bool saveAs(const std::filesystem::path& path,
                const game::GameContentRegistry& content, std::string& error);
    // Writes a validated DMAP sidecar without changing the document path or
    // dirty state. The sidecar must not be the authored document path.
    bool saveBackup(const std::filesystem::path& path,
                    const game::GameContentRegistry& content, std::string& error) const;
    [[nodiscard]] std::optional<std::filesystem::path> autosavePath() const;

    bool execute(std::unique_ptr<EditorCommand> command, std::string& error);
    bool undo();
    bool redo(std::string& error);
    void markSaved() noexcept { dirty_ = false; }

    [[nodiscard]] const maps::MapData& data() const noexcept { return data_; }
    [[nodiscard]] maps::MapData& commandData() noexcept { return data_; }
    [[nodiscard]] const std::optional<std::filesystem::path>& filePath() const noexcept {
        return filePath_;
    }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] CommandHistory& history() noexcept { return history_; }
    [[nodiscard]] const CommandHistory& history() const noexcept { return history_; }
    [[nodiscard]] EditorSelection& selection() noexcept { return selection_; }
    [[nodiscard]] const EditorSelection& selection() const noexcept { return selection_; }
    [[nodiscard]] EditorViewportState& viewport() noexcept { return viewport_; }
    [[nodiscard]] const EditorViewportState& viewport() const noexcept { return viewport_; }
    [[nodiscard]] EditorTool& activeTool() noexcept { return activeTool_; }
    [[nodiscard]] std::size_t& activeLayer() noexcept { return activeLayer_; }    [[nodiscard]] const std::vector<EditorLayerState>& layerStates() const noexcept {
        return layerStates_;
    }
    [[nodiscard]] std::vector<EditorLayerState>& layerStates() noexcept { return layerStates_; }
    [[nodiscard]] simulation::PersistentInstanceId allocatePersistentId() noexcept;
    [[nodiscard]] const std::vector<RegionPlacement>& regions() const noexcept { return regions_; }
    [[nodiscard]] std::vector<RegionPlacement>& commandRegions() noexcept { return regions_; }
    [[nodiscard]] const std::unordered_map<std::uint64_t, PropertyOverrideSet>&
        propertyOverrides() const noexcept { return propertyOverrides_; }
    [[nodiscard]] std::unordered_map<std::uint64_t, PropertyOverrideSet>&
        commandPropertyOverrides() noexcept { return propertyOverrides_; }
    [[nodiscard]] bool hasExperimentalData() const noexcept;
    [[nodiscard]] ValidationReport validate(const game::GameContentRegistry& content) const;

private:
    void markMutated() noexcept {
        if (revision_ != std::numeric_limits<std::uint64_t>::max()) { ++revision_; }
    }
    void synchronizeEditorState();
    void initializeAllocator() noexcept;

    maps::MapData data_;
    EditorSelection selection_;
    EditorViewportState viewport_;
    EditorTool activeTool_{EditorTool::select};
    std::size_t activeLayer_{};
    std::vector<EditorLayerState> layerStates_;
    CommandHistory history_;
    bool dirty_{};
    std::uint64_t revision_{1};
    std::optional<std::filesystem::path> filePath_;
    std::uint64_t nextPersistentId_{1};
    std::vector<RegionPlacement> regions_;
    std::unordered_map<std::uint64_t, PropertyOverrideSet> propertyOverrides_;
};

class EditorValidationCache final {
public:
    void invalidate() noexcept { initialized_ = false; }
    void refreshIfNeeded(const EditorDocument& document,
                         const game::GameContentRegistry& content);
    [[nodiscard]] const ValidationReport& structural() const noexcept { return structural_; }
    [[nodiscard]] const game::authoring::SemanticValidationReport& semantic() const noexcept {
        return semantic_;
    }
    [[nodiscard]] std::uint64_t validatedRevision() const noexcept { return validatedRevision_; }
    [[nodiscard]] std::size_t recomputeCount() const noexcept { return recomputeCount_; }

private:
    bool initialized_{};
    std::uint64_t validatedRevision_{};
    std::size_t recomputeCount_{};
    ValidationReport structural_;
    game::authoring::SemanticValidationReport semantic_;
};

struct VisibleTileRange final {
    int firstX{};
    int lastX{-1};
    int firstY{};
    int lastY{-1};
    [[nodiscard]] bool empty() const noexcept { return firstX > lastX || firstY > lastY; }
    [[nodiscard]] std::size_t tileCount() const noexcept {
        return empty() ? 0U : static_cast<std::size_t>(lastX - firstX + 1) *
            static_cast<std::size_t>(lastY - firstY + 1);
    }
};

[[nodiscard]] VisibleTileRange visibleTileRange(const maps::MapData& data,
    core::RectI viewport, double worldX, double worldY, double zoom,
    int margin = 1) noexcept;

[[nodiscard]] std::vector<PropertySchema> propertySchemasFor(
    const game::GameContentRegistry& content, const simulation::DefinitionId& definitionId);
[[nodiscard]] bool validatePropertyValue(const PropertySchema& schema,
                                         const PropertyValue& value,
                                         const game::GameContentRegistry& content,
                                         const EditorDocument& document,
                                         std::string& error);

} // namespace underworld::editor
