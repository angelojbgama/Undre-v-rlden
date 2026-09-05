#include "editor/editor_document.h"

#include "game/maps/dmap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace underworld::editor {
namespace {

bool pointOutside(const maps::MapData& data, core::WorldPointI point) noexcept {
    const std::int64_t width = static_cast<std::int64_t>(data.width) * data.tileSize;
    const std::int64_t height = static_cast<std::int64_t>(data.height) * data.tileSize;
    return point.x < 0 || point.y < 0 || point.x >= width || point.y >= height;
}

const game::AuthoringDescriptor* findDescriptor(
    const game::GameContentRegistry& content, const simulation::DefinitionId& id) noexcept {
    for (const auto& descriptor : content.authoringDescriptors()) {
        if (descriptor.definitionId == id) { return &descriptor; }
    }
    return nullptr;
}

void addPlacementWarnings(const maps::MapData& data, ValidationReport& report) {
    const auto add = [&](simulation::PersistentInstanceId id, core::WorldPointI position) {
        if (pointOutside(data, position)) {
            report.issues.push_back({ValidationSeverity::warning,
                "entity is outside map bounds", id, position});
        }
    };
    for (const auto& value : data.enemies) { add(value.id, value.position); }
    for (const auto& value : data.objects) { add(value.id, value.position); }
    for (const auto& value : data.pickups) { add(value.id, value.position); }
}

} // namespace

PropertyId::PropertyId(std::string value) : value_(std::move(value)) {
    if (value_.empty()) { throw std::invalid_argument("property id cannot be empty"); }
}

std::size_t PropertyIdHash::operator()(const PropertyId& id) const noexcept {
    return std::hash<std::string>{}(id.value());
}

bool ValidationReport::hasErrors() const noexcept { return errorCount() != 0; }
std::size_t ValidationReport::errorCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(),
        [](const auto& issue) { return issue.severity == ValidationSeverity::error; }));
}
std::size_t ValidationReport::warningCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(),
        [](const auto& issue) { return issue.severity == ValidationSeverity::warning; }));
}

bool CommandHistory::execute(std::unique_ptr<EditorCommand> command,
                             EditorDocument& document, std::string& error) {
    if (!command) { error = "editor command is null"; return false; }
    if (!command->apply(document, error)) { return false; }
    commands_.erase(commands_.begin() + static_cast<std::ptrdiff_t>(cursor_), commands_.end());
    commands_.push_back(std::move(command));
    cursor_ = commands_.size();
    return true;
}

bool CommandHistory::undo(EditorDocument& document) {
    if (!canUndo()) { return false; }
    commands_[cursor_ - 1]->revert(document);
    --cursor_;
    return true;
}

bool CommandHistory::redo(EditorDocument& document, std::string& error) {
    if (!canRedo()) { return false; }
    if (!commands_[cursor_]->apply(document, error)) { return false; }
    ++cursor_;
    return true;
}

void CommandHistory::clear() noexcept { commands_.clear(); cursor_ = 0; }

EditorDocument::EditorDocument() = default;
EditorDocument::EditorDocument(maps::MapData data) : data_(std::move(data)) {
    synchronizeEditorState();
    initializeAllocator();
}

EditorDocument EditorDocument::newMap(simulation::MapId id, std::uint32_t width,
                                      std::uint32_t height, std::uint16_t tileSize,
                                      bool includePlayerSpawn) {
    if (width == 0 || height == 0 || tileSize == 0 ||
        width > maps::MapLimits::maximumDimension ||
        height > maps::MapLimits::maximumDimension) {
        throw std::invalid_argument("new map dimensions are invalid");
    }
    const std::size_t cells = static_cast<std::size_t>(width) * height;
    maps::MapData data;
    data.id = std::move(id);
    data.width = width;
    data.height = height;
    data.tileSize = tileSize;
    data.layers.push_back({"Ground", true, std::vector<std::optional<std::uint32_t>>(cells)});
    data.collision.assign(cells, 0);
    if (includePlayerSpawn) {
        data.playerSpawns.push_back({simulation::SpawnId{"start"},
            {static_cast<int>(tileSize / 2), static_cast<int>(tileSize / 2)},
            game::gameplay::FacingDirection::down});
    }
    EditorDocument document(std::move(data));
    document.dirty_ = true;
    return document;
}

