#pragma once

#include "engine/core/geometry.h"
#include "engine/simulation/definition_id.h"

#include <memory>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace underworld::game::presentation {

enum class VisualAssetRoot { gameAssets, contentWorkspace };

struct VisualImageDefinition final {
    simulation::DefinitionId id{};
    VisualAssetRoot root{VisualAssetRoot::gameAssets};
    std::string relativePath;
    [[nodiscard]] bool operator==(const VisualImageDefinition&) const noexcept = default;
};

struct StaticSpriteDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId imageId{};
    std::optional<core::RectI> source;
    core::PointI anchor{};
    [[nodiscard]] bool operator==(const StaticSpriteDefinition&) const noexcept = default;
};

struct AnimationFrameDefinition final {
    core::RectI source{};
    core::PointI anchor{};
    core::PointI drawOffset{};
    std::uint32_t durationTicks{};
    std::vector<std::string> markers;
    [[nodiscard]] bool operator==(const AnimationFrameDefinition&) const noexcept = default;
};

struct AnimationDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId imageId{};
    std::vector<AnimationFrameDefinition> frames;
    bool loop{true};
    [[nodiscard]] bool operator==(const AnimationDefinition&) const noexcept = default;
};

struct DirectionalAnimationRef final {
    // Any one binding is sufficient. The loader fills missing directions from
    // the explicit default and then from the first authored direction.
    std::optional<simulation::DefinitionId> defaultAnimation{};
    std::optional<simulation::DefinitionId> down{};
    std::optional<simulation::DefinitionId> up{};
    std::optional<simulation::DefinitionId> side{};
    [[nodiscard]] bool operator==(const DirectionalAnimationRef&) const noexcept = default;
};

struct EnemyAttackVisualDefinition final {
    simulation::DefinitionId visualActionId{};
    DirectionalAnimationRef clips;
    [[nodiscard]] bool operator==(const EnemyAttackVisualDefinition&) const noexcept = default;
};

struct EnemyVisualDefinition final {
    simulation::DefinitionId id{};
    DirectionalAnimationRef idle;
    std::optional<DirectionalAnimationRef> move;
    std::optional<DirectionalAnimationRef> hurt;
    std::optional<DirectionalAnimationRef> death;
    std::optional<DirectionalAnimationRef> dead;
    // Historical name is retained for the compact runtime model: keys are
    // arbitrary VisualActionIds, not a fixed attack enum or required attack set.
    std::vector<EnemyAttackVisualDefinition> attacks;
    [[nodiscard]] bool operator==(const EnemyVisualDefinition&) const noexcept = default;
};

struct WorldObjectVisualDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId idleAnimationId{};
    std::optional<simulation::DefinitionId> openedAnimationId;
    std::optional<simulation::DefinitionId> destroyingAnimationId;
    std::optional<simulation::DefinitionId> activationInactiveAnimationId;
    std::optional<simulation::DefinitionId> activationActiveAnimationId;
    std::optional<simulation::DefinitionId> doorLockedAnimationId;
    std::optional<simulation::DefinitionId> doorClosedAnimationId;
    std::optional<simulation::DefinitionId> doorOpenAnimationId;
    std::optional<simulation::DefinitionId> destroyedAnimationId;
    [[nodiscard]] bool operator==(const WorldObjectVisualDefinition&) const noexcept = default;
};

template<class T>
class DefinitionCatalog final {
public:
    void add(T definition) {
        if (definition.id.empty()) throw std::invalid_argument("visual definition requires id");
        const auto [it, inserted] = definitions_.emplace(definition.id, std::move(definition));
        static_cast<void>(it);
        if (!inserted) throw std::logic_error("duplicate visual definition id");
    }
    [[nodiscard]] const T* find(const simulation::DefinitionId& id) const noexcept {
        const auto it = definitions_.find(id);
        return it == definitions_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] const T& require(const simulation::DefinitionId& id) const {
        const auto* value = find(id);
        if (!value) throw std::out_of_range("visual definition not found");
        return *value;
    }
    [[nodiscard]] const std::unordered_map<simulation::DefinitionId, T,
                                           simulation::DefinitionIdHash>& values() const noexcept {
        return definitions_;
    }
private:
    std::unordered_map<simulation::DefinitionId, T, simulation::DefinitionIdHash> definitions_;
};

using VisualImageCatalog = DefinitionCatalog<VisualImageDefinition>;
using StaticSpriteDefinitionCatalog = DefinitionCatalog<StaticSpriteDefinition>;
using AnimationDefinitionCatalog = DefinitionCatalog<AnimationDefinition>;
using EnemyVisualDefinitionCatalog = DefinitionCatalog<EnemyVisualDefinition>;
using WorldObjectVisualDefinitionCatalog = DefinitionCatalog<WorldObjectVisualDefinition>;

} // namespace underworld::game::presentation
