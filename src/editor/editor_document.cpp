#include "editor/editor_document.h"

#include "game/maps/authored_map.h"
#include "game/maps/dmap.h"
#include "game/maps/map_composition.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

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
    for (const auto& value : data.npcs) { add(value.id, value.position); }
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
    authoredSource_ = maps::authoredMapFromMapData(data_);
    synchronizeEditorState();
    initializeAllocator();
    for (const auto& region : data_.regions) {
        regions_.push_back({allocatePersistentId(), std::string(region.id.value()), region.bounds,
                            region.environmentEffectId});
    }
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

EditorDocument EditorDocument::newAuthoredMap(
    simulation::MapId id, std::uint32_t width, std::uint32_t height,
    std::uint16_t tileSize, const game::GameContentRegistry& content,
    bool includePlayerSpawn) {
    maps::MapBlueprint blueprint;
    blueprint.id = id;
    blueprint.room.width = width;
    blueprint.room.height = height;
    blueprint.tileSize = tileSize;
    if (includePlayerSpawn) {
        blueprint.room.playerSpawn = maps::PlayerSpawnBlueprint{
            simulation::SpawnId{"entry.start"},
            {static_cast<int>(width / 2U), static_cast<int>(height / 2U)},
            game::gameplay::FacingDirection::down};
    }

    const auto composed = maps::MapComposer{}.compose(blueprint,
                                                        content.authoringSemantics());
    if (!composed) {
        throw std::invalid_argument("authored map composition failed");
    }
    maps::MapData data = *composed.map;
    const auto cells = static_cast<std::size_t>(width) * height;

    // Keep the runtime/editor layer contract explicit.  The compositor owns the
    // boundary and collision; the editor owns the immediately paintable canvas.
    data.layers.insert(data.layers.begin(),
                       {"ground", true, std::vector<std::optional<std::uint32_t>>(cells)});
    data.layers.push_back(
        {"decoration_low", true, std::vector<std::optional<std::uint32_t>>(cells)});
    data.layers.push_back(
        {"objects_visual", true, std::vector<std::optional<std::uint32_t>>(cells)});
    data.layers.push_back(
        {"decoration_high", true, std::vector<std::optional<std::uint32_t>>(cells)});

    // masonry.39 is a catalogued interior masonry surface already used by the
    // authored runtime content.  It is a visual starter only; collision remains
    // the explicit perimeter grid produced by MapComposer.
    const auto* surface = content.authoringSemantics().findTile(
        simulation::DefinitionId{"tile.dungeon.masonry.39"});
    if (!surface) {
        throw std::invalid_argument("authored map surface semantic is unavailable");
    }
    data.tileReferences.push_back(game::authoring::tileReferenceFor(*surface));
    const auto surfaceIndex = static_cast<std::uint32_t>(data.tileReferences.size() - 1U);
    auto& ground = data.layers.front().cells;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            ground[static_cast<std::size_t>(y) * width + x] = surfaceIndex;
        }
    }

    EditorDocument document(std::move(data));
    document.dirty_ = true;
    return document;
}

std::optional<EditorDocument> EditorDocument::open(
    const std::filesystem::path& path, const game::GameContentRegistry& content,
    std::string& error) {
    if (path.extension() == ".umap") {
        const auto loaded = maps::readAuthoredMapFile(path);
        if (!loaded.source) {
            error = loaded.diagnostics.empty() ? "could not decode authored map" :
                loaded.diagnostics.front().message;
            return std::nullopt;
        }
        EditorDocument document(maps::mapDataFromAuthored(*loaded.source));
        document.authoredSource_ = *loaded.source;
        for (const auto& authoredOverride : loaded.source->placementOverrides) {
            PropertyValue value;
            switch (authoredOverride.value.kind) {
            case maps::AuthoredPropertyValueKind::boolean:
                value = authoredOverride.value.booleanValue; break;
            case maps::AuthoredPropertyValueKind::integer:
                value = authoredOverride.value.integerValue; break;
            case maps::AuthoredPropertyValueKind::enumeration:
                value = EnumPropertyValue{authoredOverride.value.textValue}; break;
            case maps::AuthoredPropertyValueKind::definitionReference:
                value = DefinitionReference{authoredOverride.value.definitionValue}; break;
            case maps::AuthoredPropertyValueKind::instanceReference:
                value = InstanceReference{authoredOverride.value.instanceValue}; break;
            }
            document.propertyOverrides_[authoredOverride.instanceId.value]
                [PropertyId{authoredOverride.propertyId}] = std::move(value);
        }
        document.filePath_ = path;
        document.dirty_ = false;
        document.initializeAllocator();
        document.synchronizeAuthoredSource();
        return std::optional<EditorDocument>{std::move(document)};
    }
    const auto catalogs = game::mapValidationCatalogs(content);
    auto loaded = maps::readDmap(path, &catalogs);
    if (!loaded) { error = std::move(loaded.error); return std::nullopt; }
    EditorDocument document(std::move(loaded.data));
    document.filePath_ = path;
    document.dirty_ = false;
    return std::optional<EditorDocument>{std::move(document)};
}

