#include "game/game.h"

#include "engine/core/fixed_timestep.h"
#include "engine/core/game_metrics.h"
#include "engine/platform/platform.h"
#include "engine/render/framebuffer.h"
#include "game/audit/audit_session.h"
#include "game/phase5_demo.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

namespace underworld::game {

namespace {

constexpr core::FixedStepConfig fixedStepConfig{
    core::GameMetrics::fixedDt,
    0.25, // Never accept more than 250 ms from one rendered frame.
    5,    // At most 83.3 ms of simulation work is recovered per frame.
};

class ManualAuditObserver final {
public:
    [[nodiscard]] bool open(const audit::GameAuditSnapshot& initial, std::string& error) {
        audit::AuditSessionConfig config;
        config.outputRoot = "audit";
        config.metadata.platform = "win32";
        config.metadata.mode = "manual";
        config.metadata.scenario = "manual_gameplay";
        config.metadata.initialMap = initial.currentMap;
        config.metadata.initialSpawn = initial.currentSpawn;
        config.metadata.logicalWidth = core::GameMetrics::logicalWidth;
        config.metadata.logicalHeight = core::GameMetrics::logicalHeight;
        if (!session_.open(config, error)) { return false; }
        previous_ = initial;
        initialized_ = true;
        if (!session_.recordEvent({initial.tick, "startup", {
                {"map", initial.currentMap}, {"spawn", initial.currentSpawn}}}) ||
            !session_.recordState(initial, true)) {
            error = "could not write manual audit startup checkpoint";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool observe(const audit::GameAuditSnapshot& current,
                               core::PixelBufferView framebuffer, std::string& error) {
        if (!initialized_) { return true; }
        if (!startupCaptured_ && !session_.captureScreenshot("startup", framebuffer, error)) {
            return false;
        }
        startupCaptured_ = true;
        static_cast<void>(session_.recordState(current));
        const bool mapChanged = current.currentMap != previous_.currentMap;
        if (mapChanged) {
            if (!checkpoint(current, framebuffer, "map_transition", "map_entered", error)) {
                return false;
            }
        }
        if (current.playerHealth < previous_.playerHealth) {
            if (!session_.recordEvent({current.tick, "player_damaged", {
                    {"health", static_cast<std::int64_t>(current.playerHealth)}}})) {
                error = "could not write player damage audit event";
                return false;
            }
        } else if (current.playerHealth > previous_.playerHealth) {
            if (!session_.recordEvent({current.tick, "player_healed", {
                    {"health", static_cast<std::int64_t>(current.playerHealth)}}})) {
                error = "could not write player heal audit event";
                return false;
            }
        }
        if (current.playerHealth == 0 && previous_.playerHealth != 0) {
            if (!checkpoint(current, framebuffer, "player_defeated", "player_defeated", error)) {
                return false;
            }
        }
        if (current.gold != previous_.gold) {
            const auto delta = static_cast<std::int64_t>(current.gold) -
                               static_cast<std::int64_t>(previous_.gold);
            if (!session_.recordEvent({current.tick, "gold_changed", {
                    {"gold", static_cast<std::uint64_t>(current.gold)}, {"delta", delta}}})) {
                error = "could not write gold audit event";
                return false;
            }
        }
        if (!mapChanged) {
            observeEnemies(current);
            observeObjects(current);
            observePickups(current);
        }
        if (current.dialogue.active && !previous_.dialogue.active) {
            if (!checkpoint(current, framebuffer, "dialogue_start", "dialogue_started", error)) {
                return false;
            }
        } else if (!current.dialogue.active && previous_.dialogue.active) {
            if (!session_.recordEvent({current.tick, "dialogue_completed", {}})) {
                error = "could not write dialogue completion audit event";
                return false;
            }
        } else if (current.dialogue.pageIndex != previous_.dialogue.pageIndex ||
                   current.dialogue.nodeId != previous_.dialogue.nodeId) {
            if (!session_.recordEvent({current.tick, "dialogue_page", {
                    {"dialogue", current.dialogue.dialogueId},
                    {"node", current.dialogue.nodeId},
                    {"page", static_cast<std::uint64_t>(current.dialogue.pageIndex)}}})) {
                error = "could not write dialogue page audit event";
                return false;
            }
        }
        if (current.lastEvent != previous_.lastEvent) {
            observeLastEvent(current, error);
            if (!error.empty()) { return false; }
        }
        previous_ = current;
        return true;
    }

    [[nodiscard]] bool captureManual(const audit::GameAuditSnapshot& current,
                                     core::PixelBufferView framebuffer,
                                     std::string& error) {
        return checkpoint(current, framebuffer,
                          "manual_tick_" + std::to_string(current.tick),
                          "manual_capture", error);
    }

    [[nodiscard]] bool close(const audit::GameAuditSnapshot& finalSnapshot,
                             std::string& error) {
        if (!initialized_) { return true; }
        if (!session_.recordEvent({finalSnapshot.tick, "shutdown", {}})) {
            error = "could not write manual audit shutdown event";
            return false;
        }
        return session_.close("PASS");
    }

private:
    [[nodiscard]] bool checkpoint(const audit::GameAuditSnapshot& snapshot,
                                  core::PixelBufferView framebuffer,
                                  std::string_view screenshot,
                                  std::string_view eventType,
                                  std::string& error) {
        if (!session_.recordEvent({snapshot.tick, std::string(eventType), {
                {"map", snapshot.currentMap}}}) ||
            !session_.recordState(snapshot, true) ||
            !session_.captureScreenshot(screenshot, framebuffer, error)) {
            if (error.empty()) { error = "could not write manual audit checkpoint"; }
            return false;
        }
        return true;
    }

    void observeEnemies(const audit::GameAuditSnapshot& current) {
        for (const auto& enemy : current.enemies) {
            const auto previous = std::find_if(previous_.enemies.begin(), previous_.enemies.end(),
                [&](const auto& value) { return value.instanceId == enemy.instanceId; });
            if (previous != previous_.enemies.end() && enemy.health < previous->health) {
                static_cast<void>(session_.recordEvent({current.tick, "entity_damaged", {
                    {"definition", enemy.definitionId},
                    {"instance", enemy.instanceId},
                    {"health", static_cast<std::int64_t>(enemy.health)}}}));
            }
        }
        for (const auto& enemy : previous_.enemies) {
            const auto currentEnemy = std::find_if(current.enemies.begin(), current.enemies.end(),
                [&](const auto& value) { return value.instanceId == enemy.instanceId; });
            if (currentEnemy == current.enemies.end()) {
                static_cast<void>(session_.recordEvent({current.tick, "entity_defeated", {
                    {"definition", enemy.definitionId}, {"instance", enemy.instanceId}}}));
            }
        }
    }

    void observeObjects(const audit::GameAuditSnapshot& current) {
        for (const auto& object : current.objects) {
            const auto previous = std::find_if(previous_.objects.begin(), previous_.objects.end(),
                [&](const auto& value) { return value.instanceId == object.instanceId; });
            if (previous != previous_.objects.end() && object.state != previous->state) {
                const std::string type = object.state == "opened" ? "chest_opened" :
                    object.state == "destroyed" ? "crate_destroyed" : "object_state_changed";
                static_cast<void>(session_.recordEvent({current.tick, type, {
                    {"definition", object.definitionId}, {"instance", object.instanceId},
                    {"state", object.state}}}));
            }
        }
    }

    void observePickups(const audit::GameAuditSnapshot& current) {
        for (const auto& pickup : previous_.pickups) {
            const auto remains = std::find_if(current.pickups.begin(), current.pickups.end(),
                [&](const auto& value) { return value.instanceId == pickup.instanceId; });
            if (remains == current.pickups.end()) {
                static_cast<void>(session_.recordEvent({current.tick, "pickup_collected", {
                    {"definition", pickup.definitionId}, {"instance", pickup.instanceId}}}));
            }
        }
    }

    void observeLastEvent(const audit::GameAuditSnapshot& current, std::string& error) {
        std::string type;
        if (current.lastEvent == "SAVED") { type = "save_completed"; }
        else if (current.lastEvent == "LOADED") { type = "load_completed"; }
        else if (current.lastEvent == "SAVE ERROR") { type = "save_failed"; }
        else if (current.lastEvent == "LOAD ERROR") { type = "load_failed"; }
        if (!type.empty() && !session_.recordEvent({current.tick, type, {}})) {
            error = "could not write save/load audit event";
        }
    }

    audit::AuditSession session_;
    audit::GameAuditSnapshot previous_;
    bool initialized_{};
    bool startupCaptured_{};
};

} // namespace

int run(platform::Platform& platform, const GameLaunchOptions& options) {
    render::Framebuffer framebuffer(core::GameMetrics::logicalWidth,
                                    core::GameMetrics::logicalHeight);
    const auto executableDirectory = platform.executableDirectory();
    Phase7Demo demo(platform.imageDecoder(),
                    findLicensedAssetRoot(executableDirectory), executableDirectory, options);
    core::FixedStepAccumulator accumulator(fixedStepConfig);

    std::uint64_t tickCount = 0;
    std::uint64_t ticksAtLastReport = 0;
    std::uint64_t renderedFrames = 0;
    double previous = platform.nowSeconds();
    double lastReport = previous;

    platform.log(platform::LogLevel::info, "startup: entering fixed-step loop");
    platform.log(platform::LogLevel::info, demo.startupSummary());

    std::unique_ptr<ManualAuditObserver> auditObserver;
    if (options.auditEnabled) {
        auditObserver = std::make_unique<ManualAuditObserver>();
        std::string auditError;
        if (!auditObserver->open(demo.auditSnapshot(), auditError)) {
            platform.log(platform::LogLevel::error, auditError);
            return 1;
        }
    }

    while (platform.isRunning()) {
        platform.pollEvents();
        if (!platform.isRunning()) {
            break;
        }

        if (platform.isMinimized()) {
            platform.waitForEvents();
            previous = platform.nowSeconds();
            continue;
        }

        const double current = platform.nowSeconds();
        const double frameDelta = current - previous;
        previous = current;

        bool manualCaptureRequested = false;
        const core::FixedStepResult step = accumulator.advance(frameDelta, [&] {
            ++tickCount;
            const auto debugInput = platform.consumeDebugInput();
            manualCaptureRequested = manualCaptureRequested ||
                                      debugInput.captureAuditSnapshotPressed;
            demo.fixedTick(tickCount, platform.consumeInputState(), debugInput);
        });

        if (step.frameDeltaClamped || step.catchUpLimited) {
            std::ostringstream message;
            message << "timing protection discarded " << step.discardedSeconds << " seconds";
            platform.log(platform::LogLevel::warning, message.str());
        }

        demo.render(framebuffer);
        if (!platform.present(framebuffer.view())) {
            platform.log(platform::LogLevel::error, "framebuffer presentation failed");
            if (auditObserver) {
                std::string ignored;
                static_cast<void>(auditObserver->close(demo.auditSnapshot(), ignored));
            }
            return 1;
        }
        if (auditObserver) {
            std::string auditError;
            const auto snapshot = demo.auditSnapshot();
            if (!auditObserver->observe(snapshot, framebuffer.view(), auditError) ||
                (manualCaptureRequested && !auditObserver->captureManual(
                    snapshot, framebuffer.view(), auditError))) {
                platform.log(platform::LogLevel::error, auditError);
                static_cast<void>(auditObserver->close(snapshot, auditError));
                return 1;
            }
        }
        ++renderedFrames;

        if (current - lastReport >= 1.0) {
            const auto intervalTicks = tickCount - ticksAtLastReport;
            std::ostringstream message;
            message << "fixed ticks/s=" << intervalTicks << ", rendered frames=" << renderedFrames;
            platform.log(platform::LogLevel::info, message.str());
            lastReport = current;
            ticksAtLastReport = tickCount;
            renderedFrames = 0;
        }
    }

    if (auditObserver) {
        std::string auditError;
        if (!auditObserver->close(demo.auditSnapshot(), auditError)) {
            platform.log(platform::LogLevel::error, auditError);
            return 1;
        }
    }
    platform.log(platform::LogLevel::info, "shutdown: fixed-step loop ended");
    return 0;
}

} // namespace underworld::game
