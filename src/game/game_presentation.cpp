#include "game/game_presentation.h"

#include "engine/core/game_metrics.h"
#include "engine/render/bitmap_font.h"
#include "engine/render/framebuffer.h"
#include "engine/render/renderer_2d.h"
#include "game/actor_render_order.h"
#include "game/presentation/presentation_effect_renderer.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace underworld::game {
namespace {

const gameplay::scenes::SceneActorPresentation* sceneActorPresentation(
    const gameplay::scenes::ScenePresentationState& state,
    gameplay::scenes::SceneActorKind kind, simulation::PersistentInstanceId instance) {
    const auto found = std::find_if(state.actors.begin(), state.actors.end(),
        [&](const auto& value) { return value.kind == kind && value.instanceId == instance; });
    return found == state.actors.end() ? nullptr : &*found;
}

const char* emoteText(gameplay::scenes::SceneEmoteKind emote) noexcept {
    switch (emote) {
    case gameplay::scenes::SceneEmoteKind::surprise: return "!";
    case gameplay::scenes::SceneEmoteKind::question: return "?";
    case gameplay::scenes::SceneEmoteKind::ellipsis: return "...";
    }
    return "?";
}

void outline(render::Renderer2D& renderer, world::AabbI box,
             core::WorldPointI camera, core::ColorRGBA8 color) {
    const int x = box.x - camera.x;
    const int y = box.y - camera.y;
    renderer.fillRect({x, y, box.width, 1}, color);
    renderer.fillRect({x, y + box.height - 1, box.width, 1}, color);
    renderer.fillRect({x, y, 1, box.height}, color);
    renderer.fillRect({x + box.width - 1, y, 1, box.height}, color);
}

core::LogicalPointI toLogical(core::WorldPointI world, core::WorldPointI camera) noexcept {
    return {world.x - camera.x, world.y - camera.y};
}

void drawWrappedText(render::Renderer2D& renderer, const render::BitmapFont& font,
                     std::string_view text, int x, int y, std::size_t maximumColumns,
                     std::size_t maximumLines) {
    std::string line;
    std::size_t linesDrawn = 0;
    std::size_t cursor = 0;
    while (cursor < text.size() && linesDrawn < maximumLines) {
        while (cursor < text.size() && text[cursor] == ' ') { ++cursor; }
        const auto nextSpace = text.find_first_of(" \n", cursor);
        const auto wordEnd = nextSpace == std::string_view::npos ? text.size() : nextSpace;
        const std::string word{text.substr(cursor, wordEnd - cursor)};
        if (line.empty()) {
            line = word;
        } else if (line.size() + 1U + word.size() <= maximumColumns) {
            line += ' ';
            line += word;
        } else {
            render::drawText(renderer, font, line, x,
                              y + static_cast<int>(linesDrawn) * font.lineHeight());
            ++linesDrawn;
            line = word;
        }
        cursor = wordEnd;
        if (cursor < text.size() && text[cursor] == '\n') {
            render::drawText(renderer, font, line, x,
                              y + static_cast<int>(linesDrawn) * font.lineHeight());
            ++linesDrawn;
            line.clear();
            ++cursor;
        } else if (cursor < text.size()) {
            ++cursor;
        }
    }
    if (!line.empty() && linesDrawn < maximumLines) {
        render::drawText(renderer, font, line, x,
                         y + static_cast<int>(linesDrawn) * font.lineHeight());
    }
}

render::QuarterTurn projectileRotation(gameplay::FacingDirection canonical,
                                       gameplay::FacingDirection direction) noexcept {
    switch (gameplay::clockwiseQuarterTurns(canonical, direction)) {
    case 1: return render::QuarterTurn::r90;
    case 2: return render::QuarterTurn::r180;
    case 3: return render::QuarterTurn::r270;
    default: return render::QuarterTurn::r0;
    }
}

core::PointI rotatedAnchor(core::RectI source, core::PointI anchor,
                           render::QuarterTurn rotation) noexcept {
    switch (rotation) {
    case render::QuarterTurn::r90: return {source.height - anchor.y, anchor.x};
    case render::QuarterTurn::r180: return {source.width - anchor.x, source.height - anchor.y};
    case render::QuarterTurn::r270: return {anchor.y, source.width - anchor.x};
    case render::QuarterTurn::r0: return anchor;
    }
    return anchor;
}

} // namespace