std::optional<EditorDocument> EditorDocument::open(
    const std::filesystem::path& path, const game::GameContentRegistry& content,
    std::string& error) {
    const auto catalogs = game::mapValidationCatalogs(content);
    auto loaded = maps::readDmap(path, &catalogs);
    if (!loaded) { error = std::move(loaded.error); return std::nullopt; }
    EditorDocument document(std::move(loaded.data));
    document.filePath_ = path;
    document.dirty_ = false;
    return document;
}

bool EditorDocument::save(const game::GameContentRegistry& content, std::string& error) {
    if (!filePath_) { error = "document has no file path; use Save As"; return false; }
    return saveAs(*filePath_, content, error);
}

bool EditorDocument::saveAs(const std::filesystem::path& path,
                            const game::GameContentRegistry& content,
                            std::string& error) {
    const ValidationReport report = validate(content);
    if (report.hasErrors()) { error = "document validation has blocking errors"; return false; }
    if (hasExperimentalData()) {
        error = "regions and typed placement overrides are experimental and cannot be persisted in DMAP 1.0";
        return false;
    }
    if (!maps::writeDmap(path, data_, error)) { return false; }
    filePath_ = path;
    dirty_ = false;
    return true;
}

bool EditorDocument::execute(std::unique_ptr<EditorCommand> command, std::string& error) {
    if (!history_.execute(std::move(command), *this, error)) { return false; }
    dirty_ = true;
    markMutated();
    return true;
}

bool EditorDocument::undo() {
    if (!history_.undo(*this)) { return false; }
    dirty_ = true;
    markMutated();
    return true;
}

bool EditorDocument::redo(std::string& error) {
    if (!history_.redo(*this, error)) { return false; }
    dirty_ = true;
    markMutated();
    return true;
}

simulation::PersistentInstanceId EditorDocument::allocatePersistentId() noexcept {
    if (nextPersistentId_ == 0) { return {}; }
    return {nextPersistentId_++};
}

bool EditorDocument::hasExperimentalData() const noexcept {
    return !regions_.empty() || !propertyOverrides_.empty();
}

