#pragma once

#include "engine/simulation/events.h"
#include "game/gameplay/scenes/scene_definition.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace underworld::game::gameplay::scenes {

struct SceneActorSnapshot final {
    core::WorldPointI position{};
    gameplay::FacingDirection facing{gameplay::FacingDirection::down};
};

struct SceneActorPresentation final {
    SceneActorKind kind{SceneActorKind::player};
    std::string slotId;
    simulation::PersistentInstanceId instanceId{};
    int offsetY{};
    std::optional<SceneEmoteKind> emote;
};

struct ScenePresentationState final {
    std::vector<SceneActorPresentation> actors;
    [[nodiscard]] bool empty() const noexcept { return actors.empty(); }
    void clear() noexcept { actors.clear(); }
};

struct SceneRuntimeHooks final {
    std::function<std::optional<SceneActorSnapshot>(const SceneActorBinding&)> snapshotActor;
    std::function<bool(const SceneActorBinding&, core::WorldPointI)> relocateActor;
    std::function<bool(const SceneActorBinding&, gameplay::FacingDirection)> setFacing;
    std::function<bool(const simulation::DefinitionId&, std::string&)> beginDialogue;
    std::function<bool()> dialogueOpen;
    std::function<void()> closeDialogue;
    std::function<void(const simulation::DefinitionId&, simulation::EventBuffer&)> requestEffect;
    std::function<bool(const maps::WorldAction&, simulation::EventBuffer&)> executeWorldAction;
};

class SceneController final {
public:
    [[nodiscard]] bool start(const simulation::MapId& mapId, const SceneDefinition& scene,
                             SceneRuntimeHooks hooks, simulation::EventBuffer& events,
                             std::string& error);
    void advance(simulation::EventBuffer& events);
    void notifyDialogueCompleted() noexcept {
        waitingForDialogue_ = false;
        sceneDialogueOpen_ = false;
    }
    void abort(simulation::EventBuffer& events, std::string reason = {});

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool waitingForDialogue() const noexcept { return waitingForDialogue_; }
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] std::uint32_t tick() const noexcept { return tick_; }
    [[nodiscard]] const simulation::DefinitionId& sceneId() const noexcept { return scene_.id; }
    [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }
    [[nodiscard]] const ScenePresentationState& presentation() const noexcept { return presentation_; }

private:
    struct ClipKey final {
        std::size_t track{};
        std::size_t clip{};
        [[nodiscard]] bool operator==(const ClipKey&) const noexcept = default;
    };
    struct ClipKeyHash final {
        std::size_t operator()(const ClipKey& value) const noexcept {
            return value.track * 1315423911U + value.clip;
        }
    };

    [[nodiscard]] const SceneActorBinding* actor(std::string_view slot) const noexcept;
    [[nodiscard]] core::WorldPointI positionAt(const SceneActorBinding& binding,
                                                std::uint32_t tick) const;
    void applyActorState(simulation::EventBuffer& events);
    void fireEvents(simulation::EventBuffer& events);
    void updatePresentation();
    void finish(simulation::EventBuffer& events);

    simulation::MapId mapId_{};
    SceneDefinition scene_{};
    SceneRuntimeHooks hooks_{};
    simulation::EventBuffer* events_{};
    std::unordered_map<std::string, SceneActorSnapshot> initialActors_;
    std::unordered_map<ClipKey, bool, ClipKeyHash> fired_;
    ScenePresentationState presentation_;
    std::uint32_t tick_{};
    bool active_{};
    bool waitingForDialogue_{};
    bool sceneDialogueOpen_{};
    bool finished_{};
    std::string lastError_;
};

} // namespace underworld::game::gameplay::scenes