void GamePresentation::followPlayer(core::WorldPointI playerFeet, int worldWidthPixels,
                                    int worldHeightPixels) noexcept {
    camera_.centerOn(playerFeet);
    camera_.clampToWorld(worldWidthPixels, worldHeightPixels);
}

void GamePresentation::renderLayer(render::Renderer2D& renderer, const world::RuntimeMap& map,
                                   const world::TileLayer& layer,
                                   render::VisibleTileRange visible,
                                   const TilesetVisualCatalog& tilesets,
                                   core::WorldPointI cameraPosition) const {
    if (!layer.visible() || visible.empty()) { return; }
    for (int y = visible.firstY; y <= visible.lastY; ++y) {
        for (int x = visible.firstX; x <= visible.lastX; ++x) {
            const world::TileCell& cell = layer.cell(x, y);
            if (!cell) { continue; }
            const auto* tilesetVisual = tilesets.find(cell->definition.tilesetId);
            if (!tilesetVisual) {
                throw std::runtime_error("active map references an unavailable tileset image");
            }
            const core::RectI source = tilesetVisual->atlas.sourceRect(cell->definition.sourceIndex);
            const int dx = x * map.tileSize() - cameraPosition.x;
            const int dy = y * map.tileSize() - cameraPosition.y;
            if (world::hasFlag(cell->flags, world::TileFlags::flipX)) {
                renderer.drawImageRegionFlipX(*tilesetVisual->image, source, dx, dy);
            } else {
                renderer.drawImageRegion(*tilesetVisual->image, source, dx, dy);
            }
        }
    }
}