ValidationReport EditorDocument::validate(const game::GameContentRegistry& content) const {
    ValidationReport report;
    const auto catalogs = game::mapValidationCatalogs(content);
    const auto base = maps::validateMapData(data_, &catalogs);
    if (!base) { report.issues.push_back({ValidationSeverity::error, base.error, {}, {}}); }

    for (const auto& pickup : data_.pickups) {
        if (!content.pickup(pickup.definitionId)) {
            report.issues.push_back({ValidationSeverity::error,
                "pickup placement references an unknown definition", pickup.id, pickup.position});
        }
    }

    std::unordered_set<std::string> regionIds;
    std::unordered_set<std::uint64_t> allIds;
    for (const auto& value : data_.enemies) { allIds.insert(value.id.value); }
    for (const auto& value : data_.objects) { allIds.insert(value.id.value); }
    for (const auto& value : data_.pickups) { allIds.insert(value.id.value); }
    for (const auto& region : regions_) {
        if (!region.id || !allIds.insert(region.id.value).second) {
            report.issues.push_back({ValidationSeverity::error,
                "region persistent instance id is zero or duplicate", region.id, {}});
        }
        if (region.regionId.empty() || !regionIds.insert(region.regionId).second) {
            report.issues.push_back({ValidationSeverity::error,
                "region id is empty or duplicate", region.id, {}});
        }
        if (region.bounds.width <= 0 || region.bounds.height <= 0) {
            report.issues.push_back({ValidationSeverity::error,
                "region AABB must have positive dimensions", region.id,
                core::WorldPointI{region.bounds.x, region.bounds.y}});
        }
    }

    for (const auto& [instance, overrides] : propertyOverrides_) {
        simulation::DefinitionId definition;
        for (const auto& value : data_.enemies) {
            if (value.id.value == instance) { definition = value.definitionId; break; }
        }
        if (definition.empty()) {
            report.issues.push_back({ValidationSeverity::error,
                "property override source does not exist", simulation::PersistentInstanceId{instance}, {}});
            continue;
        }
        const auto schemas = propertySchemasFor(content, definition);
        for (const auto& [propertyId, value] : overrides) {
            const auto schema = std::find_if(schemas.begin(), schemas.end(), [&](const auto& candidate) {
                return candidate.id == propertyId;
            });
            std::string error;
            if (schema == schemas.end() || !validatePropertyValue(*schema, value, content, *this, error)) {
                report.issues.push_back({ValidationSeverity::error,
                    schema == schemas.end() ? "unknown placement property" : error,
                    simulation::PersistentInstanceId{instance}, {}});
            }
        }
    }

    addPlacementWarnings(data_, report);
    for (const auto& spawn : data_.playerSpawns) {
        if (pointOutside(data_, spawn.position)) {
            report.issues.push_back({ValidationSeverity::warning,
                "player spawn is outside map bounds", {}, spawn.position});
        } else {
            const auto tx = static_cast<std::uint32_t>(spawn.position.x / data_.tileSize);
            const auto ty = static_cast<std::uint32_t>(spawn.position.y / data_.tileSize);
            const auto index = static_cast<std::size_t>(ty) * data_.width + tx;
            if (index < data_.collision.size() && data_.collision[index] != 0) {
                report.issues.push_back({ValidationSeverity::warning,
                    "player spawn is inside collision", {}, spawn.position});
            }
        }
    }
    if (hasExperimentalData()) {
        report.issues.push_back({ValidationSeverity::warning,
            "experimental regions/property overrides are not persisted by DMAP 1.0; save is blocked",
            {}, {}});
    }
    return report;
}

void EditorDocument::synchronizeEditorState() {
    layerStates_.clear();
    layerStates_.reserve(data_.layers.size());
    for (const auto& layer : data_.layers) { layerStates_.push_back({layer.visible, false}); }
    if (activeLayer_ >= data_.layers.size()) { activeLayer_ = 0; }
}

void EditorDocument::initializeAllocator() noexcept {
    std::uint64_t maximum{};
    const auto see = [&](simulation::PersistentInstanceId id) { maximum = std::max(maximum, id.value); };
    for (const auto& value : data_.enemies) { see(value.id); }
    for (const auto& value : data_.objects) { see(value.id); }
    for (const auto& value : data_.pickups) { see(value.id); }
    for (const auto& value : regions_) { see(value.id); }
    nextPersistentId_ = maximum == std::numeric_limits<std::uint64_t>::max() ? 0 : maximum + 1;
}

void EditorValidationCache::refreshIfNeeded(
    const EditorDocument& document, const game::GameContentRegistry& content) {
    if (initialized_ && validatedRevision_ == document.revision()) { return; }
    structural_ = document.validate(content);
    game::authoring::MapSemanticValidator validator;
    semantic_ = validator.validate(document.data(), content.authoringSemantics());
    validatedRevision_ = document.revision();
    initialized_ = true;
    ++recomputeCount_;
}

