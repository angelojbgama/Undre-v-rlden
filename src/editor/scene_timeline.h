#pragma once

#include "engine/core/geometry.h"
#include "game/gameplay/scenes/scene_definition.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace underworld::editor {

// This file deliberately owns only scene authoring state and geometry.  It is
// independent from GameSession and RuntimeWorld so editor scrub/playback can
// never mutate the authored map or the running game.
struct SceneTimelineSnap final {
    bool enabled{true};
    std::uint32_t intervalTicks{5};
};

[[nodiscard]] std::uint32_t snapSceneTick(std::uint32_t tick,
                                          SceneTimelineSnap snap) noexcept;

struct SceneClipLocation final {
    std::size_t track{};
    std::size_t clip{};
    [[nodiscard]] bool operator==(const SceneClipLocation&) const noexcept = default;
};

[[nodiscard]] bool sceneClipHasDuration(
    game::gameplay::scenes::SceneClipKind kind) noexcept;
[[nodiscard]] std::uint32_t sceneClipEndTick(
    const game::gameplay::scenes::SceneClip& clip) noexcept;

[[nodiscard]] bool addSceneClip(game::gameplay::scenes::SceneDefinition& scene,
                                 std::size_t track, game::gameplay::scenes::SceneClip clip,
                                 SceneTimelineSnap snap, std::string& error);
[[nodiscard]] bool duplicateSceneClip(game::gameplay::scenes::SceneDefinition& scene,
                                       SceneClipLocation source, std::size_t targetTrack,
                                       std::uint32_t targetTick, SceneTimelineSnap snap,
                                       std::string& error);
[[nodiscard]] bool removeSceneClip(game::gameplay::scenes::SceneDefinition& scene,
                                    SceneClipLocation location, std::string& error);
[[nodiscard]] bool moveSceneClip(game::gameplay::scenes::SceneDefinition& scene,
                                  SceneClipLocation location, std::uint32_t startTick,
                                  SceneTimelineSnap snap, std::string& error);
[[nodiscard]] bool resizeSceneClip(game::gameplay::scenes::SceneDefinition& scene,
                                    SceneClipLocation location, std::uint32_t durationTicks,
                                    SceneTimelineSnap snap, std::string& error);
[[nodiscard]] bool setSceneMoveTarget(game::gameplay::scenes::SceneDefinition& scene,
                                      SceneClipLocation location,
                                      core::WorldPointI target, std::string& error);

[[nodiscard]] bool addSceneMarker(game::gameplay::scenes::SceneDefinition& scene,
                                   game::gameplay::scenes::SceneMarker marker,
                                   SceneTimelineSnap snap, std::string& error);
[[nodiscard]] bool renameSceneMarker(game::gameplay::scenes::SceneDefinition& scene,
                                      std::size_t marker, std::string name,
                                      std::string& error);
[[nodiscard]] bool removeSceneMarker(game::gameplay::scenes::SceneDefinition& scene,
                                      std::size_t marker, std::string& error);
[[nodiscard]] std::optional<std::uint32_t> jumpToSceneMarker(
    const game::gameplay::scenes::SceneDefinition& scene, std::string_view name) noexcept;
[[nodiscard]] std::uint32_t fitSceneDurationToContent(
    game::gameplay::scenes::SceneDefinition& scene) noexcept;

struct ScenePreviewActorInitial final {
    std::string slotId;
    core::WorldPointI position{};
    game::gameplay::FacingDirection facing{game::gameplay::FacingDirection::down};
};

struct ScenePreviewActorState final {
    std::string slotId;
    core::WorldPointI position{};
    game::gameplay::FacingDirection facing{game::gameplay::FacingDirection::down};
    int visualOffsetY{};
    std::optional<game::gameplay::scenes::SceneEmoteKind> emote;
};

struct ScenePreviewState final {
    std::uint32_t tick{};
    std::vector<ScenePreviewActorState> actors;
};

// Rebuilds from tick zero on every call.  This is intentionally simple and
// deterministic; cache invalidation is not part of the first scene editor.
[[nodiscard]] ScenePreviewState evaluateScenePreview(
    const game::gameplay::scenes::SceneDefinition& scene,
    const std::vector<ScenePreviewActorInitial>& initialActors,
    std::uint32_t tick);

struct SceneValidationContext final {
    std::uint32_t mapWidthPixels{};
    std::uint32_t mapHeightPixels{};
    std::vector<simulation::PersistentInstanceId> npcInstances;
    std::vector<simulation::PersistentInstanceId> enemyInstances;
};

[[nodiscard]] game::gameplay::scenes::SceneValidationResult validateSceneTimeline(
    const game::gameplay::scenes::SceneDefinition& scene,
    const SceneValidationContext& context);

struct SceneTimelineGeometry final {
    int originX{};
    int width{};
    std::uint32_t visibleStartTick{};
    float pixelsPerTick{1.0F};
};

struct SceneTimelineRulerMark final {
    std::uint32_t tick{};
    int x{};
    bool major{};
};

struct SceneTimelineClipBounds final {
    int x{};
    int width{};
};

[[nodiscard]] int sceneTimelineXForTick(const SceneTimelineGeometry& geometry,
                                         std::uint32_t tick) noexcept;
[[nodiscard]] std::uint32_t sceneTimelineTickForX(const SceneTimelineGeometry& geometry,
                                                   int x, SceneTimelineSnap snap) noexcept;
[[nodiscard]] std::vector<SceneTimelineRulerMark> sceneTimelineRulerMarks(
    const SceneTimelineGeometry& geometry, std::uint32_t minorTicks = 5,
    std::uint32_t majorTicks = 60);
[[nodiscard]] SceneTimelineClipBounds sceneTimelineClipBounds(
    const SceneTimelineGeometry& geometry,
    const game::gameplay::scenes::SceneClip& clip) noexcept;

} // namespace underworld::editor