void GamePresentation::renderActors(render::Renderer2D& renderer,
                                    const GamePresentationFrame& frame,
                                    core::WorldPointI cameraPosition) const {
    enum class ActorKind { player, enemy, npc, object, objectResidue, pickup };
    struct Actor { int sortY; simulation::EntityHandle handle; ActorKind kind; std::size_t index{}; };
    const auto& world = frame.world;
    const auto& enemies = world.enemies();
    const auto& npcs = world.npcs();
    const auto& objects = world.objects();
    const auto& objectResidues = frame.objectResidueVisuals;
    const auto& pickups = world.pickups();
    std::vector<Actor> actors;
    actors.reserve(enemies.size() + npcs.size() + objects.size() + objectResidues.size() + pickups.size() + 1);
    actors.push_back({frame.player.feetPosition().y, frame.player.entityHandle(), ActorKind::player});
    for (std::size_t i = 0; i < enemies.size(); ++i) {
        actors.push_back({enemies[i].instance.feetPosition().y, enemies[i].instance.handle(), ActorKind::enemy, i});
    }
    for (std::size_t i = 0; i < npcs.size(); ++i) {
        actors.push_back({npcs[i].instance.position().y, npcs[i].instance.handle(), ActorKind::npc, i});
    }
    for (std::size_t i = 0; i < objects.size(); ++i) {
        actors.push_back({objects[i].instance.position().y, objects[i].instance.handle(), ActorKind::object, i});
    }
    for (std::size_t i = 0; i < objectResidues.size(); ++i) {
        // Residues have no runtime handle by design. Their persistent ID only
        // supplies a deterministic tie-breaker for Y-sorting.
        actors.push_back({objectResidues[i].position().y,
                          {static_cast<std::uint32_t>(objectResidues[i].persistentId().value), 0},
                          ActorKind::objectResidue, i});
    }
    for (std::size_t i = 0; i < pickups.size(); ++i) {
        actors.push_back({pickups[i].instance.position().y, pickups[i].instance.handle(), ActorKind::pickup, i});
    }
    std::sort(actors.begin(), actors.end(), [](const Actor& a, const Actor& b) {
        return actorRendersBefore({a.sortY, a.handle}, {b.sortY, b.handle});
    });
    for (const Actor& actor : actors) {
        if (actor.kind == ActorKind::player) {
            const auto* scene = sceneActorPresentation(frame.scenePresentation,
                gameplay::scenes::SceneActorKind::player, {});
            const auto logical = toLogical(frame.player.feetPosition(), cameraPosition);
            render::drawAnimator(renderer, frame.playerVisual.animator(),
                                 {logical.x, logical.y + (scene ? scene->offsetY : 0)},
                                 frame.playerVisual.flipX());
            if (scene && scene->emote) {
                render::drawText(renderer, frame.font, emoteText(*scene->emote), logical.x - 2,
                                 logical.y - 34 + scene->offsetY);
            }
        } else if (actor.kind == ActorKind::enemy) {
            const auto& enemy = enemies[actor.index].instance;
            const auto* scene = sceneActorPresentation(frame.scenePresentation,
                gameplay::scenes::SceneActorKind::enemy, world.enemies()[actor.index].persistentId);
            const auto logical = toLogical(enemy.feetPosition(), cameraPosition);
            render::drawAnimator(renderer, frame.enemyVisuals[actor.index].animator(),
                                 {logical.x, logical.y + (scene ? scene->offsetY : 0)},
                                 frame.enemyVisuals[actor.index].flipX());
            if (scene && scene->emote) {
                render::drawText(renderer, frame.font, emoteText(*scene->emote), logical.x - 2,
                                 logical.y - 34 + scene->offsetY);
            }
        } else if (actor.kind == ActorKind::npc) {
            const auto& npc = npcs[actor.index].instance;
            const auto* scene = sceneActorPresentation(frame.scenePresentation,
                gameplay::scenes::SceneActorKind::npc, world.npcs()[actor.index].persistentId);
            const auto logical = toLogical(npc.position(), cameraPosition);
            const auto& visualSet = frame.npcVisualCatalog.require(npc.definition().visualSetId);
            if (actor.index < frame.npcVisuals.size() &&
                frame.npcVisuals[actor.index].hasSprite()) {
                render::drawAnimator(renderer, frame.npcVisuals[actor.index].animator(),
                                     {logical.x, logical.y + (scene ? scene->offsetY : 0)},
                                     frame.npcVisuals[actor.index].flipX());
            } else {
                renderer.fillRect({logical.x - 6, logical.y - 20 + (scene ? scene->offsetY : 0),
                                   12, 20}, visualSet.markerColor);
            }
            if (scene && scene->emote) {
                render::drawText(renderer, frame.font, emoteText(*scene->emote), logical.x - 2,
                                 logical.y - 34 + scene->offsetY);
            }
        } else if (actor.kind == ActorKind::object) {
            const auto logical = toLogical(objects[actor.index].instance.position(), cameraPosition);
            render::drawAnimator(renderer, frame.objectVisuals[actor.index].animator(),
                                 {logical.x, logical.y});
        } else if (actor.kind == ActorKind::objectResidue) {
            const auto logical = toLogical(objectResidues[actor.index].position(), cameraPosition);
            render::drawAnimator(renderer, objectResidues[actor.index].animator(),
                                 {logical.x, logical.y});
        } else {
            const auto& pickup = pickups[actor.index].instance;
            const auto& sprite = frame.staticSprites.require(pickup.definition().visualId);
            const auto logical = toLogical(pickup.position(), cameraPosition);
            render::drawSprite(renderer, *sprite.sheet, sprite.frame,
                               {logical.x, logical.y});
        }
    }
}

void GamePresentation::renderProjectiles(render::Renderer2D& renderer,
                                         const GamePresentationFrame& frame,
                                         core::WorldPointI cameraPosition) const {
    for (const auto& projectile : frame.projectiles.projectiles()) {
        if (!projectile.definition) { continue; }
        const auto& sprite = frame.staticSprites.require(projectile.definition->visualId);
        const auto rotation = projectileRotation(projectile.definition->canonicalFacing,
                                                  projectile.direction);
        const auto anchor = rotatedAnchor(sprite.frame.source, sprite.frame.anchor, rotation);
        renderer.drawImageRegionQuarterTurn(
            sprite.sheet->image(), sprite.frame.source,
            projectile.position.x - cameraPosition.x - anchor.x,
            projectile.position.y - cameraPosition.y - anchor.y, rotation);
    }
}

