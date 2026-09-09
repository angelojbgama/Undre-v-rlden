#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/definition_id.h"
#include "engine/simulation/persistent_id.h"
#include "game/gameplay/facing_direction.h"
#include "game/maps/world_rules.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace underworld::game::gameplay::scenes {

enum class SceneActorKind { player, npc, enemy };
enum class SceneTrackKind { actor, dialogue, world, presentation };
enum class SceneClipKind { move, face, emote, hop, dialogue, presentationEffect, worldEvent };
enum class SceneEmoteKind { surprise, question, ellipsis };

struct SceneActorBinding final {
    std::string slotId;
    SceneActorKind kind{SceneActorKind::player};
    simulation::PersistentInstanceId instanceId{};
    [[nodiscard]] bool operator==(const SceneActorBinding&) const noexcept = default;
};

// A single deliberately small clip DTO.  Fields not used by the selected kind
// remain at their defaults; validation is the authority for semantic validity.
struct SceneClip final {
    SceneClipKind kind{SceneClipKind::face};
    std::string actorSlot;
    std::uint32_t startTick{};
    std::uint32_t durationTicks{};
    core::WorldPointI targetPosition{};
    gameplay::FacingDirection facing{gameplay::FacingDirection::down};
    SceneEmoteKind emote{SceneEmoteKind::surprise};
    int heightPixels{};
    simulation::DefinitionId dialogueId{};
    bool waitForCompletion{};
    simulation::DefinitionId effectId{};
    maps::WorldAction worldAction{};
    [[nodiscard]] bool operator==(const SceneClip&) const noexcept = default;
};

struct SceneTrack final {
    SceneTrackKind kind{SceneTrackKind::actor};
    std::string actorSlot;
    std::vector<SceneClip> clips;
    [[nodiscard]] bool operator==(const SceneTrack&) const noexcept = default;
};

struct SceneMarker final {
    std::string name;
    std::uint32_t tick{};
    [[nodiscard]] bool operator==(const SceneMarker&) const noexcept = default;
};

struct SceneDefinition final {
    simulation::DefinitionId id{};
    std::uint32_t durationTicks{};
    std::vector<SceneActorBinding> actors;
    std::vector<SceneTrack> tracks;
    std::vector<SceneMarker> markers;
    [[nodiscard]] bool operator==(const SceneDefinition&) const noexcept = default;
};

struct SceneValidationResult final {
    bool valid{};
    std::string error;
    std::string path;
    [[nodiscard]] explicit operator bool() const noexcept { return valid; }
};

[[nodiscard]] SceneValidationResult validateScene(
    const SceneDefinition& scene, std::uint32_t mapWidthPixels,
    std::uint32_t mapHeightPixels,
    const std::vector<simulation::PersistentInstanceId>& npcInstances,
    const std::vector<simulation::PersistentInstanceId>& enemyInstances);

} // namespace underworld::game::gameplay::scenes
