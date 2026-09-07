#pragma once

#include "engine/platform/image_decoder.h"
#include "game/enemy_visual.h"
#include "game/game_content.h"
#include "game/gameplay/npcs/npc_engine.h"
#include "game/world_object_visual.h"
#include "game/presentation/visual_content.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace underworld::game::presentation {

struct VisualAssetRoots final {
    std::filesystem::path gameAssetsRoot;
    std::optional<std::filesystem::path> contentWorkspaceRoot;
};

enum class VisualContentDiagnosticStage { resolve, decode, validate, compile };

struct VisualContentDiagnostic final {
    VisualContentDiagnosticStage stage{VisualContentDiagnosticStage::resolve};
    std::string code;
    simulation::DefinitionId definitionId{};
    std::optional<VisualAssetRoot> assetRoot;
    std::string relativePath;
    std::string message;
};

struct RuntimeStaticSprite final {
    simulation::DefinitionId id{};
    std::shared_ptr<const render::SpriteSheet> sheet;
    render::SpriteFrame frame{};
};

class RuntimeStaticSpriteCatalog final {
public:
    void add(RuntimeStaticSprite sprite);
    [[nodiscard]] const RuntimeStaticSprite* find(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const RuntimeStaticSprite& require(const simulation::DefinitionId& id) const;
private:
    std::unordered_map<simulation::DefinitionId, RuntimeStaticSprite,
                       simulation::DefinitionIdHash> sprites_;
};

class RuntimeAnimationCatalog final {
public:
    void add(simulation::DefinitionId id, std::shared_ptr<const render::AnimationClip> clip);
    [[nodiscard]] const std::shared_ptr<const render::AnimationClip>* find(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const std::shared_ptr<const render::AnimationClip>& require(
        const simulation::DefinitionId& id) const;
private:
    std::unordered_map<simulation::DefinitionId, std::shared_ptr<const render::AnimationClip>,
                       simulation::DefinitionIdHash> clips_;
};

struct RuntimeNpcVisualSet final {
    simulation::DefinitionId id{};
    core::ColorRGBA8 markerColor{};
    std::optional<DirectionalAnimationClips> idle;
};

class RuntimeNpcVisualInstance final {
public:
    RuntimeNpcVisualInstance(simulation::EntityHandle handle,
                             const RuntimeNpcVisualSet& visualSet);

    void update(const gameplay::npcs::NpcInstance& npc, std::uint64_t ticks = 1);
    [[nodiscard]] simulation::EntityHandle handle() const noexcept { return handle_; }
    [[nodiscard]] bool hasSprite() const noexcept { return animator_.hasClip(); }
    [[nodiscard]] const render::Animator& animator() const noexcept { return animator_; }
    [[nodiscard]] bool flipX() const noexcept { return flipX_; }

private:
    simulation::EntityHandle handle_{};
    const RuntimeNpcVisualSet* visualSet_{};
    render::Animator animator_{};
    gameplay::FacingDirection facing_{gameplay::FacingDirection::down};
    bool flipX_{};
    bool initialized_{};
};

class RuntimeNpcVisualCatalog final {
public:
    void add(RuntimeNpcVisualSet set);
    [[nodiscard]] const RuntimeNpcVisualSet* find(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const RuntimeNpcVisualSet& require(const simulation::DefinitionId& id) const;
private:
    std::unordered_map<simulation::DefinitionId, RuntimeNpcVisualSet,
                       simulation::DefinitionIdHash> sets_;
};

struct RuntimeVisualContent final {
    RuntimeStaticSpriteCatalog staticSprites;
    RuntimeAnimationCatalog animations;
    EnemyVisualCatalog enemies;
    WorldObjectVisualCatalog objects;
    RuntimeNpcVisualCatalog npcs;
};

struct VisualContentLoadResult final {
    std::optional<RuntimeVisualContent> content;
    std::vector<VisualContentDiagnostic> diagnostics;
    [[nodiscard]] explicit operator bool() const noexcept {
        return content.has_value() && diagnostics.empty();
    }
};

class VisualContentLoader final {
public:
    explicit VisualContentLoader(platform::ImageDecoder& decoder) noexcept : decoder_(decoder) {}
    [[nodiscard]] VisualContentLoadResult load(const GameContentRegistry& registry,
                                               const VisualAssetRoots& roots) const;
private:
    platform::ImageDecoder& decoder_;
};

[[nodiscard]] const char* visualContentStageName(VisualContentDiagnosticStage stage) noexcept;
[[nodiscard]] const char* visualAssetRootName(VisualAssetRoot root) noexcept;
[[nodiscard]] std::string formatVisualContentDiagnostic(
    const VisualContentDiagnostic& diagnostic);

} // namespace underworld::game::presentation