void GamePresentation::renderEffects(render::Renderer2D& renderer,
                                     const GamePresentationFrame& frame,
                                     core::WorldPointI cameraPosition) const {
    for (const auto& effect : frame.effects.effects()) {
        const auto logical = toLogical(effect.position, cameraPosition);
        render::drawAnimator(renderer, effect.animator, {logical.x, logical.y});
    }
}

void GamePresentation::renderDebug(render::Renderer2D& renderer,
                                   const GamePresentationFrame& frame,
                                   render::VisibleTileRange visible,
                                   core::WorldPointI cameraPosition) const {
    const auto& map = frame.world.map();
    if (frame.collisionOverlay && !visible.empty()) {
        constexpr core::ColorRGBA8 fill{255, 24, 32, 72};
        for (int y = visible.firstY; y <= visible.lastY; ++y) {
            for (int x = visible.firstX; x <= visible.lastX; ++x) {
                if (map.collision().isSolid(x, y)) {
                    renderer.fillRect({x * map.tileSize() - cameraPosition.x,
                                       y * map.tileSize() - cameraPosition.y,
                                       map.tileSize(), map.tileSize()}, fill);
                }
            }
        }
    }
    if (frame.combatDebug.collisionBody) {
        outline(renderer, frame.player.collisionBody(), cameraPosition, {32, 255, 96, 255});
        for (const auto& enemy : frame.world.enemies()) {
            outline(renderer, enemy.instance.collisionBody(), cameraPosition, {32, 255, 96, 255});
        }
    }
    if (frame.combatDebug.hurtbox) {
        outline(renderer, frame.player.hurtbox().bounds, cameraPosition, {32, 220, 255, 255});
        for (const auto& enemy : frame.world.enemies()) {
            if (enemy.instance.hurtbox().enabled) {
                outline(renderer, enemy.instance.hurtbox().bounds, cameraPosition, {32, 220, 255, 255});
            }
        }
        for (const auto& object : frame.world.objects()) {
            if (object.instance.hurtbox().enabled) {
                outline(renderer, object.instance.hurtbox().bounds, cameraPosition, {32, 220, 255, 255});
            }
        }
    }
    if (frame.combatDebug.hitbox) {
        if (frame.activeSword.enabled) {
            outline(renderer, frame.activeSword.bounds, cameraPosition, {255, 48, 48, 255});
        }
        for (const auto& projectile : frame.projectiles.projectiles()) {
            outline(renderer, projectile.hitbox(), cameraPosition, {255, 220, 32, 255});
        }
        for (const auto& persistent : frame.world.enemies()) {
            const auto& enemy = persistent.instance;
            if (!enemy.activeAttack() || !enemy.activeAttack()->meleeHitboxActive ||
                !enemy.activeAttack()->definition->meleeHitboxes) { continue; }
            outline(renderer, enemy.activeAttack()->definition->meleeHitboxes->forFacing(
                        enemy.activeAttack()->lockedFacing).at(enemy.feetPosition()),
                    cameraPosition, {255, 48, 48, 255});
        }
    }
    if (frame.combatDebug.interaction) {
        outline(renderer, frame.player.interactionArea().bounds, cameraPosition, {255, 64, 255, 255});
        for (const auto& object : frame.world.objects()) {
            if (const auto area = object.instance.interactionArea()) {
                outline(renderer, *area, cameraPosition, {255, 64, 255, 255});
            }
        }
        for (const auto& pickup : frame.world.pickups()) {
            outline(renderer, pickup.instance.collectionArea(), cameraPosition, {255, 200, 64, 255});
        }
    }
}

