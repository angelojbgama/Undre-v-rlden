#include "editor/editor_playtest.h"

#include "game/game_launch.h"

#include <algorithm>
#include <array>

namespace underworld::editor {

EditorPlaytestSession::~EditorPlaytestSession() { stop(); }

bool EditorPlaytestSession::start(const game::maps::MapData& data,
                                  const game::GameContentRegistry& content,
                                  std::string& error) {
    game::maps::AuthoredWorldSource source;
    source.entryMapId = data.id;
    source.maps.push_back(game::maps::authoredMapFromMapData(data));
    return start(source, content, data.id, error);
}

bool EditorPlaytestSession::start(const game::maps::AuthoredWorldSource& source,
                                  const game::GameContentRegistry& content,
                                  const std::optional<simulation::MapId>& startMap,
                                  std::string& error) {
    stop();
    error.clear();
    const auto compiled = game::maps::compileAuthoredWorld(source, content);
    if (!compiled.valid()) { error = "playtest cannot start: " + compiled.diagnostics.front().message; return false; }
    const auto* progression = content.progressions().find(
        game::gameplay::rpg::defaultPlayerProgressionId());
    if (!progression) { error = "playtest cannot start: default player progression is unavailable"; return false; }
    try {
        runtimeTilesets_ = std::make_unique<game::RuntimeTilesetCatalog>(content.tilesets());
        validationCatalogs_ = game::mapValidationCatalogs(content);
        const std::array visuals{game::gameplay::creatures::soldierVisualId(),
                                 game::gameplay::creatures::skullVisualId()};
        enemyFactory_ = std::make_unique<game::gameplay::creatures::EnemyFactory>(
            content.enemies(), content.behaviors(), content.attacks(), content.projectiles(), visuals);
        objectFactory_ = std::make_unique<game::gameplay::WorldObjectFactory>(content.objects(), content.items());
        npcFactory_ = std::make_unique<game::gameplay::npcs::NpcFactory>(content.npcs());
        runtimeBuilder_ = std::make_unique<game::maps::RuntimeWorldBuilder>(
            validationCatalogs_, *enemyFactory_, *objectFactory_, *runtimeTilesets_, npcFactory_.get());
        for (const auto& compiledMap : compiled.maps) mapCatalog_.addData(compiledMap.data);
        session_ = std::make_unique<game::GameSession>(simulation::PlayerId{0}, *progression);
        session_->configureCombat(content.attacks(), content.projectiles(), content.behaviors(),
            content.attacks().require(game::gameplay::playerSwordAttackId()),
            content.attacks().require(game::gameplay::playerBowAttackId()));
        session_->configureItems(content.items());
        session_->configureNarrative(content.dialogues(), content.quests());
        session_->configureRewards(content.rewardProfiles(), content.pickups());
        session_->configureRewardGrants(content.rewardGrants());
        session_->configureShops(content.shops());
        const auto requested = startMap.value_or(source.entryMapId);
        const auto map = std::find_if(compiled.maps.begin(), compiled.maps.end(),
            [&](const auto& value) { return value.id == requested; });
        if (map == compiled.maps.end()) { error = "playtest start map is not in project"; stop(); return false; }
        std::string spawnError;
        const auto spawn = game::selectStartupSpawn(map->data, std::nullopt, spawnError);
        if (!spawn) { error = "playtest cannot start: " + spawnError; stop(); return false; }
        if (!session_->initializeMap(mapCatalog_, validationCatalogs_, *runtimeBuilder_,
                                     requested, *spawn, error)) {
            error = "playtest runtime build failed: " + error; stop(); return false;
        }
        return true;
    } catch (const std::exception& exception) { error = exception.what(); stop(); return false; }
}

void EditorPlaytestSession::stop() noexcept {
    session_.reset();
    runtimeBuilder_.reset(); npcFactory_.reset(); objectFactory_.reset(); enemyFactory_.reset();
    runtimeTilesets_.reset();
    mapCatalog_ = game::maps::MapCatalog{};
}

void EditorPlaytestSession::tick(const simulation::PlayerCommand& command) {
    if (session_) session_->tick(command);
}

void EditorPlaytestSession::relocatePlayer(core::WorldPointI position,
                                           game::gameplay::FacingDirection facing) noexcept {
    if (session_) session_->relocatePlayer(position, facing);
}

const game::maps::RuntimeWorld* EditorPlaytestSession::world() const noexcept {
    return session_ ? &session_->world() : nullptr;
}

const game::maps::MapData* EditorPlaytestSession::sourceData() const noexcept {
    return session_ ? &session_->mapData() : nullptr;
}

const simulation::MapId* EditorPlaytestSession::activeMapId() const noexcept {
    const auto* value = world(); return value ? &value->id() : nullptr;
}

} // namespace underworld::editor