EditorDocument EditorDocument::fromAuthoredSource(maps::AuthoredMapSource source) {
    EditorDocument document(maps::mapDataFromAuthored(source));
    document.authoredSource_ = std::move(source);
    for (const auto& authoredOverride : document.authoredSource_.placementOverrides) {
        PropertyValue value;
        switch (authoredOverride.value.kind) {
        case maps::AuthoredPropertyValueKind::boolean: value = authoredOverride.value.booleanValue; break;
        case maps::AuthoredPropertyValueKind::integer: value = authoredOverride.value.integerValue; break;
        case maps::AuthoredPropertyValueKind::enumeration: value = EnumPropertyValue{authoredOverride.value.textValue}; break;
        case maps::AuthoredPropertyValueKind::definitionReference: value = DefinitionReference{authoredOverride.value.definitionValue}; break;
        case maps::AuthoredPropertyValueKind::instanceReference: value = InstanceReference{authoredOverride.value.instanceValue}; break;
        }
        document.propertyOverrides_[authoredOverride.instanceId.value]
            [PropertyId{authoredOverride.propertyId}] = std::move(value);
    }
    document.dirty_ = false;
    document.initializeAllocator();
    document.synchronizeAuthoredSource();
    return document;
}

bool EditorDocument::save(const game::GameContentRegistry& content, std::string& error) {
    if (!filePath_) { error = "document has no file path; use Save As"; return false; }
    if (filePath_->extension() != ".umap") {
        error = "imported DMAP documents must be saved as .umap; use Save As";
        return false;
    }
    return saveAs(*filePath_, content, error);
}

bool EditorDocument::saveAs(const std::filesystem::path& path,
                            const game::GameContentRegistry& content,
                            std::string& error) {
    const ValidationReport report = validate(content);
    if (report.hasErrors()) { error = "document validation has blocking errors"; return false; }
    if (path.extension() != ".umap") {
        error = "authored Save As requires a .umap path; use Compile/Export DMAP for runtime output";
        return false;
    }
    synchronizeAuthoredSource();
    if (!maps::writeAuthoredMapFile(path, authoredSource_, error)) { return false; }
    filePath_ = path;
    dirty_ = false;
    return true;
}

bool EditorDocument::saveBackup(const std::filesystem::path& path,
                                const game::GameContentRegistry& content,
                                std::string& error) const {
    error.clear();
    if (path.empty()) { error = "backup path is empty"; return false; }
    if (filePath_) {
        std::error_code pathError;
        const auto authored = std::filesystem::absolute(*filePath_, pathError).lexically_normal();
        const auto backup = std::filesystem::absolute(path, pathError).lexically_normal();
        if (!pathError && authored == backup) {
            error = "backup path must not replace the authored document";
            return false;
        }
    }
    const ValidationReport report = validate(content);
    if (report.hasErrors()) { error = "document validation has blocking errors"; return false; }
    if (path.extension() == ".umap") {
        auto authored = maps::authoredMapFromMapData(data_);
        for (const auto& region : regions_) {
            const auto found = std::find_if(authored.regions.begin(), authored.regions.end(),
                [&](const auto& value) { return value.id.value() == region.regionId; });
            if (found == authored.regions.end()) {
                authored.regions.push_back({simulation::DefinitionId{region.regionId}, region.bounds,
                                            region.environmentEffectId});
            } else {
                found->bounds = region.bounds;
                found->environmentEffectId = region.environmentEffectId;
            }
        }
        for (const auto& [instance, overrides] : propertyOverrides_) {
            for (const auto& [property, value] : overrides) {
                maps::AuthoredPlacementOverride authoredOverride;
                authoredOverride.instanceId = {instance};
                authoredOverride.propertyId = property.value();
                if (const auto* boolean = std::get_if<bool>(&value)) {
                    authoredOverride.value.kind = maps::AuthoredPropertyValueKind::boolean;
                    authoredOverride.value.booleanValue = *boolean;
                } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
                    authoredOverride.value.kind = maps::AuthoredPropertyValueKind::integer;
                    authoredOverride.value.integerValue = *integer;
                } else if (const auto* enumeration = std::get_if<EnumPropertyValue>(&value)) {
                    authoredOverride.value.kind = maps::AuthoredPropertyValueKind::enumeration;
                    authoredOverride.value.textValue = enumeration->value;
                } else if (const auto* definition = std::get_if<DefinitionReference>(&value)) {
                    authoredOverride.value.kind = maps::AuthoredPropertyValueKind::definitionReference;
                    authoredOverride.value.definitionValue = definition->id;
                } else {
                    authoredOverride.value.kind = maps::AuthoredPropertyValueKind::instanceReference;
                    authoredOverride.value.instanceValue = std::get<InstanceReference>(value).id;
                }
                authored.placementOverrides.push_back(std::move(authoredOverride));
            }
        }
        return maps::writeAuthoredMapFile(path, authored, error);
    }
    if (hasExperimentalData()) {
        error = "authored-only regions and typed placement overrides cannot be backed up in DMAP 1.0";
        return false;
    }
    return maps::writeDmap(path, data_, error);
}