void GamePresentation::renderHud(render::Renderer2D& renderer,
                                 const GamePresentationFrame& frame) const {
    const auto& view = frame.view;
    renderer.fillRect({0, 0, core::GameMetrics::logicalWidth, 14}, {8, 10, 16, 220});
    for (int index = 0; index < view.playerMaximumHealth; ++index) {
        if (index < view.playerHealth) {
            renderer.drawImage(*frame.hudHeartImage, 3 + index * 12, 2);
        } else {
            renderer.fillRect({3 + index * 12, 3, 9, 8}, {54, 30, 38, 255});
        }
    }
    renderer.drawImage(*frame.hudMoneyImage, 68, 2);
    render::drawText(renderer, frame.font, std::to_string(view.gold), 79, 2);
    render::drawText(renderer, frame.font, "MAP: " + std::string(frame.world.id().value()), 116, 2);
    if (!frame.lastEvent.empty()) { render::drawText(renderer, frame.font, std::string(frame.lastEvent), 190, 2); }
    renderer.fillRect({0, 194, core::GameMetrics::logicalWidth, 30}, {8, 10, 16, 220});
    for (std::size_t index = 0; index < view.quickSlots.size(); ++index) {
        const int x = 4 + static_cast<int>(index) * 40;
        renderer.fillRect({x, 197, 34, 23}, {54, 30, 38, 255});
        render::drawText(renderer, frame.font, std::to_string(index + 1), x + 2, 199);
        if (view.quickSlots[index].visualId) {
            if (const auto* sprite = frame.staticSprites.find(*view.quickSlots[index].visualId)) {
                render::drawSprite(renderer, *sprite->sheet, sprite->frame,
                                   {x + 10 + sprite->frame.anchor.x, 199 + sprite->frame.anchor.y});
            }
            render::drawText(renderer, frame.font, std::to_string(view.quickSlots[index].quantity), x + 22, 209);
        }
    }
    render::drawText(renderer, frame.font, "I ITEMS  E OPEN", 169, 203);
    if (frame.dialogue.isOpen()) {
        renderer.fillRect({8, 130, 256, 62}, {8, 10, 16, 248});
        renderer.fillRect({9, 131, 254, 60}, {54, 30, 38, 255});
        render::drawText(renderer, frame.font, std::string(frame.dialogue.speaker()), 14, 134);
        render::drawText(renderer, frame.font, std::to_string(frame.dialogue.pageIndex() + 1) + "/" +
                         std::to_string(frame.dialogue.pageCount()), 238, 134);
        drawWrappedText(renderer, frame.font, frame.dialogue.currentPage(), 14, 145, 34, 2);
        if (frame.dialogue.choicesVisible()) {
            for (std::size_t index = 0; index < frame.dialogue.choiceCount(); ++index) {
                const int y = 164 + static_cast<int>(index) * 11;
                if (index == frame.dialogue.selectedChoice()) { renderer.fillRect({12, y - 1, 244, 10}, {96, 62, 54, 255}); }
                render::drawText(renderer, frame.font, std::to_string(index + 1) + ": " +
                                 std::string(frame.dialogue.choiceLabel(index)), 14, y);
            }
        } else { render::drawText(renderer, frame.font, "E NEXT  X CLOSE", 14, 181); }
        return;
    }
    if (view.shopOpen) { renderShopOverlay(renderer, frame); return; }
    if (view.bankOpen) {
        renderer.fillRect({4, 24, 264, 169}, {8, 10, 16, 248});
        render::drawText(renderer, frame.font, "BANK", 8, 27);
        const auto drawGrid = [&](const auto& slots, std::size_t selected,
                                  gameplay::BankOverlayFocus focus,
                                  gameplay::BankOverlayFocus gridFocus, int y, int cell) {
            for (std::size_t index = 0; index < slots.size(); ++index) {
                const int x = 8 + static_cast<int>(index % 10) * cell;
                const int row = static_cast<int>(index / 10);
                const int slotY = y + row * 15;
                const bool highlighted = view.bankFocus == focus && index == selected;
                renderer.fillRect({x, slotY, cell - 2, 13}, highlighted
                    ? core::ColorRGBA8{220, 180, 72, 255} : core::ColorRGBA8{54, 30, 38, 255});
                if (slots[index].visualId) {
                    if (const auto* sprite = frame.staticSprites.find(*slots[index].visualId)) {
                        render::drawSprite(renderer, *sprite->sheet, sprite->frame,
                                           {x + 1 + sprite->frame.anchor.x, slotY + sprite->frame.anchor.y});
                    }
                }
                if (slots[index].quantity > 1) {
                    render::drawText(renderer, frame.font, std::to_string(slots[index].quantity), x + 1, slotY + 4);
                }
            }
            static_cast<void>(gridFocus);
        };
        render::drawText(renderer, frame.font, "INVENTORY", 8, 43);
        drawGrid(view.inventory, view.inventorySelection, gameplay::BankOverlayFocus::inventory,
                 gameplay::BankOverlayFocus::inventory, 53, 26);
        render::drawText(renderer, frame.font, "STORAGE", 8, 105);
        drawGrid(view.bank, view.bankSelection, gameplay::BankOverlayFocus::bank,
                 gameplay::BankOverlayFocus::bank, 115, 26);
        render::drawText(renderer, frame.font, "CARRIED " + std::to_string(view.gold) +
                         "  STORED " + std::to_string(view.bankGold), 8, 188);
        render::drawText(renderer, frame.font, "Z TRANSFER  I CLOSE", 152, 188);
        return;
    }
    if (!view.inventoryOpen) { return; }
    renderer.fillRect({6, 52, 260, 145}, {8, 10, 16, 245});
    render::drawText(renderer, frame.font, "INVENTORY", 10, 55);
    for (std::size_t index = 0; index < view.inventory.size(); ++index) {
        const int column = static_cast<int>(index % 10);
        const int row = static_cast<int>(index / 10);
        const int x = 10 + column * 25;
        const int y = 66 + row * 20;
        const core::ColorRGBA8 slotColor = view.inventoryFocus == gameplay::InventoryOverlayFocus::inventory && index == view.inventorySelection
            ? core::ColorRGBA8{220, 180, 72, 255} : core::ColorRGBA8{54, 30, 38, 255};
        renderer.fillRect({x, y, 22, 18}, slotColor);
        if (view.inventory[index].visualId) {
            if (const auto* sprite = frame.staticSprites.find(*view.inventory[index].visualId)) {
                render::drawSprite(renderer, *sprite->sheet, sprite->frame,
                                   {x + 3 + sprite->frame.anchor.x, y + 1 + sprite->frame.anchor.y});
            }
            if (view.inventory[index].quantity > 1) {
                render::drawText(renderer, frame.font, std::to_string(view.inventory[index].quantity), x + 10, y + 9);
            }
        }
    }
    render::drawText(renderer, frame.font, "EQUIPMENT", 10, 128);
    const auto drawEquipmentSlot = [&](const char* label, const ItemSlotView& item,
                                       gameplay::rpg::EquipmentSlot slot, int x) {
        const bool selected = view.inventoryFocus == gameplay::InventoryOverlayFocus::equipment &&
                              view.equipmentSelection == slot;
        renderer.fillRect({x, 138, 92, 22}, selected
            ? core::ColorRGBA8{220, 180, 72, 255} : core::ColorRGBA8{54, 30, 38, 255});
        render::drawText(renderer, frame.font, label, x + 3, 141);
        if (item.itemId && item.visualId) {
            if (const auto* sprite = frame.staticSprites.find(*item.visualId)) {
                render::drawSprite(renderer, *sprite->sheet, sprite->frame,
                                   {x + 55 + sprite->frame.anchor.x, 140 + sprite->frame.anchor.y});
            }
        }
    };
    drawEquipmentSlot("ARMOR", view.armor, gameplay::rpg::EquipmentSlot::armor, 10);
    drawEquipmentSlot("ACCESSORY", view.accessory, gameplay::rpg::EquipmentSlot::accessory, 108);
    render::drawText(renderer, frame.font, "MAX HP " + std::to_string(view.derivedMaximumHealth) +
                     "  ATK +" + std::to_string(view.playerAttackDamageBonus), 10, 164);
    render::drawText(renderer, frame.font, "Z USE/EQUIP  1-4 BIND  I CLOSE", 10, 181);
}

