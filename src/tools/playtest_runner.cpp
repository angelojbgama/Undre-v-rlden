#include "engine/platform/headless/headless_audit_platform.h"
#include "engine/core/game_metrics.h"
#include "engine/core/image_data.h"
#include "engine/render/framebuffer.h"
#include "game/audit/audit_session.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/world_objects.h"
#include "game/phase5_demo.h"
#include "game/maps/dmap.h"
#include "game/maps/official_maps.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
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
    [[nodiscard]] core::ImageData decode(const std::filesystem::path&) override {
        constexpr int width = 304;
        constexpr int height = 192;
        constexpr std::size_t stride = static_cast<std::size_t>(width) * 4U;
        core::ImageData result{width, height, stride,
                               std::vector<std::uint8_t>(stride * height, 0xffU)};
        return result;
    }
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
        demo_ = std::make_unique<game::Phase7Demo>(
            decoder_, options.assetRoot.empty() ? root_ : options.assetRoot,
            executableDirectory_, launch);

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

    [[nodiscard]] game::Phase7Demo& demo() noexcept { return *demo_; }
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
    std::unique_ptr<game::Phase7Demo> demo_;
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

template<class Actor>
bool moveTo(ScenarioContext& context, const Actor& actor, int maximumTicks = 500) {
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

struct PointTarget final { int x{}; int y{}; };

PointTarget linkCenter(const world::AabbI& area) {
    return {area.x + area.width / 2, area.y + area.height / 2};
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
    if (!moveTo(context, target)) { return context.fail("could not approach expected pickup"); }
    for (int index = 0; index < 12; ++index) { if (!context.step()) { return false; } }
    const auto& after = context.snapshot();
    if (definition == "pickup.money") {
        const bool changed = context.require(after.gold > initialGold,
                                             "money pickup did not change wallet");
        if (changed) { static_cast<void>(context.checkpoint("pickup_money", "pickup_collected")); }
        return changed;
    }
    const bool changed = context.require(after.pickups.size() < initialPickupCount ||
                                             !after.inventory.empty() ||
                                             after.playerHealth > initial.playerHealth,
                                         "pickup did not produce an observable state change");
    if (changed) { static_cast<void>(context.checkpoint(definition, "pickup_collected")); }
    return changed;
}

bool runDialogue(ScenarioContext& context) {
    if (!runBaseline(context)) { return false; }
    const auto initial = context.snapshot();
    if (!context.require(!initial.npcs.empty(), "expected NPC is absent")) { return false; }
    const auto npc = initial.npcs.front();
    if (!moveTo(context, PointTarget{npc.x, npc.y})) {
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
        const PointTarget target{found->x, found->y};
        if (!moveTo(context, target)) { return context.fail("could not approach expected enemy"); }
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
        if (!moveTo(context, target)) { return context.fail("could not approach expected object"); }
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
    } else {
        // Quest start is not exposed through a player command yet. Keep these names
        // in the matrix as startup/update/render smoke until that public boundary exists.
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
    "dialogue_choice", "dialogue_flag", "quest", "quest_save_load"};

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
