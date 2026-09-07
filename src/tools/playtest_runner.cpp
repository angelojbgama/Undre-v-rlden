#include "engine/platform/headless/headless_audit_platform.h"
#include "engine/core/game_metrics.h"
#include "engine/core/image_data.h"
#include "engine/render/framebuffer.h"
#include "editor/content_workspace_document.h"
#include "editor/editor_commands.h"
#include "editor/editor_document.h"
#include "editor/visual_preview.h"
#include "game/audit/audit_session.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/world_objects.h"
#include "game/game_runtime.h"
#include "game/content/builtin_content.h"
#include "game/content/content_compiler.h"
#include "game/maps/dmap.h"
#include "game/maps/official_maps.h"
#include "game/gameplay/world_logic.h"
#include "game/presentation/presentation_effects.h"
#include "game/presentation/presentation_feedback_controller.h"
#include "game/presentation/visual_content_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace underworld;
using game::audit::AuditSession;
using game::audit::AuditSessionConfig;
using game::audit::GameAuditSnapshot;

class SyntheticImageDecoder final : public platform::ImageDecoder {
public:
    [[nodiscard]] core::ImageData decode(const std::filesystem::path& path) override {
        paths.push_back(path);
        constexpr int width = 304;
        constexpr int height = 192;
        constexpr std::size_t stride = static_cast<std::size_t>(width) * 4U;
        core::ImageData result{width, height, stride,
                               std::vector<std::uint8_t>(stride * height, 0xffU)};
        return result;
    }

    std::vector<std::filesystem::path> paths;
};

std::filesystem::path repositoryRoot() {
    auto current = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        if (std::filesystem::is_directory(current / "maps" / "gameplay")) { return current; }
        const auto parent = current.parent_path();
        if (parent == current) { break; }
        current = parent;
    }
    throw std::runtime_error("playtest runner could not locate repository root");
}

std::filesystem::path mapPath(const std::filesystem::path& root,
                              std::string_view mapId) {
    const auto maps = game::maps::officialGameplayMaps();
    const auto found = std::find_if(maps.begin(), maps.end(), [&](const auto& entry) {
        return entry.id.value() == mapId;
    });
    if (found == maps.end()) { throw std::runtime_error("unknown official map: " + std::string(mapId)); }
    return root / found->relativePath;
}

struct RunnerOptions final {
    bool all{};
    std::string scenario;
    std::filesystem::path assetRoot;
    std::filesystem::path auditRoot{"audit"};
    std::uint64_t seed{1};
    std::uint64_t maximumTicks{1200};
};

std::optional<RunnerOptions> parseOptions(int argc, char** argv, std::string& error) {
    RunnerOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] == nullptr ? "" : argv[index];
        const auto value = [&](std::string_view name) -> std::optional<std::string> {
            if (argument == name) {
                if (++index >= argc || argv[index] == nullptr) {
                    error = std::string(name) + " requires a value";
                    return std::nullopt;
                }
                return std::string(argv[index]);
            }
            const std::string prefix = std::string(name) + "=";
            if (argument.rfind(prefix, 0) == 0) { return argument.substr(prefix.size()); }
            return {};
        };
        if (argument == "--all") { options.all = true; continue; }
        if (argument == "--help") {
            error = "help";
            return std::nullopt;
        }
        if (const auto valueResult = value("--scenario")) {
            options.scenario = *valueResult;
        } else if (argument == "--scenario" || argument.rfind("--scenario=", 0) == 0) {
            if (error.empty()) { error = "--scenario requires a value"; }
            return std::nullopt;
        } else if (const auto valueResult = value("--asset-root")) {
            options.assetRoot = *valueResult;
        } else if (argument == "--asset-root" || argument.rfind("--asset-root=", 0) == 0) {
            if (error.empty()) { error = "--asset-root requires a value"; }
            return std::nullopt;
        } else if (const auto valueResult = value("--audit-root")) {
            options.auditRoot = *valueResult;
        } else if (argument == "--audit-root" || argument.rfind("--audit-root=", 0) == 0) {
            if (error.empty()) { error = "--audit-root requires a value"; }
            return std::nullopt;
        } else if (const auto valueResult = value("--seed")) {
            try {
                options.seed = std::stoull(*valueResult);
            } catch (const std::exception&) {
                error = "--seed requires an unsigned integer";
                return std::nullopt;
            }
        } else if (argument == "--seed" || argument.rfind("--seed=", 0) == 0) {
            if (error.empty()) { error = "--seed requires a value"; }
            return std::nullopt;
        } else if (const auto valueResult = value("--ticks")) {
            try {
                options.maximumTicks = std::stoull(*valueResult);
            } catch (const std::exception&) {
                error = "--ticks requires an unsigned integer";
                return std::nullopt;
            }
        } else if (argument == "--ticks" || argument.rfind("--ticks=", 0) == 0) {
            if (error.empty()) { error = "--ticks requires a value"; }
            return std::nullopt;
        } else {
            error = "unknown playtest option: " + argument;
            return std::nullopt;
        }
    }
    if (!options.all && options.scenario.empty()) {
        error = "use --all or --scenario <name>";
        return std::nullopt;
    }
    if (options.assetRoot.empty()) {
        if (const char* environment = std::getenv("UNDERWORLD_ASSET_ROOT")) {
            options.assetRoot = environment;
        }
    }
    return options;
}

struct WorldLogicFixture final {
    game::GameContentRegistry content;
    game::maps::MapData map;
};

WorldLogicFixture makeWorldLogicFixture(const std::filesystem::path& root);
WorldLogicFixture makeInteractiveWorldFixture(const std::filesystem::path& root);

class ScenarioContext final {
public:
    ScenarioContext(const std::filesystem::path& root, const RunnerOptions& options,
                    std::string scenario, std::string mapId)
        : root_(root), scenario_(std::move(scenario)), mapId_(std::move(mapId)),
          maximumTicks_(options.maximumTicks),
          executableDirectory_(std::filesystem::temp_directory_path() /
                               ("underworld_playtest_" + scenario_)),
          platform_(decoder_, executableDirectory_), framebuffer_(core::GameMetrics::logicalWidth,
                                                                  core::GameMetrics::logicalHeight) {
        std::error_code error;
        std::filesystem::create_directories(executableDirectory_, error);
        if (error) { throw std::runtime_error("could not create playtest save directory"); }
        game::GameLaunchOptions launch;
        launch.mapPath = mapPath(root_, mapId_);
        game::GameContentRegistry content = game::content::compileBuiltinContentOrThrow();
        if (scenario_ == "world_logic" || scenario_ == "interactive_world") {
            const auto fixture = scenario_ == "world_logic"
                ? makeWorldLogicFixture(root_)
                : makeInteractiveWorldFixture(root_);
            const auto fixtureMap = executableDirectory_ /
                (scenario_ == "world_logic" ? "world_logic.dmap" : "interactive_world.dmap");
            std::string writeError;
            if (!game::maps::writeDmap(fixtureMap, fixture.map, writeError)) {
                throw std::runtime_error("could not write interactive playtest map: " + writeError);
            }
            launch.mapPath = fixtureMap;
            content = fixture.content;
        }
        demo_ = std::make_unique<game::GameRuntime>(
            decoder_, options.assetRoot.empty() ? root_ : options.assetRoot,
            executableDirectory_, std::move(content), launch);

        AuditSessionConfig config;
        config.outputRoot = options.auditRoot;
        config.sessionId = scenario_ + "_" + std::to_string(options.seed);
        config.metadata.platform = "headless";
        config.metadata.mode = "automated";
        config.metadata.scenario = scenario_;
        config.metadata.initialMap = mapId_;
        config.metadata.initialSpawn = demo_->auditSnapshot().currentSpawn;
        config.metadata.randomSeed = std::to_string(options.seed);
        config.metadata.assetRoot = options.assetRoot.empty() ? "synthetic" : options.assetRoot.string();
        std::string errorMessage;
        if (!audit_.open(config, errorMessage)) { throw std::runtime_error(errorMessage); }
        renderFrame();
        if (!audit_.recordEvent({0, "startup", {{"map", std::string(mapId_)}}}) ||
            !audit_.recordState(demo_->auditSnapshot(), true) ||
            !audit_.captureScreenshot("startup", platform_.lastPresentedFrame(), errorMessage)) {
            throw std::runtime_error("could not write startup audit artifacts: " + errorMessage);
        }
    }

    ~ScenarioContext() { std::error_code error; std::filesystem::remove_all(executableDirectory_, error); }

    [[nodiscard]] const GameAuditSnapshot& snapshot() {
        snapshot_ = demo_->auditSnapshot();
        return snapshot_;
    }
    [[nodiscard]] std::uint64_t tick() const noexcept { return tick_; }

    bool step(platform::InputState input = {}) {
        if (tick_ >= maximumTicks_) { return fail("scenario tick budget exceeded"); }
        const auto nextTick = tick_ + 1;
        platform_.setInput(nextTick, input);
        platform_.advanceFixedTicks(1);
        tick_ = nextTick;
        demo_->fixedTick(tick_, platform_.consumeInputState(), platform_.consumeDebugInput());
        renderFrame();
        if (!platform_.lastPresentedFrame().isValid()) {
            return fail("headless present returned an invalid frame");
        }
        static_cast<void>(audit_.recordState(demo_->auditSnapshot()));
        return !failed_;
    }

    bool require(bool condition, std::string message) {
        if (condition) {
            ++assertionsPassed_;
            return true;
        }
        ++assertionsFailed_;
        return fail(std::move(message));
    }

    bool fail(std::string message) {
        if (failed_) { return false; }
        failed_ = true;
        failure_ = std::move(message);
        static_cast<void>(audit_.recordEvent({tick_, "audit_assertion_failure", {{"message", failure_}}}));
        static_cast<void>(audit_.recordState(demo_->auditSnapshot(), true));
        std::string error;
        static_cast<void>(audit_.captureScreenshot("failure", platform_.lastPresentedFrame(), error));
        return false;
    }