bool EditorDocument::exportDmap(const std::filesystem::path& path,
                                const game::GameContentRegistry& content,
                                std::string& error) const {
    const ValidationReport report = validate(content);
    if (report.hasErrors()) { error = "document validation has blocking errors"; return false; }
    auto authored = maps::authoredMapFromMapData(data_);
    for (const auto& region : regions_) {
        const auto found = std::find_if(authored.regions.begin(), authored.regions.end(),
            [&](const auto& value) { return value.id.value() == region.regionId; });
        if (found == authored.regions.end()) {
            authored.regions.push_back({simulation::DefinitionId{region.regionId}, region.bounds,
                                        region.environmentEffectId});
        } else {
            found->bounds = region.bounds;
            found->environmentEffectId = region.environmentEffectId;
        }
    }
    const auto compiled = maps::compileAuthoredMap(authored, content);
    if (!compiled.map) {
        error = compiled.diagnostics.empty() ? "authored map compilation failed" :
            compiled.diagnostics.front().message;
        return false;
    }
    return maps::writeDmap(path, *compiled.map, error);
}

std::optional<std::filesystem::path> EditorDocument::autosavePath() const {
    if (!filePath_) { return std::nullopt; }
    auto result = *filePath_;
    result.replace_filename(filePath_->stem().string() + ".autosave.umap");
    return result;
}

bool EditorDocument::execute(std::unique_ptr<EditorCommand> command, std::string& error) {
    if (!history_.execute(std::move(command), *this, error)) { return false; }
    synchronizeAuthoredSource();
    dirty_ = true;
    markMutated();
    return true;
}

bool EditorDocument::undo() {
    if (!history_.undo(*this)) { return false; }
    synchronizeAuthoredSource();
    dirty_ = true;
    markMutated();
    return true;
}

bool EditorDocument::redo(std::string& error) {
    if (!history_.redo(*this, error)) { return false; }
    synchronizeAuthoredSource();
    dirty_ = true;
    markMutated();
    return true;
}

simulation::PersistentInstanceId EditorDocument::allocatePersistentId() noexcept {
    if (nextPersistentId_ == 0) { return {}; }
    return {nextPersistentId_++};
}