VisibleTileRange visibleTileRange(const maps::MapData& data, core::RectI viewport,
                                  double worldX, double worldY, double zoom,
                                  int margin) noexcept {
    if (data.width == 0 || data.height == 0 || data.tileSize == 0 ||
        viewport.width <= 0 || viewport.height <= 0 || zoom <= 0.0) {
        return {};
    }
    const int safeMargin = std::max(0, margin);
    const double worldRight = worldX + static_cast<double>(viewport.width) / zoom;
    const double worldBottom = worldY + static_cast<double>(viewport.height) / zoom;
    const auto firstX = static_cast<int>(std::floor(worldX / data.tileSize)) - safeMargin;
    const auto firstY = static_cast<int>(std::floor(worldY / data.tileSize)) - safeMargin;
    const auto lastX = static_cast<int>(std::ceil(worldRight / data.tileSize)) - 1 + safeMargin;
    const auto lastY = static_cast<int>(std::ceil(worldBottom / data.tileSize)) - 1 + safeMargin;
    return {
        std::clamp(firstX, 0, static_cast<int>(data.width) - 1),
        std::clamp(lastX, 0, static_cast<int>(data.width) - 1),
        std::clamp(firstY, 0, static_cast<int>(data.height) - 1),
        std::clamp(lastY, 0, static_cast<int>(data.height) - 1)};
}

std::vector<PropertySchema> propertySchemasFor(
    const game::GameContentRegistry& content, const simulation::DefinitionId& definitionId) {
    const auto* enemy = content.enemies().find(definitionId);
    if (!enemy) { return {}; }
    const auto& profile = content.behaviors().require(enemy->behaviorProfileId);
    return {
        {PropertyId{"enemy.detection_range"}, "Detection Range", PropertyType::integer,
         0, 4096, {}, {}, std::int64_t{profile.detectionRangePixels}},
        {PropertyId{"enemy.disengage_range"}, "Disengage Range", PropertyType::integer,
         0, 4096, {}, {}, std::int64_t{profile.disengageRangePixels}},
    };
}

bool validatePropertyValue(const PropertySchema& schema, const PropertyValue& value,
                           const game::GameContentRegistry& content,
                           const EditorDocument& document, std::string& error) {
    const auto wrong = [&] { error = "property value type does not match schema"; return false; };
    switch (schema.type) {
    case PropertyType::boolean:
        if (!std::holds_alternative<bool>(value)) { return wrong(); }
        break;
    case PropertyType::integer: {
        const auto* integer = std::get_if<std::int64_t>(&value);
        if (!integer) { return wrong(); }
        if (schema.minimum && *integer < *schema.minimum) { error = "integer property is below minimum"; return false; }
        if (schema.maximum && *integer > *schema.maximum) { error = "integer property is above maximum"; return false; }
        break;
    }
    case PropertyType::enumeration: {
        const auto* enumeration = std::get_if<EnumPropertyValue>(&value);
        if (!enumeration) { return wrong(); }
        if (std::find(schema.enumValues.begin(), schema.enumValues.end(), enumeration->value) ==
            schema.enumValues.end()) { error = "enum property value is not allowed"; return false; }
        break;
    }
    case PropertyType::definitionReference: {
        const auto* reference = std::get_if<DefinitionReference>(&value);
        if (!reference || !schema.definitionCategory || reference->category != *schema.definitionCategory) {
            return wrong();
        }
        const auto* descriptor = findDescriptor(content, reference->id);
        if (!descriptor || descriptor->category != reference->category) {
            error = "definition reference has the wrong or unknown category"; return false;
        }
        break;
    }
    case PropertyType::instanceReference: {
        const auto* reference = std::get_if<InstanceReference>(&value);
        if (!reference || !reference->id) { return wrong(); }
        const auto exists = [&](auto&& values) {
            return std::any_of(values.begin(), values.end(), [&](const auto& placement) {
                return placement.id == reference->id;
            });
        };
        if (!exists(document.data().enemies) && !exists(document.data().objects) &&
            !exists(document.data().pickups) &&
            std::none_of(document.regions().begin(), document.regions().end(), [&](const auto& region) {
                return region.id == reference->id;
            })) { error = "instance reference target does not exist in this map"; return false; }
        break;
    }
    }
    return true;
}

} // namespace underworld::editor