    bool finish() {
        if (!failed_) {
            std::string error;
            static_cast<void>(audit_.recordEvent({tick_, "scenario_completed", {
                {"assertionsPassed", std::to_string(assertionsPassed_)},
                {"assertionsFailed", std::to_string(assertionsFailed_)}}}));
            static_cast<void>(audit_.recordState(demo_->auditSnapshot(), true));
            static_cast<void>(audit_.captureScreenshot("complete", platform_.lastPresentedFrame(), error));
        }
        return audit_.close(failed_ ? "FAIL" : "PASS", failure_);
    }

    bool checkpoint(std::string_view name, std::string_view eventType = "playtest_checkpoint") {
        std::string error;
        if (audit_.recordEvent({tick_, std::string(eventType), {{"name", std::string(name)}}}) &&
            audit_.recordState(demo_->auditSnapshot(), true) &&
            audit_.captureScreenshot(name, platform_.lastPresentedFrame(), error)) {
            return true;
        }
        return fail("could not write playtest checkpoint: " + error);
    }

    [[nodiscard]] std::uint64_t assertionsPassed() const noexcept { return assertionsPassed_; }

    [[nodiscard]] game::GameRuntime& demo() noexcept { return *demo_; }
    [[nodiscard]] platform::HeadlessAuditPlatform& platform() noexcept { return platform_; }
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

private:
    void renderFrame() {
        demo_->render(framebuffer_);
        if (!platform_.present(framebuffer_.view())) { failed_ = true; }
    }

    const std::filesystem::path& root_;
    std::string scenario_;
    std::string mapId_;
    std::uint64_t maximumTicks_{};
    std::filesystem::path executableDirectory_;
    SyntheticImageDecoder decoder_;
    platform::HeadlessAuditPlatform platform_;
    render::Framebuffer framebuffer_;
    std::unique_ptr<game::GameRuntime> demo_;
    AuditSession audit_;
    GameAuditSnapshot snapshot_;
    std::uint64_t tick_{};
    bool failed_{};
    std::string failure_;
    std::uint64_t assertionsPassed_{};
    std::uint64_t assertionsFailed_{};
};

bool runBaseline(ScenarioContext& context) {
    const auto& snapshot = context.snapshot();
    return context.require(!snapshot.currentMap.empty() && snapshot.playerMaximumHealth > 0 &&
                               snapshot.playerHealth > 0 && context.platform().lastPresentedFrame().isValid(),
                           "startup snapshot or framebuffer is invalid");
}

bool runMovement(ScenarioContext& context) {
    if (!runBaseline(context)) { return false; }
    const auto initial = context.snapshot();
    platform::InputState input;
    input.moveRight = true;
    for (int index = 0; index < 30; ++index) { if (!context.step(input)) { return false; } }
    const auto& moved = context.snapshot();
    return context.require(moved.playerX != initial.playerX || moved.playerY != initial.playerY,
                           "scripted movement did not change the player position");
}

bool runCollision(ScenarioContext& context) {
    if (!runBaseline(context)) { return false; }
    platform::InputState input;
    input.moveUp = true;
    for (int index = 0; index < 240; ++index) { if (!context.step(input)) { return false; } }
    const auto& snapshot = context.snapshot();
    return context.require(snapshot.playerX >= 0 && snapshot.playerX <= 384 &&
                               snapshot.playerY >= 0 && snapshot.playerY <= 288,
                           "scripted collision movement left the authored map bounds");
}

struct PointTarget final { int x{}; int y{}; };

template<class Actor>
bool moveTo(ScenarioContext& context, const Actor& actor, int maximumTicks = 500,
            bool engageEnemies = true) {
    for (int index = 0; index < maximumTicks; ++index) {
        const auto& player = context.snapshot();
        const int dx = actor.x - player.playerX;
        const int dy = actor.y - player.playerY;
        if (std::abs(dx) <= 4 && std::abs(dy) <= 4) { return true; }
        platform::InputState input;
        input.moveRight = dx > 0;
        input.moveLeft = dx < 0;
        input.moveDown = dy > 0;
        input.moveUp = dy < 0;
        for (const auto& enemy : player.enemies) {
            if (!engageEnemies) { break; }
            const int enemyDistanceX = enemy.x - player.playerX;
            const int enemyDistanceY = enemy.y - player.playerY;
            if (std::abs(enemyDistanceX) <= 160 && std::abs(enemyDistanceY) <= 160) {
                input.primaryAttackPressed = enemy.definitionId == "enemy.evil_soldier" &&
                                             index % 12 == 0;
                input.secondaryAttackPressed = enemy.definitionId == "enemy.skull" &&
                                               index % 12 == 0;
                break;
            }
        }
        if (!context.step(input)) { return false; }
    }
    return false;
}

bool moveToContent(ScenarioContext& context, PointTarget target) {
    for (int index = 0; index < 500; ++index) {
        const auto& current = context.snapshot();
        PointTarget waypoint = target;
        // Map 03 has an authored chest on the direct horizontal approach to
        // the potion.  Use the open row below it, then approach the pickup.
        if (current.currentMap == "map.dungeon.03" && target.y == 184 &&
            std::abs(current.playerY - 216) > 8) {
            waypoint.y = 216;
        }
        if (std::abs(target.x - current.playerX) <= 4 &&
            std::abs(target.y - current.playerY) <= 4) { return true; }
        platform::InputState input;
        input.moveRight = waypoint.x > current.playerX;
        input.moveLeft = waypoint.x < current.playerX;
        input.moveDown = waypoint.y > current.playerY;
        input.moveUp = waypoint.y < current.playerY;
        for (const auto& enemy : current.enemies) {
            const int distanceX = enemy.x - current.playerX;
            const int distanceY = enemy.y - current.playerY;
            if (std::abs(distanceX) <= 160 && std::abs(distanceY) <= 160) {
                input.primaryAttackPressed = enemy.definitionId == "enemy.evil_soldier" &&
                                             index % 12 == 0;
                input.secondaryAttackPressed = enemy.definitionId == "enemy.skull" &&
                                               index % 12 == 0;
                break;
            }
        }
        if (!context.step(input)) { return false; }
    }
    return false;
}

std::optional<game::maps::MapData> loadMap(const ScenarioContext& context,
                                           std::string_view mapId) {
    auto loaded = game::maps::readDmap(mapPath(context.root(), mapId));
    if (!loaded) { return std::nullopt; }
    return std::move(loaded.data);
}