void GamePresentation::renderShopOverlay(render::Renderer2D& renderer,
                                         const GamePresentationFrame& frame) const {
    const auto& view = frame.view;
    renderer.fillRect({4, 24, 264, 169}, {8, 10, 16, 248});
    render::drawText(renderer, frame.font, "SHOP " + std::string(view.shopMode == gameplay::ShopOverlayMode::buy ? "BUY" : "SELL"), 8, 27);
    render::drawText(renderer, frame.font, "CARRIED GOLD " + std::to_string(view.gold), 8, 39);
    if (view.shopMode == gameplay::ShopOverlayMode::buy) {
        std::size_t selected = 0;
        for (const auto& offer : view.shopOffers) {
            if (!offer.playerBuyPrice) continue;
            if (selected == view.shopBuySelection) renderer.fillRect({8, 49 + static_cast<int>(selected) * 12, 250, 11}, {96, 62, 54, 255});
            const auto label = std::string(offer.itemId.value());
            render::drawText(renderer, frame.font, label.substr(0, 22) + " " + std::to_string(*offer.playerBuyPrice) + "G", 11, 50 + static_cast<int>(selected) * 12);
            ++selected;
            if (selected == 10) break;
        }
    } else {
        for (std::size_t index = 0; index < view.inventory.size(); ++index) {
            const int x = 8 + static_cast<int>(index % 10) * 25, y = 51 + static_cast<int>(index / 10) * 20;
            renderer.fillRect({x, y, 22, 18}, index == view.shopInventorySelection ? core::ColorRGBA8{220, 180, 72, 255} : core::ColorRGBA8{54, 30, 38, 255});
            if (view.inventory[index].visualId) {
                if (const auto* sprite = frame.staticSprites.find(*view.inventory[index].visualId)) {
                    render::drawSprite(renderer, *sprite->sheet, sprite->frame,
                                       {x + 3 + sprite->frame.anchor.x, y + 1 + sprite->frame.anchor.y});
                }
            }
        }
        const auto& selected = view.inventory[view.shopInventorySelection];
        render::drawText(renderer, frame.font, selected.itemId ? std::string(selected.itemId->value()).substr(0, 24) : "EMPTY SLOT", 8, 116);
        render::drawText(renderer, frame.font, "SELL: see offer", 8, 128);
    }
    if (view.shopFeedback) render::drawText(renderer, frame.font, "STATUS " + std::to_string(static_cast<int>(*view.shopFeedback)), 8, 177);
    render::drawText(renderer, frame.font, "PRIMARY TRADE  SECONDARY SWITCH  I CLOSE", 8, 188);
}

