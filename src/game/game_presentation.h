#pragma once

#include "engine/core/coordinates.h"
#include "engine/core/game_metrics.h"
#include "engine/render/bitmap_font.h"
#include "engine/render/camera_2d.h"
#include "game/combat_debug.h"
#include "game/effect_system.h"
#include "game/enemy_visual.h"
#include "game/game_view_model.h"
#include "game/player_visual.h"
#include "game/gameplay/dialogue/dialogue_session.h"
#include "game/gameplay/player.h"
#include "game/gameplay/projectile_system.h"
#include "game/gameplay/world_objects.h"
#include "game/gameplay/world_pickups.h"
#include "game/tilesets.h"
#include "game/world_object_visual.h"
#include "game/maps/runtime_world.h"
#include "game/presentation/presentation_effects.h"

#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace underworld::render { class Framebuffer; class Renderer2D; }

namespace underworld::game {

struct GamePresentationFrame final {
    const maps::RuntimeWorld& world;
    const gameplay::Player& player;
    const PlayerVisual& playerVisual;
    const std::vector<EnemyVisualInstance>& enemyVisuals;
    const std::vector<WorldObjectVisualInstance>& objectVisuals;
    const EffectSystem& effects;
    const presentation::PresentationEffectFrame& presentationEffects;
    const gameplay::ProjectileSystem& projectiles;
    const TilesetVisualCatalog& tilesetVisuals;
    const gameplay::npcs::NpcVisualCatalog& npcVisuals;
    const EnemyVisualCatalog& enemyVisualCatalog;
    const WorldObjectVisualCatalog& objectVisualCatalog;
    const std::unordered_map<simulation::DefinitionId,
                             std::shared_ptr<const render::SpriteSheet>,
                             simulation::DefinitionIdHash>& projectileVisuals;
    const std::unordered_map<simulation::DefinitionId,
                             std::shared_ptr<const render::Image>,
                             simulation::DefinitionIdHash>& pickupVisuals;
    const std::unordered_map<simulation::DefinitionId,
                             std::shared_ptr<const render::Image>,
                             simulation::DefinitionIdHash>& itemVisuals;
    const render::BitmapFont& font;
    const std::shared_ptr<const render::Image>& hudHeartImage;
    const std::shared_ptr<const render::Image>& hudMoneyImage;
    const gameplay::dialogue::DialogueSession& dialogue;
    const GameViewModel& view;
    const CombatDebugVisibility& combatDebug;
    const gameplay::Hitbox& activeSword;
    std::string_view lastEvent;
    bool collisionOverlay{};
};

class GamePresentation final {
public:
    GamePresentation() noexcept
        : camera_(core::GameMetrics::logicalWidth, core::GameMetrics::logicalHeight) {}

    void followPlayer(core::WorldPointI playerFeet, int worldWidthPixels,
                      int worldHeightPixels) noexcept;
    void render(render::Framebuffer& framebuffer, const GamePresentationFrame& frame) const;

    [[nodiscard]] const render::Camera2D& camera() const noexcept { return camera_; }

private:
    void renderLayer(render::Renderer2D& renderer, const world::RuntimeMap& map,
                     const world::TileLayer& layer, render::VisibleTileRange visible,
                     const TilesetVisualCatalog& tilesets,
                     core::WorldPointI cameraPosition) const;
    void renderActors(render::Renderer2D& renderer, const GamePresentationFrame& frame,
                      core::WorldPointI cameraPosition) const;
    void renderProjectiles(render::Renderer2D& renderer, const GamePresentationFrame& frame,
                           core::WorldPointI cameraPosition) const;
    void renderEffects(render::Renderer2D& renderer, const GamePresentationFrame& frame,
                       core::WorldPointI cameraPosition) const;
    void renderDebug(render::Renderer2D& renderer, const GamePresentationFrame& frame,
                     render::VisibleTileRange visible, core::WorldPointI cameraPosition) const;
    void renderHud(render::Renderer2D& renderer, const GamePresentationFrame& frame) const;
    void renderShopOverlay(render::Renderer2D& renderer, const GamePresentationFrame& frame) const;

    render::Camera2D camera_;
};

} // namespace underworld::game