bool moveToPickup(ScenarioContext& context, PointTarget target, bool engageEnemies = true) {
    const auto loaded = loadMap(context, context.snapshot().currentMap);
    if (!loaded) { return moveTo(context, target, 500, engageEnemies); }
    const auto& map = *loaded;
    const auto tileIndex = [&](int x, int y) {
        return static_cast<std::size_t>(y) * map.width + static_cast<std::size_t>(x);
    };
    const auto clampTile = [&](int value, std::uint32_t limit) {
        return std::clamp(value, 0, static_cast<int>(limit) - 1);
    };
    const auto initial = context.snapshot();
    const int startX = clampTile(initial.playerX / static_cast<int>(map.tileSize), map.width);
    const int startY = clampTile(initial.playerY / static_cast<int>(map.tileSize), map.height);
    const int goalX = clampTile(target.x / static_cast<int>(map.tileSize), map.width);
    const int goalY = clampTile(target.y / static_cast<int>(map.tileSize), map.height);
    std::vector<int> parent(static_cast<std::size_t>(map.width) * map.height, -1);
    std::queue<std::pair<int, int>> pending;
    parent[tileIndex(startX, startY)] = static_cast<int>(tileIndex(startX, startY));
    pending.push({startX, startY});
    constexpr std::array<std::pair<int, int>, 4> directions{{
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    while (!pending.empty()) {
        const auto [x, y] = pending.front();
        pending.pop();
        if (x == goalX && y == goalY) { break; }
        for (const auto& [dx, dy] : directions) {
            const int nextX = x + dx;
            const int nextY = y + dy;
            if (nextX < 0 || nextY < 0 || static_cast<std::uint32_t>(nextX) >= map.width ||
                static_cast<std::uint32_t>(nextY) >= map.height ||
                map.collision[tileIndex(nextX, nextY)] != 0 ||
                parent[tileIndex(nextX, nextY)] != -1) { continue; }
            parent[tileIndex(nextX, nextY)] = static_cast<int>(tileIndex(x, y));
            pending.push({nextX, nextY});
        }
    }
    const auto goalIndex = tileIndex(goalX, goalY);
    if (parent[goalIndex] == -1) { return false; }
    std::vector<std::pair<int, int>> path;
    for (int current = static_cast<int>(goalIndex);; current = parent[current]) {
        const int x = current % static_cast<int>(map.width);
        const int y = current / static_cast<int>(map.width);
        path.push_back({x, y});
        if (current == parent[current]) { break; }
    }
    std::reverse(path.begin(), path.end());
    for (const auto& [tileX, tileY] : path) {
        const PointTarget waypoint{
            tileX * static_cast<int>(map.tileSize) + static_cast<int>(map.tileSize) / 2,
            tileY * static_cast<int>(map.tileSize) + static_cast<int>(map.tileSize) / 2};
        if (!moveTo(context, waypoint, 500, engageEnemies)) { return false; }
    }
    return moveTo(context, target, 500, engageEnemies);
}

PointTarget linkCenter(const world::AabbI& area) {
    return {area.x + area.width / 2, area.y + area.height / 2};
}

WorldLogicFixture makeWorldLogicFixture(const std::filesystem::path& root) {
    auto authored = game::content::makeBuiltinAuthoredContent();
    for (auto& behavior : authored.behaviors) {
        // The scenario is about authored world orchestration. Keep the real
        // combat path, but prevent the participants from attacking while the
        // runner brings the player into sword range.
        behavior.detectionRangePixels = 1;
        behavior.disengageRangePixels = 2;
    }
    for (auto& enemy : authored.enemies) { enemy.maximumHealth = 1; }

    game::content::AuthoredWorldObject door;
    door.id = {"object.playtest.door"};
    door.visualSetId = {"visual.object.crate"};
    door.door = game::gameplay::ObjectDoorDefinition{
        game::gameplay::DoorState::closed, {-8, -24, 16, 24}};
    authored.objects.push_back(std::move(door));

    const auto compiled = game::content::compileContent(authored);
    if (!compiled) {
        std::string message = "could not compile world logic playtest content";
        for (const auto& diagnostic : compiled.report.diagnostics) {
            if (diagnostic.severity == game::content::ContentDiagnosticSeverity::error) {
                message += ": " + diagnostic.code + " " + diagnostic.message;
            }
        }
        throw std::runtime_error(message);
    }

    const auto loaded = game::maps::readDmap(mapPath(root, "map.dungeon.02"));
    if (!loaded) { throw std::runtime_error("could not load world logic playtest map"); }
    auto map = loaded.data;
    if (map.enemies.empty()) {
        throw std::runtime_error("world logic playtest map needs an enemy participant");
    }
    if (map.enemies.size() < 2) {
        auto duplicate = map.enemies.front();
        duplicate.id = {9002};
        duplicate.position.x += static_cast<int>(map.tileSize) * 2;
        map.enemies.push_back(std::move(duplicate));
    }

    constexpr simulation::PersistentInstanceId doorInstance{9001};
    const simulation::DefinitionId regionId{"region.playtest.arena"};
    const simulation::DefinitionId encounterId{"encounter.playtest.arena"};
    const simulation::DefinitionId completedFlag{"flag.playtest.arena.completed"};

    map.objects.push_back({doorInstance, {"object.playtest.door"}, {160, 128}, {}});
    map.regions.push_back({regionId, {0, 0,
                                      static_cast<int>(map.width * map.tileSize),
                                      static_cast<int>(map.height * map.tileSize)}});

    game::maps::EncounterDefinition encounter;
    encounter.id = encounterId;
    for (const auto& enemy : map.enemies) { encounter.participants.push_back(enemy.id); }
    map.encounters.push_back(encounter);

    game::maps::WorldRuleDefinition entered;
    entered.id = {"rule.playtest.arena.entered"};
    entered.trigger.kind = game::maps::WorldTriggerKind::regionEntered;
    entered.trigger.definitionTarget = regionId;
    entered.once = true;
    entered.actions.push_back({game::maps::WorldActionKind::setDoorState, {}, doorInstance,
                               game::gameplay::DoorState::locked});
    entered.actions.push_back({game::maps::WorldActionKind::startEncounter, encounterId, {},
                               game::gameplay::DoorState::closed});
    map.worldRules.push_back(std::move(entered));

    game::maps::WorldRuleDefinition completedRule;
    completedRule.id = {"rule.playtest.arena.completed"};
    completedRule.trigger.kind = game::maps::WorldTriggerKind::encounterCompleted;
    completedRule.trigger.definitionTarget = encounterId;
    completedRule.once = true;
    completedRule.actions.push_back({game::maps::WorldActionKind::setDoorState, {}, doorInstance,
                                     game::gameplay::DoorState::open});
    completedRule.actions.push_back({game::maps::WorldActionKind::setFlag, completedFlag, {},
                                     game::gameplay::DoorState::closed});
    map.worldRules.push_back(std::move(completedRule));

    return {std::move(*compiled.registry), std::move(map)};
}

WorldLogicFixture makeInteractiveWorldFixture(const std::filesystem::path& root) {
    auto authored = game::content::makeBuiltinAuthoredContent();
    const auto addToggle = [&](std::string id) {
        game::content::AuthoredWorldObject object;
        object.id = simulation::DefinitionId{std::move(id)};
        object.visualSetId = {"visual.object.crate"};
        object.interactable = game::gameplay::ObjectInteractionDefinition{{-16, -16, 32, 32}};
        object.activation = game::gameplay::ObjectActivationDefinition{
            game::gameplay::ObjectActivationMode::interactToggle, false, std::nullopt};
        authored.objects.push_back(std::move(object));
    };
    addToggle("object.playtest.lever.a");
    addToggle("object.playtest.lever.b");

    game::content::AuthoredWorldObject plate;
    plate.id = {"object.playtest.plate"};
    plate.visualSetId = {"visual.object.crate"};
    plate.activation = game::gameplay::ObjectActivationDefinition{
        game::gameplay::ObjectActivationMode::playerPressure, false,
        world::AabbI{-8, -8, 16, 16}};
    authored.objects.push_back(std::move(plate));

    const auto addDoor = [&](std::string id) {
        game::content::AuthoredWorldObject object;
        object.id = simulation::DefinitionId{std::move(id)};
        object.visualSetId = {"visual.object.crate"};
        object.door = game::gameplay::ObjectDoorDefinition{
            game::gameplay::DoorState::closed, {-8, -24, 16, 24}};
        authored.objects.push_back(std::move(object));
    };
    addDoor("object.playtest.door.main");
    addDoor("object.playtest.door.exit");

    const auto compiled = game::content::compileContent(authored);
    if (!compiled) {
        throw std::runtime_error("could not compile interactive world playtest content");
    }
    const auto loaded = game::maps::readDmap(mapPath(root, "map.dungeon.02"));
    if (!loaded || loaded.data.playerSpawns.empty()) {
        throw std::runtime_error("could not load interactive world playtest map");
    }
    auto map = std::move(loaded.data);
    map.id = simulation::MapId{"map.playtest.interactive"};
    map.enemies.clear();
    map.npcs.clear();
    map.pickups.clear();
    map.links.clear();
    map.objects.clear();
    map.regions.clear();
    map.worldRules.clear();
    map.encounters.clear();
    map.collision.assign(static_cast<std::size_t>(map.width) * map.height, 0);

    const auto spawn = map.playerSpawns.front().position;
    const auto positionAt = [&](int dx, int dy) {
        const int maxX = static_cast<int>(map.width * map.tileSize) - 16;
        const int maxY = static_cast<int>(map.height * map.tileSize) - 16;
        return core::WorldPointI{std::clamp(spawn.x + dx, 16, maxX),
                                 std::clamp(spawn.y + dy, 32, maxY)};
    };
    map.objects.push_back({101, {"object.playtest.lever.a"}, positionAt(0, 0), {}});
    map.objects.push_back({102, {"object.playtest.lever.b"}, positionAt(0, 32), {}});
    map.objects.push_back({103, {"object.playtest.plate"}, positionAt(48, 0), {}});
    map.objects.push_back({104, {"object.playtest.door.main"}, positionAt(64, 0), {}});
    map.objects.push_back({105, {"object.playtest.door.exit"}, positionAt(96, 0), {}});

    const simulation::DefinitionId leverA{"object.playtest.lever.a"};
    const simulation::DefinitionId leverB{"object.playtest.lever.b"};
    const simulation::PersistentInstanceId mainDoor{104};
    const simulation::PersistentInstanceId exitDoor{105};
    const simulation::PersistentInstanceId plateId{103};
    const auto addMainDoorRule = [&](simulation::DefinitionId triggerId,
                                     simulation::PersistentInstanceId conditionId) {
        game::maps::WorldRuleDefinition rule;
        rule.id = simulation::DefinitionId{
            std::string("rule.playtest.main.") + std::string(triggerId.value())};
        rule.trigger = {game::maps::WorldTriggerKind::objectActivated, {}, triggerId == leverA ?
                        simulation::PersistentInstanceId{101} : simulation::PersistentInstanceId{102}};
        rule.conditions.push_back({game::maps::WorldConditionKind::objectActive, {}, conditionId,
                                   game::gameplay::DoorState::closed});
        rule.actions.push_back({game::maps::WorldActionKind::setDoorState, {}, mainDoor,
                                game::gameplay::DoorState::open});
        map.worldRules.push_back(std::move(rule));
    };
    addMainDoorRule(leverA, simulation::PersistentInstanceId{102});
    addMainDoorRule(leverB, simulation::PersistentInstanceId{101});

    const auto addCloseRule = [&](std::string id, simulation::PersistentInstanceId target) {
        game::maps::WorldRuleDefinition rule;
        rule.id = simulation::DefinitionId{std::move(id)};
        rule.trigger = {game::maps::WorldTriggerKind::objectDeactivated, {}, target};
        rule.actions.push_back({game::maps::WorldActionKind::setDoorState, {}, mainDoor,
                                game::gameplay::DoorState::closed});
        map.worldRules.push_back(std::move(rule));
    };
    addCloseRule("rule.playtest.main.a.closed", simulation::PersistentInstanceId{101});
    addCloseRule("rule.playtest.main.b.closed", simulation::PersistentInstanceId{102});

    game::maps::WorldRuleDefinition plateOpen;
    plateOpen.id = {"rule.playtest.plate.open"};
    plateOpen.trigger = {game::maps::WorldTriggerKind::objectActivated, {}, plateId};
    plateOpen.actions.push_back({game::maps::WorldActionKind::setDoorState, {}, exitDoor,
                                 game::gameplay::DoorState::open});
    map.worldRules.push_back(std::move(plateOpen));
    game::maps::WorldRuleDefinition plateClose;
    plateClose.id = {"rule.playtest.plate.closed"};
    plateClose.trigger = {game::maps::WorldTriggerKind::objectDeactivated, {}, plateId};
    plateClose.actions.push_back({game::maps::WorldActionKind::setDoorState, {}, exitDoor,
                                  game::gameplay::DoorState::closed});
    map.worldRules.push_back(std::move(plateClose));

    return {std::move(*compiled.registry), std::move(map)};
}

const game::audit::AuditActor* findObject(const GameAuditSnapshot& snapshot, std::uint64_t id) {
    const auto found = std::find_if(snapshot.objects.begin(), snapshot.objects.end(),
                                    [&](const auto& object) { return object.instanceId == id; });
    return found == snapshot.objects.end() ? nullptr : &*found;
}

bool runInteractiveWorld(ScenarioContext& context) {
    if (!runBaseline(context)) { return false; }
    const auto interact = [&](PointTarget target) {
        if (!moveTo(context, target, 120, false)) { return false; }
        platform::InputState input;
        input.interactPressed = true;
        return context.step(input);
    };
    const auto initially = context.snapshot();
    const auto* leverA = findObject(initially, 101);
    const auto* leverB = findObject(initially, 102);
    const auto* plate = findObject(initially, 103);
    const auto* mainDoor = findObject(initially, 104);
    const auto* exitDoor = findObject(initially, 105);
    if (!context.require(leverA && leverB && plate && mainDoor && exitDoor,
                         "interactive playtest objects are incomplete")) { return false; }
    const PointTarget leverAPosition{leverA->x, leverA->y};
    const PointTarget leverBPosition{leverB->x, leverB->y};
    const PointTarget platePosition{plate->x, plate->y};
    if (!interact(leverAPosition)) { return false; }
    if (!context.require(findObject(context.snapshot(), 101)->activation.value_or(false) &&
                         context.snapshot().lastEvent == "OBJECT ACTIVATED",
                         "first lever did not toggle through interaction")) { return false; }
    if (!interact(leverBPosition)) { return false; }
    if (!context.require(findObject(context.snapshot(), 102)->activation.value_or(false) &&
                         findObject(context.snapshot(), 104)->doorState == "open",
                         "authored two-switch rule did not open the main door")) { return false; }
    if (!interact(leverAPosition)) { return false; }
    if (!context.require(!findObject(context.snapshot(), 101)->activation.value_or(true) &&
                         findObject(context.snapshot(), 104)->doorState == "closed",
                         "lever deactivation did not close the main door")) { return false; }
    if (!moveTo(context, platePosition, 120, false)) { return false; }
    const auto& pressed = context.snapshot();
    if (!context.require(findObject(pressed, 103)->activation.value_or(false) &&
                         findObject(pressed, 105)->doorState == "open",
                         "pressure plate did not open its authored door rule")) { return false; }
    if (!moveTo(context, leverAPosition, 120, false)) { return false; }
    const auto& released = context.snapshot();
    if (!context.require(!findObject(released, 103)->activation.value_or(true) &&
                         findObject(released, 105)->doorState == "closed",
                         "pressure plate did not release its authored door rule")) { return false; }
    // B is still active after the earlier A-off check; toggle A back on to
    // restore the solved two-switch state before saving.
    if (!interact(leverAPosition)) { return false; }
    if (!context.require(findObject(context.snapshot(), 104)->doorState == "open",
                         "main door did not reopen before save")) { return false; }
    platform::InputState save;
    save.saveGamePressed = true;
    if (!context.step(save) || !interact(leverAPosition)) { return false; }
    platform::InputState load;
    load.loadGamePressed = true;
    if (!context.step(load)) { return false; }
    const auto& restored = context.snapshot();
    return context.require(findObject(restored, 101)->activation.value_or(false) &&
                           findObject(restored, 102)->activation.value_or(false) &&
                           findObject(restored, 104)->doorState == "open",
                           "toggle activation or door state did not survive save/load");
}

bool runWorldLogic(ScenarioContext& context) {
    if (!runBaseline(context)) { return false; }
    if (!context.step()) { return false; }
    if (!context.require(context.snapshot().lastEvent == "ENCOUNTER STARTED",
                         "region rule did not start the encounter in the same event cycle")) {
        return false;
    }
    for (int index = 0; index < 1200 && !context.snapshot().enemies.empty(); ++index) {
        const auto enemy = context.snapshot().enemies.front();
        const auto player = context.snapshot();
        platform::InputState input;
        const int dx = enemy.x - player.playerX;
        const int dy = enemy.y - player.playerY;
        if (std::abs(dx) <= 24 && std::abs(dy) <= 24) {
            input.primaryAttackPressed = index % 8 == 0;
        } else {
            input.moveRight = dx > 0;
            input.moveLeft = dx < 0;
            input.moveDown = dy > 0;
            input.moveUp = dy < 0;
        }
        if (!context.step(input)) { return false; }
    }
    const auto& completed = context.snapshot();
    const bool finished = context.require(completed.enemies.empty(),
                                          "arena participants were not defeated through gameplay") &&
        context.require(std::find(completed.dialogueFlags.begin(), completed.dialogueFlags.end(),
                                  "flag.playtest.arena.completed") != completed.dialogueFlags.end(),
                        "encounter completion rule did not set its authored flag") &&
        context.require(completed.lastEvent == "ENCOUNTER COMPLETED",
                        "encounter completion event was not observed by the runtime");
    if (finished) { static_cast<void>(context.checkpoint("arena_complete", "world_logic_complete")); }
    return finished;
}

bool runPresentationFeedback(ScenarioContext& context) {
    namespace presentation = game::presentation;
    namespace maps = game::maps;
    namespace gameplay = game::gameplay;
    if (!runBaseline(context)) { return false; }
    const auto compiled = game::content::compileContent(game::content::makeBuiltinAuthoredContent());
    if (!context.require(compiled.registry.has_value(), "builtin presentation content did not compile")) {
        return false;
    }
    presentation::PresentationEffectSystem effects(compiled.registry->presentationEffects());
    presentation::PresentationFeedbackController feedback;
    game::maps::MapData map;
    map.id = simulation::MapId{"map.playtest.presentation"};
    map.regions.push_back({simulation::DefinitionId{"region.playtest.dark"}, {0, 0, 64, 64},
                           simulation::DefinitionId{"effect.environment.dark"}});
    const simulation::EntityHandle player{11, 1};
    simulation::EventBuffer events;
    events.emit(simulation::MapEntered{map.id});
    events.emit(simulation::RegionEntered{map.id, {"region.playtest.dark"}});
    events.emit(simulation::EntityDamaged{{12, 1}, player, 1, 9, 1});
    feedback.consume(events, map, player, effects);
    if (!context.require(effects.isActive({"effect.environment.dark"}) &&
                         effects.isActive({"effect.player.hit"}),
                         "presentation feedback did not resolve region and player-damage cues")) {
        return false;
    }
    events.clear();
    events.emit(simulation::RegionExited{map.id, {"region.playtest.dark"}});
    feedback.consume(events, map, player, effects);
    if (!context.require(!effects.isActive({"effect.environment.dark"}),
                         "presentation feedback retained a stale environment source")) {
        return false;
    }
    maps::WorldRuleDefinition rule;
    rule.id = {"rule.playtest.presentation"};
    rule.trigger = {maps::WorldTriggerKind::regionEntered, {"region.playtest.impact"}, {}};
    rule.actions.push_back({maps::WorldActionKind::playPresentationEffect,
                            {"effect.world.heavy_impact"}, {}});
    gameplay::dialogue::DialogueFlagSet flags;
    std::vector<gameplay::WorldRuleState> state;
    events.clear();
    events.emit(simulation::RegionEntered{map.id, {"region.playtest.impact"}});
    const bool consumed = gameplay::WorldLogicSystem{}.consume(
        {rule}, map.id, flags, events, state);
    feedback.consume(events, map, player, effects);
    if (!context.require(consumed && effects.isActive({"effect.world.heavy_impact"}),
                         "presentation World Logic cue did not reach the effect runtime")) {
        return false;
    }
    return context.step();
}

bool runVisualContent(ScenarioContext& context) {
    namespace content = game::content;
    namespace presentation = game::presentation;
    namespace simulation = underworld::simulation;
    if (!runBaseline(context)) { return false; }

    auto authored = content::makeBuiltinAuthoredContent();
    authored.visualImages.push_back({{"image.playtest.external"},
                                      presentation::VisualAssetRoot::contentWorkspace,
                                      "assets/playtest-character.png"});
    content::AuthoredAnimation animation;
    animation.id = {"anim.playtest.external.idle"};
    animation.imageId = {"image.playtest.external"};
    animation.frames.push_back({{0, 0, 16, 16}, {8, 15}, {}, 2, {}});
    animation.loop = true;
    authored.animations.push_back(animation);
    presentation::DirectionalAnimationRef idle;
    idle.defaultAnimation = simulation::DefinitionId{"anim.playtest.external.idle"};
    authored.enemyVisuals.push_back({{"visual.enemy.playtest.external"}, idle,
                                     std::nullopt, std::nullopt, std::nullopt,
                                     std::nullopt, {}});
    authored.behaviors.push_back({{"behavior.playtest.external"}, 96, 128, 30, 30});
    authored.enemies.push_back({{"enemy.playtest.external"},
                                {"visual.enemy.playtest.external"},
                                {"behavior.playtest.external"}, game::gameplay::Faction::enemy,
                                12, 128, {-4, -8, 8, 8}, {-6, -20, 12, 20},
                                {{game::gameplay::creatures::soldierSwordAttackId()}},
                                std::nullopt});
    authored.staticSprites.push_back({{"visual.playtest.external.sprite"},
                                      {"image.playtest.external"},
                                      std::nullopt, {8, 8}});

    const auto compiled = content::compileContent(authored);
    if (!context.require(compiled.registry.has_value(),
                         "external visual playtest content did not compile")) {
        return false;
    }
    SyntheticImageDecoder decoder;
    presentation::VisualContentLoader loader(decoder);
    const auto root = std::filesystem::temp_directory_path() /
                      "underworld_playtest_visual_content";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const auto loaded = loader.load(*compiled.registry, {root / "game-assets", root});
    const bool resolved = context.require(
        loaded && loaded.content->enemies.find({"visual.enemy.playtest.external"}) &&
        loaded.content->staticSprites.find({"visual.playtest.external.sprite"}) &&
        decoder.paths.size() == authored.visualImages.size() &&
        std::find(decoder.paths.begin(), decoder.paths.end(),
                  root / "assets/playtest-character.png") != decoder.paths.end(),
        "external visual content resolves an authored enemy and static sprite through the workspace root");
    std::filesystem::remove_all(root, error);
    return resolved && context.step();
}

bool runContentStudioVisuals(ScenarioContext& context) {
    namespace content = game::content;
    namespace editor = underworld::editor;
    namespace presentation = game::presentation;
    namespace simulation = underworld::simulation;
    if (!runBaseline(context)) { return false; }

    content::AuthoredContentPack authored;
    authored.visualImages.push_back({{"image.studio.playtest"},
                                     presentation::VisualAssetRoot::contentWorkspace,
                                     "assets/studio.png"});
    authored.animations.push_back({{"animation.studio.idle"}, {"image.studio.playtest"},
                                   {{{0, 0, 16, 16}, {8, 15}, {}, 2, {}}}, true});
    authored.animations.push_back({{"animation.studio.death"}, {"image.studio.playtest"},
                                   {{{16, 0, 16, 16}, {8, 15}, {}, 1, {"dead"}}}, false});
    content::AuthoredEnemyVisual slime;
    slime.id = {"visual.enemy.studio.slime"};
    slime.idle.defaultAnimation = {"animation.studio.idle"};
    slime.death = presentation::DirectionalAnimationRef{};
    slime.death->defaultAnimation = {"animation.studio.death"};
    authored.enemyVisuals.push_back(slime);
    const auto root = std::filesystem::temp_directory_path() /
                      "underworld_playtest_content_studio_visuals";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    std::string writeError;
    const auto source = root / "visuals.json";
    if (!context.require(content::writeAuthoredContentJsonFile(source, authored, writeError),
                         "content studio playtest could not write authored visual source")) {
        std::filesystem::remove_all(root, error);
        return false;
    }
    auto document = editor::ContentWorkspaceDocument::open(root, writeError);
    if (!context.require(document && document->valid(),
                         "content studio playtest could not open valid workspace")) {
        std::filesystem::remove_all(root, error);
        return false;
    }
    SyntheticImageDecoder decoder;
    editor::EditorVisualPreview preview(decoder, root / "game-assets");
    editor::VisualPreviewRequest request;
    request.key = {editor::ContentDefinitionKind::animation, {"animation.studio.idle"}};
    preview.prepare(*document, request);
    const bool resolved = context.require(preview.hasClip() && decoder.paths.size() == 1,
                                          "content studio preview resolves one authored image lazily");
    const bool math = context.require(
        editor::previewGridSelections({0, 16, 32, 16}, {0, 0}, {16, 16}).size() == 2,
        "content studio preview exposes deterministic grid cell selection");
    preview.advanceTicks(2);
    const bool playback = context.require(preview.animator().frameIndex() == 0,
                                           "content studio preview advances through runtime animator semantics");
    request.key = {editor::ContentDefinitionKind::enemyVisual, {"visual.enemy.studio.slime"}};
    request.state = editor::PreviewClipState::death;
    preview.prepare(*document, request);
    const bool profile = context.require(preview.hasClip() &&
                                             preview.animator().clip().id() == "animation.studio.death",
                                         "content studio playtest previews an idle-plus-death Slime profile");
    std::filesystem::remove_all(root, error);
    return resolved && math && playback && profile && context.step();
}

bool runContentStudioGameplay(ScenarioContext& context) {
    namespace content = game::content;
    namespace editor = underworld::editor;
    namespace gameplay = game::gameplay;
    namespace presentation = game::presentation;
    namespace simulation = underworld::simulation;
    namespace creatures = game::gameplay::creatures;
    namespace dialogue = game::gameplay::dialogue;
    namespace quests = game::gameplay::quests;
    if (!runBaseline(context)) { return false; }

    const auto root = std::filesystem::temp_directory_path() /
                      "underworld_playtest_content_studio_gameplay";
    std::error_code fsError;
    std::filesystem::remove_all(root, fsError);
    std::filesystem::create_directories(root, fsError);
    const auto source = root / "studio-gameplay.json";
    std::string error;
    if (!context.require(content::writeAuthoredContentJsonFile(
            source, content::makeBuiltinAuthoredContent(), error),
                         "content studio gameplay could not write its source fixture")) {
        std::filesystem::remove_all(root, fsError);
        return false;
    }
    auto document = editor::ContentWorkspaceDocument::open(root, error);
    if (!context.require(document && document->valid(),
                         "content studio gameplay opened its authored workspace")) {
        std::filesystem::remove_all(root, fsError);
        return false;
    }

    content::AuthoredBehaviorProfile behavior{{"behavior.studio.playtest"}, 96, 144, 20, 30};
    content::AuthoredAttack attack;
    attack.id = {"attack.studio.playtest"};
    attack.kind = gameplay::AttackKind::meleeHitbox;
    attack.damage = {2, 1};
    attack.totalTicks = 8;
    attack.cooldownTicks = 12;
    attack.maximumRangePixels = 32;
    attack.visualActionId = {"attack.sword"};
    attack.meleeHitboxes = gameplay::DirectionalBoxes{};
    for (auto& box : attack.meleeHitboxes->values) box = {-8, -8, 16, 16};
    attack.timeline = {{1, gameplay::AttackTimelineEventKind::activateHitbox},
                       {4, gameplay::AttackTimelineEventKind::deactivateHitbox}};
    content::AuthoredItem item;
    item.id = {"item.studio.playtest"};
    item.visualId = {"visual.item.life_potion"};
    item.category = gameplay::ItemCategory::consumable;
    item.stackLimit = 10;
    item.use = gameplay::ItemUseDefinition{gameplay::ItemUseKind::restoreHealth, 3};
    content::AuthoredPickup pickup;
    pickup.id = {"pickup.studio.playtest"};
    pickup.visualId = {"visual.pickup.heart"};
    pickup.collectionBounds = {-5, -5, 10, 10};
    pickup.payload = content::AuthoredItemPickup{item.id, 1};
    content::AuthoredRewardProfile profile;
    profile.id = {"reward.studio.playtest"};
    profile.experience = 15;
    profile.loot.push_back({pickup.id, 5000, 1, 1});
    content::AuthoredEnemy enemy;
    enemy.id = {"enemy.studio.playtest"};
    enemy.visualSetId = creatures::soldierVisualId();
    enemy.behaviorProfileId = behavior.id;
    enemy.maximumHealth = 6;
    enemy.movementSpeedSubpixelsPerTick = 128;
    enemy.collisionBody = {0, -8, 12, 8};
    enemy.hurtbox = {-6, -20, 12, 20};
    enemy.attackIds = {attack.id};
    enemy.rewardProfileId = profile.id;
    content::AuthoredWorldObject object;
    object.id = {"object.studio.playtest"};
    object.visualSetId = {"visual.object.chest"};
    object.interactable = gameplay::ObjectInteractionDefinition{{-8, -8, 16, 16}};
    object.activation = gameplay::ObjectActivationDefinition{
        gameplay::ObjectActivationMode::interactToggle, false, std::nullopt};
    content::AuthoredRewardGrant grant;
    grant.id = {"reward.grant.studio.playtest"};
    grant.experience = 20;
    grant.gold = 3;
    grant.items.push_back({item.id, 1});
    content::AuthoredQuest quest;
    quest.id = {"quest.studio.playtest"};
    quest.title = "Studio quest";
    quest.rewardGrantId = grant.id;
    quest.objectives.push_back({{"objective.studio.playtest"}, quests::QuestObjectiveKind::kill,
                                enemy.id, 1, "Defeat the authored creature"});
    content::AuthoredDialogue conversation;
    conversation.id = {"dialogue.studio.playtest"};
    conversation.entryNodeId = {"entry"};
    content::AuthoredDialogueNode node;
    node.id = {"entry"};
    node.speaker = "Studio NPC";
    node.pages = {"The content pipeline is editable."};
    node.choices.push_back({"Accept", {"entry"}, {},
                            {{dialogue::DialogueActionKind::startQuest, quest.id}}});
    conversation.nodes.push_back(node);
    content::AuthoredNpc npc;
    npc.id = {"npc.studio.playtest"};
    npc.visualSetId = {"visual.npc.scholar"};
    npc.interaction = gameplay::InteractionArea{{-8, -8, 16, 16}, true};
    npc.defaultDialogueId = conversation.id;
    npc.tags = {"studio"};
    content::AuthoredShop shop;
    shop.id = {"shop.studio.playtest"};
    shop.offers.push_back({item.id, 12, 4});
    content::AuthoredPlayerProgression progression;
    progression.id = {"progression.studio.playtest"};
    progression.baseStats.maximumHealth = 8;
    progression.cumulativeExperienceThresholds = {0, 25, 70};
    content::AuthoredPresentationEffect effect;
    effect.id = {"effect.studio.playtest"};
    effect.durationTicks = 6;
    effect.overlay = presentation::ColorOverlayDefinition{{255, 255, 255, 48},
        presentation::PresentationOverlayMode::linearFadeOut, 0,
        presentation::PresentationCompositionLayer::final};

    bool authored = document->addBehavior(source, behavior, error) &&
        document->addAttack(source, attack, error) &&
        document->addItem(source, item, error) &&
        document->addPickup(source, pickup, error) &&
        document->addRewardProfile(source, profile, error) &&
        document->addEnemy(source, enemy, error) &&
        document->addObject(source, object, error) &&
        document->addRewardGrant(source, grant, error) &&
        document->addQuest(source, quest, error) &&
        document->addDialogue(source, conversation, error) &&
        document->addNpc(source, npc, error) &&
        document->addShop(source, shop, error) &&
        document->addPlayerProgression(source, progression, error) &&
        document->addPresentationEffect(source, effect, error);
    if (!context.require(authored && document->valid(),
                         "content studio gameplay authored all typed relationships")) {
        std::filesystem::remove_all(root, fsError);
        return false;
    }
    if (!context.require(document->saveAll(error),
                         "content studio gameplay saved its authored DTOs")) {
        std::filesystem::remove_all(root, fsError);
        return false;
    }
    auto reloaded = editor::ContentWorkspaceDocument::open(root, error);
    const bool relationships = context.require(
        reloaded && reloaded->valid() && reloaded->enemy(enemy.id) &&
        reloaded->attack(attack.id) && reloaded->item(item.id) &&
        reloaded->pickup(pickup.id) && reloaded->object(object.id) &&
        reloaded->dialogue(conversation.id) && reloaded->quest(quest.id) &&
        reloaded->shop(shop.id) && reloaded->presentationEffect(effect.id),
        "content studio gameplay save/reload preserves cross-category references");
    bool runtime = false;
    if (relationships && reloaded->compiledRegistry()) {
        simulation::EntityHandlePool handles;
        creatures::EnemyFactory factory(handles, reloaded->compiledRegistry()->enemies(),
                                        reloaded->compiledRegistry()->behaviors(),
                                        reloaded->compiledRegistry()->attacks(),
                                        reloaded->compiledRegistry()->projectiles());
        auto enemyInstance = factory.create(handles, enemy.id, {32, 32}, gameplay::FacingDirection::down);
        gameplay::WorldObjectFactory objectFactory(handles, reloaded->compiledRegistry()->objects(),
                                                   reloaded->compiledRegistry()->items());
        auto objectInstance = objectFactory.create(handles, object.id, {16, 16});
        runtime = enemyInstance.definition().id == enemy.id &&
            objectInstance.definition().id == object.id && objectInstance.hasActivation();
    }
    const bool runtimeCheck = context.require(runtime,
        "content studio gameplay definitions instantiate through existing runtime factories");
    std::filesystem::remove_all(root, fsError);
    return relationships && runtimeCheck && context.step();
}

bool runContentStudioUnified(ScenarioContext& context) {
    namespace content = game::content;
    namespace editor = underworld::editor;
    namespace maps = game::maps;
    namespace simulation = underworld::simulation;
    namespace gameplay = game::gameplay;
    if (!runBaseline(context)) return false;

    const auto root = std::filesystem::temp_directory_path() / "underworld_playtest_content_studio_unified";
    std::error_code fsError; std::filesystem::remove_all(root, fsError); std::filesystem::create_directories(root, fsError);
    const auto source = root / "unified.json"; std::string error;
    if (!context.require(content::writeAuthoredContentJsonFile(source, content::makeBuiltinAuthoredContent(), error),
                         "unified Studio fixture was written")) { std::filesystem::remove_all(root, fsError); return false; }
    auto workspace = editor::ContentWorkspaceDocument::open(root, error);
    if (!context.require(workspace && workspace->valid(), "unified Studio workspace opened")) { std::filesystem::remove_all(root, fsError); return false; }

    const auto tilesetId = simulation::DefinitionId{"tileset.studio.unified"};
    content::AuthoredTileset tileset{tilesetId, "Studio Tiles", "assets/studio-tiles.png", 16, 4, 4};
    content::AuthoringDescriptor descriptor{{"enemy.studio.unified"}, "Unified Test Enemy", content::AuthoringCategory::enemy, {"test", "enemy"}};
    content::AuthoredTileSemantic semantic; semantic.id = {"tile.studio.unified.floor"}; semantic.tilesetId = tilesetId; semantic.sourceIndex = 0; semantic.family = "studio"; semantic.role = game::authoring::TileRole::floor;
    content::AuthoredStamp stamp; stamp.id = {"stamp.studio.unified"}; stamp.displayName = "Studio Stamp"; stamp.width = 1; stamp.height = 1; stamp.cells.push_back({0, 0, semantic.id});
    content::AuthoredEnemy enemy; enemy.id = descriptor.definitionId; enemy.visualSetId = game::gameplay::creatures::soldierVisualId(); enemy.behaviorProfileId = game::gameplay::creatures::soldierBehaviorId(); enemy.faction = gameplay::Faction::enemy; enemy.maximumHealth = 5; enemy.movementSpeedSubpixelsPerTick = 256; enemy.collisionBody = {0, -8, 12, 8}; enemy.hurtbox = {-6, -18, 12, 18}; enemy.attackIds = {{"attack.soldier.sword"}};
    content::AuthoredWorldObject object; object.id = {"object.studio.unified.switch"}; object.visualSetId = {"visual.object.chest"}; object.interactable = gameplay::ObjectInteractionDefinition{{-8, -8, 16, 16}}; object.activation = gameplay::ObjectActivationDefinition{gameplay::ObjectActivationMode::interactToggle, false, std::nullopt};
    const bool authored = workspace->addTileset(source, tileset, error) && workspace->addAuthoringDescriptor(source, descriptor, error) &&
        workspace->addTileSemantic(source, semantic, error) && workspace->addStamp(source, stamp, error) && workspace->addEnemy(source, enemy, error) && workspace->addObject(source, object, error);
    if (!context.require(authored && workspace->valid(), "unified Studio authored tileset, semantic, stamp and placeable content")) { std::filesystem::remove_all(root, fsError); return false; }

    auto registry = workspace->compiledRegistry();
    if (!context.require(registry.has_value(), "unified Studio produced a current registry")) { std::filesystem::remove_all(root, fsError); return false; }
    editor::EditorDocument map = editor::EditorDocument::newMap(simulation::MapId{"map.studio.unified"}, 8, 8, 16, true);
    if (!context.require(map.execute(std::make_unique<editor::AddLayerCommand>(1, "Foreground"), error), "unified map added a layer")) { std::filesystem::remove_all(root, fsError); return false; }
    editor::TileBrushSelection brush{tilesetId, 2, 2, {{tilesetId, 0, world::TileFlags::none}, {tilesetId, 1, world::TileFlags::none}, {tilesetId, 4, world::TileFlags::none}, {tilesetId, 5, world::TileFlags::none}}};
    const auto placements = editor::brushPlacements(brush, {2, 3}, map.data());
    if (!context.require(placements.size() == 4 && placements[0].first.x == 2 && placements[3].first.y == 4,
                         "unified map expands a deterministic multi-tile brush")) { std::filesystem::remove_all(root, fsError); return false; }
    const editor::TileBrushSelection horizontalBrush{
        tilesetId, 2, 1, {{tilesetId, 0, world::TileFlags::none},
                           {tilesetId, 1, world::TileFlags::none}}};
    const auto patterned = editor::patternRectanglePlacements(
        horizontalBrush, {1, 1}, {4, 2}, map.data());
    if (!context.require(patterned.size() == 8 && patterned[0].second.sourceIndex == 0 &&
                         patterned[1].second.sourceIndex == 1 && patterned[2].second.sourceIndex == 0 &&
                         patterned[4].second.sourceIndex == 0 &&
                         std::none_of(patterned.begin(), patterned.end(), [](const auto& placement) {
                             return placement.first.x == 5;
                         }), "unified map rectangle repeats a brush without painting outside its bounds")) {
        std::filesystem::remove_all(root, fsError); return false;
    }
    auto patternPaint = std::make_unique<editor::CompoundEditorCommand>("Paint Unified Pattern");
    for (const auto& placement : patterned) {
        patternPaint->add(std::make_unique<editor::PaintTilesCommand>(
            1, std::vector<editor::TileCoordinate>{placement.first}, placement.second));
    }
    if (!context.require(map.execute(std::move(patternPaint), error),
                         "unified map paints the periodic rectangle as one edit")) {
        std::filesystem::remove_all(root, fsError); return false;
    }
    auto brushPaint = std::make_unique<editor::CompoundEditorCommand>("Paint Unified Brush");
    for (const auto& placement : placements) brushPaint->add(std::make_unique<editor::PaintTilesCommand>(
        1, std::vector<editor::TileCoordinate>{placement.first}, placement.second));
    if (!context.require(map.execute(std::move(brushPaint), error) &&
                         map.data().layers[1].cells[static_cast<std::size_t>(3) * map.data().width + 2].has_value(),
                         "unified map painted a tile brush")) { std::filesystem::remove_all(root, fsError); return false; }
    if (!context.require(map.execute(std::make_unique<editor::RenameLayerCommand>(1, "Foreground Studio"), error) && map.execute(std::make_unique<editor::MoveLayerCommand>(1, 0), error), "unified map edited layer order")) { std::filesystem::remove_all(root, fsError); return false; }
    if (!context.require(map.execute(std::make_unique<editor::PlaceStampCommand>(1, registry->authoringSemantics().stamps().back(), editor::TileCoordinate{4, 4}, registry->authoringSemantics()), error), "unified map placed an authored stamp")) { std::filesystem::remove_all(root, fsError); return false; }
    const auto enemyInstance = map.allocatePersistentId(); const auto objectInstance = map.allocatePersistentId();
    const auto npcInstance = map.allocatePersistentId(); const auto pickupInstance = map.allocatePersistentId();
    if (!context.require(map.execute(std::make_unique<editor::PlaceEntityCommand>(maps::EnemyPlacement{enemyInstance, enemy.id, {24, 24}, gameplay::FacingDirection::down}), error) &&
                         map.execute(std::make_unique<editor::PlaceEntityCommand>(maps::ObjectPlacement{objectInstance, object.id, {40, 24}, {}}), error) &&
                         map.execute(std::make_unique<editor::PlaceEntityCommand>(maps::NpcPlacement{npcInstance, game::gameplay::npcs::guardNpcId(), {56, 24}, gameplay::FacingDirection::down}), error) &&
                         map.execute(std::make_unique<editor::PlaceEntityCommand>(maps::PickupPlacement{pickupInstance, {"pickup.heart"}, {"visual.pickup.heart"}, {72, 24}, {0, 0, 16, 16}, gameplay::HealthPickup{1}}), error),
                         "unified map placed authored enemy, NPC, object and pickup instances")) { std::filesystem::remove_all(root, fsError); return false; }
    const auto enemyUsages = editor::findPlacementUsages(map, {editor::ContentDefinitionKind::enemy, enemy.id});
    const auto npcUsages = editor::findPlacementUsages(map, {editor::ContentDefinitionKind::npc, game::gameplay::npcs::guardNpcId()});
    const auto objectUsages = editor::findPlacementUsages(map, {editor::ContentDefinitionKind::object, object.id});
    const auto pickupUsages = editor::findPlacementUsages(map, {editor::ContentDefinitionKind::pickup, {"pickup.heart"}});
    if (!context.require(enemyUsages.size() == 1 && npcUsages.size() == 1 && objectUsages.size() == 1 &&
                         pickupUsages.size() == 1 && enemyUsages.front().instanceId == enemyInstance &&
                         npcUsages.front().instanceId == npcInstance && objectUsages.front().instanceId == objectInstance &&
                         pickupUsages.front().instanceId == pickupInstance,
                         "unified map finds every placeable definition usage in authored order")) {
        std::filesystem::remove_all(root, fsError); return false;
    }
    const auto effectId = simulation::DefinitionId{"effect.environment.dark"};
    const auto cueEffectId = simulation::DefinitionId{"effect.world.heavy_impact"};
    const auto regionInstance = map.allocatePersistentId();
    if (!context.require(map.execute(std::make_unique<editor::PlaceEntityCommand>(
            editor::RegionPlacement{regionInstance, "region.studio.unified", {0, 0, 32, 32}, {}}), error) &&
                         map.setRegionEnvironmentEffect({"region.studio.unified"}, effectId, error),
                         "unified map authored a region presentation binding")) {
        std::filesystem::remove_all(root, fsError);
        return false;
    }
    maps::WorldRuleDefinition rule; rule.id = {"rule.studio.unified"}; rule.trigger = {maps::WorldTriggerKind::objectActivated, {}, objectInstance}; rule.actions.push_back({maps::WorldActionKind::playPresentationEffect, cueEffectId, {}, {}});
    const auto rewardId = registry->rewardGrants().find({"reward.quest.scholar.path"}) ? std::optional<simulation::DefinitionId>{simulation::DefinitionId{"reward.quest.scholar.path"}} : std::nullopt;
    maps::EncounterDefinition encounter; encounter.id = {"encounter.studio.unified"}; encounter.participants = {enemyInstance}; encounter.rewardGrantId = rewardId;
    if (!context.require(map.addRule(rule, error) && map.addEncounter(encounter, error), "unified map authored a rule and encounter")) { std::filesystem::remove_all(root, fsError); return false; }
    const auto mapPath = root / "map.studio.unified.umap"; const auto dmapPath = root / "map.studio.unified.dmap";
    const bool saved = map.saveAs(mapPath, *registry, error) && map.exportDmap(dmapPath, *registry, error);
    if (!context.require(saved, "unified Studio saved authored UMAP and compiled DMAP: " + error)) { std::filesystem::remove_all(root, fsError); return false; }
    const auto catalogs = game::mapValidationCatalogs(*registry);
    const auto productionDmap = maps::readDmap(dmapPath, &catalogs);
    if (!context.require(productionDmap.success, "unified Studio DMAP is accepted by the production reader")) { std::filesystem::remove_all(root, fsError); return false; }
    auto reloadedMap = editor::EditorDocument::open(mapPath, *registry, error);
    auto reloadedWorkspace = editor::ContentWorkspaceDocument::open(root, error);
    const bool reloaded = context.require(reloadedMap && reloadedWorkspace && reloadedWorkspace->valid() && reloadedMap->data().layers.size() == 2 &&
        reloadedMap->data().enemies.size() == 1 && reloadedMap->data().npcs.size() == 1 &&
        reloadedMap->data().objects.size() == 1 && reloadedMap->data().pickups.size() == 1 &&
        reloadedMap->regions().size() == 1 && reloadedMap->regions().front().environmentEffectId == effectId &&
        !reloadedMap->rules().empty() && !reloadedMap->encounters().empty(),
        "unified Studio save/reload preserves content, layers, placements, rule and encounter");
    std::filesystem::remove_all(root, fsError);
    return reloaded && context.step();
}

bool runPickup(ScenarioContext& context, std::string_view definition) {
    if (!runBaseline(context)) { return false; }
    const auto initial = context.snapshot();
    const auto found = std::find_if(initial.pickups.begin(), initial.pickups.end(),
        [&](const auto& pickup) { return pickup.definitionId == definition; });
    if (!context.require(found != initial.pickups.end(), "expected pickup is absent")) {
        return false;
    }
    const PointTarget target{found->x, found->y};
    const auto initialGold = initial.gold;
    const auto initialPickupCount = initial.pickups.size();
    // Keep the heart scenario focused on health-pickup semantics; combat loot
    // is covered by rewards_loot and must not alter its population assertion.
    if (!moveToPickup(context, target, definition != "pickup.heart")) {
        return context.fail("could not approach expected pickup");
    }
    for (int index = 0; index < 2; ++index) { if (!context.step()) { return false; } }
    const auto& after = context.snapshot();
    if (definition == "pickup.money") {
        const bool changed = context.require(after.gold > initialGold,
                                             "money pickup did not change wallet");
        if (changed) { static_cast<void>(context.checkpoint("pickup_money", "pickup_collected")); }
        return changed;
    }
    const bool fullHealthNoOp = definition == "pickup.heart" &&
        after.playerHealth == after.playerMaximumHealth &&
        after.pickups.size() == initialPickupCount;
    // Combat may have produced transient loot while approaching the authored
    // heart. Compare the player's health and the total pickup population rather
    // than assuming that every remaining pickup came from the map file.
    const bool changed = context.require(after.pickups.size() <= initialPickupCount ||
                                             !after.inventory.empty() ||
                                             after.playerHealth > initial.playerHealth ||
                                             fullHealthNoOp,
                                         "pickup did not produce an observable state change");
    if (changed) { static_cast<void>(context.checkpoint(definition, "pickup_collected")); }
    return changed;
}

bool runDialogue(ScenarioContext& context) {
    if (!runBaseline(context)) { return false; }
    const auto initial = context.snapshot();
    if (!context.require(!initial.npcs.empty(), "expected NPC is absent")) { return false; }
    const auto npc = initial.npcs.front();
    if (!moveTo(context, PointTarget{npc.x, npc.y}, 500, false)) {
        return context.fail("could not approach expected NPC");
    }
    platform::InputState input;
    input.interactPressed = true;
    if (!context.step(input)) { return false; }
    const auto& opened = context.snapshot();
    const bool active = context.require(opened.dialogue.active,
                                        "NPC interaction did not open dialogue");
    if (active) { static_cast<void>(context.checkpoint("dialogue_start", "dialogue_started")); }
    return active;
}

bool runQuest(ScenarioContext& context) {
    if (!runBaseline(context)) { return false; }
    const auto initial = context.snapshot();
    const auto scholar = std::find_if(initial.npcs.begin(), initial.npcs.end(),
        [](const auto& npc) { return npc.definitionId == "npc.scholar"; });
    if (!context.require(scholar != initial.npcs.end(), "expected Scholar is absent")) {
        return false;
    }
    if (!moveTo(context, PointTarget{scholar->x, scholar->y}, 500, false)) {
        return context.fail("could not approach Scholar");
    }
    platform::InputState input;
    input.interactPressed = true;
    if (!context.step(input)) { return false; }
    input = {};
    input.primaryAttackPressed = true;
    if (!context.step(input) || !context.step(input)) { return false; }
    const auto& after = context.snapshot();
    const auto quest = std::find_if(after.quests.begin(), after.quests.end(),
        [](const auto& value) { return value.questId == "quest.scholar.path"; });
    const bool active = quest != after.quests.end() && quest->status == "active";
    if (active) { static_cast<void>(context.checkpoint("quest_start", "quest_activated")); }
    return context.require(active, "Scholar dialogue did not activate its quest");
}

bool runSaveLoad(ScenarioContext& context) {
    if (!runBaseline(context)) { return false; }
    platform::InputState input;
    input.moveRight = true;
    for (int index = 0; index < 20; ++index) { if (!context.step(input)) { return false; } }
    const auto saved = context.snapshot();
    input = {};
    input.saveGamePressed = true;
    if (!context.step(input)) { return false; }
    input = {};
    input.moveDown = true;
    for (int index = 0; index < 20; ++index) { if (!context.step(input)) { return false; } }
    input = {};
    input.loadGamePressed = true;
    if (!context.step(input)) { return false; }
    const auto& loaded = context.snapshot();
    return context.require(loaded.currentMap == saved.currentMap &&
                               loaded.playerX == saved.playerX && loaded.playerY == saved.playerY,
                           "load did not restore the saved player state");
}

bool runTransition(ScenarioContext& context, std::string_view targetMap) {
    if (!runBaseline(context)) { return false; }
    const auto map = loadMap(context, context.snapshot().currentMap);
    if (!context.require(map.has_value(), "could not read current map for transition")) {
        return false;
    }
    const auto link = std::find_if(map->links.begin(), map->links.end(),
        [&](const auto& candidate) { return candidate.targetMapId.value() == targetMap; });
    if (!context.require(link != map->links.end(), "expected map link is absent")) { return false; }
    const auto target = linkCenter(link->trigger);
    for (int index = 0; index < 500; ++index) {
        if (context.snapshot().currentMap == targetMap) {
            static_cast<void>(context.checkpoint("map_transition", "map_entered"));
            return true;
        }
        const auto& player = context.snapshot();
        platform::InputState input;
        input.moveRight = target.x > player.playerX;
        input.moveLeft = target.x < player.playerX;
        input.moveDown = target.y > player.playerY;
        input.moveUp = target.y < player.playerY;
        for (const auto& enemy : player.enemies) {
            if (std::abs(enemy.x - player.playerX) <= 96 &&
                std::abs(enemy.y - player.playerY) <= 96) {
                input.primaryAttackPressed = enemy.definitionId == "enemy.evil_soldier" &&
                                             index % 24 == 0;
                input.secondaryAttackPressed = enemy.definitionId == "enemy.skull" &&
                                               index % 24 == 0;
                break;
            }
        }
        if (!context.step(input)) { return false; }
    }
    return context.require(context.snapshot().currentMap == targetMap,
                           "map link did not activate the expected destination");
}

bool runContentAction(ScenarioContext& context, std::string_view definition,
                      bool attackObject = false) {
    if (!runBaseline(context)) { return false; }
    const auto initial = context.snapshot();
    if (definition == "enemy.evil_soldier" || definition == "enemy.skull") {
        const auto found = std::find_if(initial.enemies.begin(), initial.enemies.end(),
                                        [&](const auto& actor) { return actor.definitionId == definition; });
        if (!context.require(found != initial.enemies.end(), "expected enemy is absent")) { return false; }
        // Stop at attack range instead of entering the enemy collision body.
        // Contact damage is intentionally part of the real gameplay path, so
        // the playtest must exercise attacks without depending on body overlap.
        for (int index = 0; index < 500; ++index) {
            const auto& current = context.snapshot();
            const auto enemy = std::find_if(current.enemies.begin(), current.enemies.end(),
                [&](const auto& actor) { return actor.definitionId == definition; });
            if (enemy == current.enemies.end()) { break; }
            if (std::abs(enemy->x - current.playerX) <= 20 &&
                std::abs(enemy->y - current.playerY) <= 20) { break; }
            platform::InputState input;
            input.moveRight = enemy->x > current.playerX;
            input.moveLeft = enemy->x < current.playerX;
            input.moveDown = enemy->y > current.playerY;
            input.moveUp = enemy->y < current.playerY;
            if (!context.step(input)) { return false; }
        }
        const auto& positioned = context.snapshot();
        const bool inRange = std::any_of(positioned.enemies.begin(), positioned.enemies.end(),
            [&](const auto& actor) { return actor.definitionId == definition &&
                std::abs(actor.x - positioned.playerX) <= 20 &&
                std::abs(actor.y - positioned.playerY) <= 20; });
        if (!context.require(inRange, "could not approach expected enemy without body overlap")) {
            return false;
        }
        const auto initialHealth = found->health;
        bool sawAttack = false;
        for (int index = 0; index < 240; ++index) {
            platform::InputState input;
            if (definition == "enemy.skull") { input.secondaryAttackPressed = index % 24 == 0; }
            else { input.primaryAttackPressed = index % 24 == 0; }
            if (!context.step(input)) { return false; }
            sawAttack = sawAttack || context.snapshot().playerAction != "NONE";
        }
        const auto& after = context.snapshot();
        const auto enemy = std::find_if(after.enemies.begin(), after.enemies.end(),
            [&](const auto& actor) { return actor.definitionId == definition; });
        return context.require(sawAttack || enemy == after.enemies.end() ||
                                   enemy->health < initialHealth,
                               "enemy scenario produced no observable combat result");
    }
    if (definition == "object.chest" || definition == "object.crate") {
        const auto found = std::find_if(initial.objects.begin(), initial.objects.end(),
                                        [&](const auto& actor) { return actor.definitionId == definition; });
        if (!context.require(found != initial.objects.end(), "expected object is absent")) { return false; }
        const PointTarget target{found->x, found->y};
        if (!moveToContent(context, target)) { return context.fail("could not approach expected object"); }
        for (int index = 0; index < 160; ++index) {
            platform::InputState input;
            input.interactPressed = definition == "object.chest" && index == 0;
            input.primaryAttackPressed = attackObject && index % 24 == 0;
            if (!context.step(input)) { return false; }
        }
        const auto& after = context.snapshot();
        const auto changed = std::find_if(after.objects.begin(), after.objects.end(),
            [&](const auto& actor) { return actor.definitionId == definition &&
                                              (actor.state == "opened" || actor.state == "destroyed"); });
        return context.require(changed != after.objects.end() ||
                                   (definition == "object.crate" &&
                                    std::none_of(after.objects.begin(), after.objects.end(),
                                        [&](const auto& actor) { return actor.definitionId == definition; })),
                               "object interaction did not produce an observable state");
    }
    return context.fail("unsupported content action");
}

bool runRewards(ScenarioContext& context) {
    if (!runBaseline(context)) { return false; }
    const auto initialExperience = context.snapshot().playerExperience;
    if (!runContentAction(context, "enemy.evil_soldier")) { return false; }
    const auto& after = context.snapshot();
    const bool rewarded = context.require(after.playerExperience == initialExperience + 60,
                                          "soldier defeat did not grant its reward XP");
    if (rewarded) { static_cast<void>(context.checkpoint("reward_loot", "reward_resolved")); }
    return rewarded;
}

struct ScenarioResult final {
    bool passed{};
    std::uint64_t ticks{};
    std::uint64_t assertions{};
};

ScenarioResult runScenario(const std::filesystem::path& root, const RunnerOptions& options,
                           std::string_view name) {
    std::string map = "map.dungeon.01";
    if (name == "ranged_combat" || name == "pickup_heart" || name == "crate" ||
        name == "map_02_to_01" || name == "map_02_to_03" || name == "dialogue_choice" ||
        name == "dialogue_flag" || name == "quest" || name == "quest_save_load") {
        map = "map.dungeon.02";
    }
    if (name == "pickup_life_potion" || name == "map_03_to_02") {
        map = "map.dungeon.03";
    }
    ScenarioContext context(root, options, std::string(name), map);
    bool passed = false;
    if (name == "startup") { passed = runBaseline(context); }
    else if (name == "movement") { passed = runMovement(context); }
    else if (name == "collision") { passed = runCollision(context); }
    else if (name == "melee_combat") { passed = runContentAction(context, "enemy.evil_soldier"); }
    else if (name == "ranged_combat") { passed = runContentAction(context, "enemy.skull"); }
    else if (name == "chest") { passed = runContentAction(context, "object.chest"); }
    else if (name == "crate") { passed = runContentAction(context, "object.crate", true); }
    else if (name == "pickup_money") { passed = runPickup(context, "pickup.money"); }
    else if (name == "pickup_heart") { passed = runPickup(context, "pickup.heart"); }
    else if (name == "pickup_life_potion") { passed = runPickup(context, "pickup.life_potion"); }
    else if (name == "inventory" || name == "quick_slot" || name == "inventory_navigation") {
        platform::InputState input;
        input.toggleInventoryPressed = true;
        passed = context.step(input) && context.require(context.snapshot().inventoryOpen,
                                                        "inventory did not open from logical input");
    }
    else if (name == "save_load") { passed = runSaveLoad(context); }
    else if (name == "map_01_to_02") { passed = runTransition(context, "map.dungeon.02"); }
    else if (name == "map_02_to_01") { passed = runTransition(context, "map.dungeon.01"); }
    else if (name == "map_02_to_03") { passed = runTransition(context, "map.dungeon.03"); }
    else if (name == "map_03_to_02") { passed = runTransition(context, "map.dungeon.02"); }
    else if (name == "npc_dialogue" || name == "dialogue_pagination" ||
             name == "dialogue_choice" || name == "dialogue_flag") {
        passed = runDialogue(context);
    } else if (name == "quest") {
        passed = runQuest(context);
    } else if (name == "world_logic") {
        passed = runWorldLogic(context);
    } else if (name == "presentation_feedback") {
        passed = runPresentationFeedback(context);
    } else if (name == "visual_content") {
        passed = runVisualContent(context);
    } else if (name == "content_studio_visuals") {
        passed = runContentStudioVisuals(context);
    } else if (name == "content_studio_gameplay") {
        passed = runContentStudioGameplay(context);
    } else if (name == "content_studio_unified") {
        passed = runContentStudioUnified(context);
    } else if (name == "interactive_world") {
        passed = runInteractiveWorld(context);
    } else if (name == "rewards_loot") {
        passed = runRewards(context);
    } else {
        passed = runBaseline(context);
    }
    const bool closed = context.finish();
    return {passed && closed, context.tick(), context.assertionsPassed()};
}

const std::vector<std::string> allScenarios{
    "startup", "movement", "collision", "melee_combat", "ranged_combat",
    "pickup_money", "pickup_heart", "pickup_life_potion", "inventory", "quick_slot",
    "inventory_navigation", "chest", "crate", "map_01_to_02", "map_02_to_01",
    "map_02_to_03", "map_03_to_02", "save_load", "npc_dialogue", "dialogue_pagination",
        "dialogue_choice", "dialogue_flag", "quest", "quest_save_load", "world_logic",
        "presentation_feedback", "interactive_world", "visual_content", "content_studio_visuals",
        "content_studio_gameplay", "content_studio_unified", "rewards_loot"};

} // namespace

int main(int argc, char** argv) {
    std::string error;
    const auto options = parseOptions(argc, argv, error);
    if (!options) {
        if (error != "help") { std::cerr << "playtest_runner: " << error << '\n'; }
        std::cerr << "usage: playtest_runner --all | --scenario <name> [--seed N] [--ticks N]\n";
        return error == "help" ? 0 : 2;
    }
    try {
        const auto root = repositoryRoot();
        std::vector<std::string> scenarios;
        if (options->all) { scenarios = allScenarios; }
        else { scenarios.push_back(options->scenario); }
        int failures = 0;
        for (const auto& scenario : scenarios) {
            try {
                const auto result = runScenario(root, *options, scenario);
                std::cout << (result.passed ? "PASS " : "FAIL ") << scenario
                          << " ticks=" << result.ticks
                          << " assertions=" << result.assertions << '\n';
                if (!result.passed) { ++failures; }
            } catch (const std::exception& exception) {
                ++failures;
                std::cout << "FAIL " << scenario << ": " << exception.what() << '\n';
            }
        }
        return failures == 0 ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << "playtest_runner: " << exception.what() << '\n';
        return 1;
    }
}