bool EditorDocument::setRegionEnvironmentEffect(
    const simulation::DefinitionId& regionId,
    std::optional<simulation::DefinitionId> effectId, std::string& error) {
    error.clear();
    if (regionId.empty()) { error = "region id cannot be empty"; return false; }
    if (effectId && effectId->empty()) { error = "environment effect id cannot be empty"; return false; }
    const auto found = std::find_if(regions_.begin(), regions_.end(),
        [&](const auto& value) { return value.regionId == regionId.value(); });
    if (found == regions_.end()) { error = "region does not exist"; return false; }
    found->environmentEffectId = std::move(effectId);
    // Region commands expose the editor's placement view directly.  Synchronize
    // that view before rebuilding the authored source so a newly-created region
    // is not lost when this binding is changed before the next Save.
    synchronizeAuthoredSource();
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::hasExperimentalData() const noexcept {
    return !regions_.empty() || !propertyOverrides_.empty();
}

void EditorDocument::commitAuthoredMutation() noexcept {
    data_ = maps::mapDataFromAuthored(authoredSource_);
    synchronizeEditorState();
    initializeAllocator();
    // These operations are direct document edits rather than EditorCommand
    // instances.  Do not leave an older undo branch capable of restoring a
    // stale compiled view over the newly edited authored source.
    history_.clear();
    dirty_ = true;
    markMutated();
}

bool EditorDocument::addRule(maps::WorldRuleDefinition rule, std::string& error) {
    error.clear();
    if (rule.id.empty()) { error = "world rule id cannot be empty"; return false; }
    const auto found = std::find_if(authoredSource_.worldRules.begin(),
                                    authoredSource_.worldRules.end(),
        [&](const auto& value) { return value.id == rule.id; });
    if (found != authoredSource_.worldRules.end()) {
        error = "world rule id already exists";
        return false;
    }
    authoredSource_.worldRules.push_back(std::move(rule));
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::removeRule(const simulation::DefinitionId& ruleId, std::string& error) {
    error.clear();
    const auto found = std::find_if(authoredSource_.worldRules.begin(),
                                    authoredSource_.worldRules.end(),
        [&](const auto& value) { return value.id == ruleId; });
    if (found == authoredSource_.worldRules.end()) { error = "world rule does not exist"; return false; }
    authoredSource_.worldRules.erase(found);
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::setRuleTrigger(const simulation::DefinitionId& ruleId,
                                    maps::WorldTrigger trigger, std::string& error) {
    error.clear();
    const auto found = std::find_if(authoredSource_.worldRules.begin(),
                                    authoredSource_.worldRules.end(),
        [&](const auto& value) { return value.id == ruleId; });
    if (found == authoredSource_.worldRules.end()) { error = "world rule does not exist"; return false; }
    found->trigger = std::move(trigger);
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::addRuleCondition(const simulation::DefinitionId& ruleId,
                                      maps::WorldCondition condition, std::string& error) {
    error.clear();
    const auto found = std::find_if(authoredSource_.worldRules.begin(),
                                    authoredSource_.worldRules.end(),
        [&](const auto& value) { return value.id == ruleId; });
    if (found == authoredSource_.worldRules.end()) { error = "world rule does not exist"; return false; }
    found->conditions.push_back(std::move(condition));
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::removeRuleCondition(const simulation::DefinitionId& ruleId,
                                         std::size_t index, std::string& error) {
    error.clear();
    const auto found = std::find_if(authoredSource_.worldRules.begin(),
                                    authoredSource_.worldRules.end(),
        [&](const auto& value) { return value.id == ruleId; });
    if (found == authoredSource_.worldRules.end()) { error = "world rule does not exist"; return false; }
    if (index >= found->conditions.size()) { error = "world rule condition index is out of range"; return false; }
    found->conditions.erase(found->conditions.begin() + static_cast<std::ptrdiff_t>(index));
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::addRuleAction(const simulation::DefinitionId& ruleId,
                                   maps::WorldAction action, std::string& error) {
    error.clear();
    const auto found = std::find_if(authoredSource_.worldRules.begin(),
                                    authoredSource_.worldRules.end(),
        [&](const auto& value) { return value.id == ruleId; });
    if (found == authoredSource_.worldRules.end()) { error = "world rule does not exist"; return false; }
    found->actions.push_back(std::move(action));
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::removeRuleAction(const simulation::DefinitionId& ruleId,
                                      std::size_t index, std::string& error) {
    error.clear();
    const auto found = std::find_if(authoredSource_.worldRules.begin(),
                                    authoredSource_.worldRules.end(),
        [&](const auto& value) { return value.id == ruleId; });
    if (found == authoredSource_.worldRules.end()) { error = "world rule does not exist"; return false; }
    if (index >= found->actions.size()) { error = "world rule action index is out of range"; return false; }
    found->actions.erase(found->actions.begin() + static_cast<std::ptrdiff_t>(index));
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::setRuleOnce(const simulation::DefinitionId& ruleId, bool once,
                                 std::string& error) {
    error.clear();
    const auto found = std::find_if(authoredSource_.worldRules.begin(),
                                    authoredSource_.worldRules.end(),
        [&](const auto& value) { return value.id == ruleId; });
    if (found == authoredSource_.worldRules.end()) { error = "world rule does not exist"; return false; }
    found->once = once;
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::addEncounter(maps::EncounterDefinition encounter, std::string& error) {
    error.clear();
    if (encounter.id.empty()) { error = "encounter id cannot be empty"; return false; }
    const auto found = std::find_if(authoredSource_.encounters.begin(),
                                    authoredSource_.encounters.end(),
        [&](const auto& value) { return value.id == encounter.id; });
    if (found != authoredSource_.encounters.end()) {
        error = "encounter id already exists";
        return false;
    }
    authoredSource_.encounters.push_back(std::move(encounter));
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::removeEncounter(const simulation::DefinitionId& encounterId,
                                     std::string& error) {
    error.clear();
    const auto found = std::find_if(authoredSource_.encounters.begin(),
                                    authoredSource_.encounters.end(),
        [&](const auto& value) { return value.id == encounterId; });
    if (found == authoredSource_.encounters.end()) { error = "encounter does not exist"; return false; }
    authoredSource_.encounters.erase(found);
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::addEncounterParticipant(const simulation::DefinitionId& encounterId,
                                             simulation::PersistentInstanceId participant,
                                             std::string& error) {
    error.clear();
    const auto found = std::find_if(authoredSource_.encounters.begin(),
                                    authoredSource_.encounters.end(),
        [&](const auto& value) { return value.id == encounterId; });
    if (found == authoredSource_.encounters.end()) { error = "encounter does not exist"; return false; }
    if (!participant) { error = "encounter participant id cannot be zero"; return false; }
    if (std::find(found->participants.begin(), found->participants.end(), participant) !=
        found->participants.end()) { error = "encounter participant already exists"; return false; }
    found->participants.push_back(participant);
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::removeEncounterParticipant(const simulation::DefinitionId& encounterId,
                                                simulation::PersistentInstanceId participant,
                                                std::string& error) {
    error.clear();
    const auto found = std::find_if(authoredSource_.encounters.begin(),
                                    authoredSource_.encounters.end(),
        [&](const auto& value) { return value.id == encounterId; });
    if (found == authoredSource_.encounters.end()) { error = "encounter does not exist"; return false; }
    const auto participantIt = std::find(found->participants.begin(), found->participants.end(), participant);
    if (participantIt == found->participants.end()) { error = "encounter participant does not exist"; return false; }
    found->participants.erase(participantIt);
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::setEncounterRewardGrant(
    const simulation::DefinitionId& encounterId,
    std::optional<simulation::DefinitionId> rewardGrantId, std::string& error) {
    error.clear();
    const auto found = std::find_if(authoredSource_.encounters.begin(),
                                    authoredSource_.encounters.end(),
        [&](const auto& value) { return value.id == encounterId; });
    if (found == authoredSource_.encounters.end()) { error = "encounter does not exist"; return false; }
    if (rewardGrantId && rewardGrantId->empty()) { error = "reward grant id cannot be empty"; return false; }
    found->rewardGrantId = std::move(rewardGrantId);
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::clearEncounterRewardGrant(const simulation::DefinitionId& encounterId,
                                               std::string& error) {
    return setEncounterRewardGrant(encounterId, std::nullopt, error);
}

bool EditorDocument::addScene(game::gameplay::scenes::SceneDefinition scene,
                              std::string& error) {
    error.clear();
    if (scene.id.empty()) { error = "scene id cannot be empty"; return false; }
    if (std::any_of(authoredSource_.scenes.begin(), authoredSource_.scenes.end(),
                    [&](const auto& value) { return value.id == scene.id; })) {
        error = "scene id already exists";
        return false;
    }
    authoredSource_.scenes.push_back(std::move(scene));
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::removeScene(const simulation::DefinitionId& sceneId,
                                 std::string& error) {
    error.clear();
    const auto found = std::find_if(authoredSource_.scenes.begin(), authoredSource_.scenes.end(),
                                    [&](const auto& value) { return value.id == sceneId; });
    if (found == authoredSource_.scenes.end()) { error = "scene does not exist"; return false; }
    // Keep WorldLogic references honest.  A scene can only be deleted after
    // its map-local startScene actions have been removed or retargeted.
    const auto referenced = std::any_of(authoredSource_.worldRules.begin(),
        authoredSource_.worldRules.end(), [&](const auto& rule) {
            return std::any_of(rule.actions.begin(), rule.actions.end(), [&](const auto& action) {
                return action.kind == maps::WorldActionKind::startScene &&
                       action.definitionTarget == sceneId;
            });
        });
    if (referenced) { error = "scene is referenced by a world rule"; return false; }
    authoredSource_.scenes.erase(found);
    commitAuthoredMutation();
    return true;
}

bool EditorDocument::updateScene(const simulation::DefinitionId& sceneId,
                                 game::gameplay::scenes::SceneDefinition scene,
                                 std::string& error) {
    error.clear();
    if (scene.id.empty()) { error = "scene id cannot be empty"; return false; }
    if (scene.id != sceneId && std::any_of(authoredSource_.scenes.begin(),
                                           authoredSource_.scenes.end(),
                                           [&](const auto& value) { return value.id == scene.id; })) {
        error = "scene id already exists";
        return false;
    }
    const auto found = std::find_if(authoredSource_.scenes.begin(), authoredSource_.scenes.end(),
                                    [&](const auto& value) { return value.id == sceneId; });
    if (found == authoredSource_.scenes.end()) { error = "scene does not exist"; return false; }
    if (scene.id != sceneId) {
        for (auto& rule : authoredSource_.worldRules) {
            for (auto& action : rule.actions) {
                if (action.kind == maps::WorldActionKind::startScene &&
                    action.definitionTarget == sceneId) action.definitionTarget = scene.id;
            }
        }
    }
    *found = std::move(scene);
    commitAuthoredMutation();
    return true;
}

ValidationReport EditorDocument::validate(const game::GameContentRegistry& content) const {
    ValidationReport report;
    const auto catalogs = game::mapValidationCatalogs(content);
    auto authored = maps::authoredMapFromMapData(data_);
    for (const auto& region : regions_) {
        const auto found = std::find_if(authored.regions.begin(), authored.regions.end(),
            [&](const auto& value) { return value.id.value() == region.regionId; });
        if (found == authored.regions.end()) {
            authored.regions.push_back({simulation::DefinitionId{region.regionId}, region.bounds,
                                        region.environmentEffectId});
        } else {
            found->bounds = region.bounds;
            found->environmentEffectId = region.environmentEffectId;
        }
    }
    const auto base = maps::validateMapData(maps::mapDataFromAuthored(authored), &catalogs);
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
    for (const auto& value : data_.npcs) { allIds.insert(value.id.value); }
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
    return report;
}

void EditorDocument::synchronizeAuthoredSource() {
    authoredSource_ = maps::authoredMapFromMapData(data_);
    for (const auto& region : regions_) {
        const auto found = std::find_if(authoredSource_.regions.begin(), authoredSource_.regions.end(),
            [&](const auto& value) { return value.id.value() == region.regionId; });
        if (found == authoredSource_.regions.end()) {
            authoredSource_.regions.push_back({simulation::DefinitionId{region.regionId}, region.bounds,
                                               region.environmentEffectId});
        } else {
            found->bounds = region.bounds;
            found->environmentEffectId = region.environmentEffectId;
        }
    }
    authoredSource_.placementOverrides.clear();
    for (const auto& [instance, overrides] : propertyOverrides_) {
        for (const auto& [property, value] : overrides) {
            maps::AuthoredPlacementOverride authoredOverride;
            authoredOverride.instanceId = {instance};
            authoredOverride.propertyId = property.value();
            if (const auto* boolean = std::get_if<bool>(&value)) {
                authoredOverride.value.kind = maps::AuthoredPropertyValueKind::boolean;
                authoredOverride.value.booleanValue = *boolean;
            } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
                authoredOverride.value.kind = maps::AuthoredPropertyValueKind::integer;
                authoredOverride.value.integerValue = *integer;
            } else if (const auto* enumeration = std::get_if<EnumPropertyValue>(&value)) {
                authoredOverride.value.kind = maps::AuthoredPropertyValueKind::enumeration;
                authoredOverride.value.textValue = enumeration->value;
            } else if (const auto* definition = std::get_if<DefinitionReference>(&value)) {
                authoredOverride.value.kind = maps::AuthoredPropertyValueKind::definitionReference;
                authoredOverride.value.definitionValue = definition->id;
            } else {
                authoredOverride.value.kind = maps::AuthoredPropertyValueKind::instanceReference;
                authoredOverride.value.instanceValue = std::get<InstanceReference>(value).id;
            }
            authoredSource_.placementOverrides.push_back(std::move(authoredOverride));
        }
    }
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
    for (const auto& value : data_.npcs) { see(value.id); }
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