void GamePresentation::render(render::Framebuffer& framebuffer,
                              const GamePresentationFrame& frame) const {
    framebuffer.clear({28, 13, 22, 255});
    render::Renderer2D renderer(framebuffer);
    const auto& map = frame.world.map();
    const auto baseCameraPosition = camera_.position();
    const core::WorldPointI cameraPosition{
        baseCameraPosition.x - frame.presentationEffects.cameraOffset.x,
        baseCameraPosition.y - frame.presentationEffects.cameraOffset.y};
    render::Camera2D effectiveCamera(core::GameMetrics::logicalWidth,
                                     core::GameMetrics::logicalHeight);
    effectiveCamera.setPosition(cameraPosition);
    const auto visible = effectiveCamera.visibleTiles(map.widthTiles(), map.heightTiles(), map.tileSize());
    std::size_t groundLayer = 0;
    std::size_t foregroundLayer = map.layerCount();
    std::vector<std::size_t> lowLayers;
    for (std::size_t index = 0; index < map.layerCount(); ++index) {
        const auto name = map.layer(index).name();
        if (name == "ground") { groundLayer = index; }
        else if (name == "foreground") { foregroundLayer = index; }
    }
    for (std::size_t index = 0; index < map.layerCount(); ++index) {
        if (index != groundLayer && index != foregroundLayer) { lowLayers.push_back(index); }
    }
    if (groundLayer < map.layerCount()) { renderLayer(renderer, map, map.layer(groundLayer), visible, frame.tilesetVisuals, cameraPosition); }
    for (const auto layer : lowLayers) { renderLayer(renderer, map, map.layer(layer), visible, frame.tilesetVisuals, cameraPosition); }
    renderActors(renderer, frame, cameraPosition);
    renderProjectiles(renderer, frame, cameraPosition);
    if (foregroundLayer < map.layerCount()) { renderLayer(renderer, map, map.layer(foregroundLayer), visible, frame.tilesetVisuals, cameraPosition); }
    renderEffects(renderer, frame, cameraPosition);
    const auto playerLogical = toLogical(frame.player.feetPosition(), cameraPosition);
    presentation::PresentationEffectRenderer::applyWorld(
        framebuffer, frame.presentationEffects, playerLogical);
    renderDebug(renderer, frame, visible, cameraPosition);
    renderHud(renderer, frame);
    presentation::PresentationEffectRenderer::applyFinal(framebuffer, frame.presentationEffects);
}

} // namespace underworld::game
