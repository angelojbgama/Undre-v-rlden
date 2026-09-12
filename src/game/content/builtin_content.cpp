#include "game/content/builtin_content.h"

#include "game/content/content_compiler.h"

#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace underworld::game::content {
namespace {

using gameplay::AttackKind;
using gameplay::AttackTimelineEventKind;
using gameplay::FacingDirection;

gameplay::DirectionalBoxes swordBoxes() { return {{{{-10, -1, 20, 18}, {-10, -27, 20, 19}, {-27, -18, 21, 18}, {6, -18, 21, 18}}}}; }
gameplay::DirectionalOffsets arrowOffsets() { return {{{{0, 3}, {0, -20}, {-12, -10}, {12, -10}}}}; }

struct DungeonTileSeed { int x; int y; const char* name; authoring::TileRole role; authoring::TileTopology topology; const char* family; };
constexpr DungeonTileSeed dungeonTiles[] = {
    {4,0,"top_cap.left",authoring::TileRole::wall,authoring::TileTopology::cap,"masonry"},{5,0,"top_cap.center",authoring::TileRole::wall,authoring::TileTopology::straightHorizontal,"masonry"},{6,0,"top_cap.right",authoring::TileRole::wall,authoring::TileTopology::cap,"masonry"},{14,1,"detail.01",authoring::TileRole::detail,authoring::TileTopology::architecturalDetail,"detail"},
    {0,2,"masonry.00",authoring::TileRole::wall,authoring::TileTopology::outerCorner,"masonry"},{1,2,"masonry.01",authoring::TileRole::wall,authoring::TileTopology::straightHorizontal,"masonry"},{2,2,"frame.nw",authoring::TileRole::corner,authoring::TileTopology::outerCorner,"masonry"},{3,2,"frame.n",authoring::TileRole::wall,authoring::TileTopology::straightHorizontal,"masonry"},{4,2,"frame.ne",authoring::TileRole::corner,authoring::TileTopology::outerCorner,"masonry"},{5,2,"masonry.05",authoring::TileRole::wall,authoring::TileTopology::straightHorizontal,"masonry"},{6,2,"masonry.06",authoring::TileRole::wall,authoring::TileTopology::straightHorizontal,"masonry"},{7,2,"masonry.07",authoring::TileRole::wall,authoring::TileTopology::straightHorizontal,"masonry"},{8,2,"masonry.08",authoring::TileRole::wall,authoring::TileTopology::straightHorizontal,"masonry"},{10,2,"masonry.10",authoring::TileRole::wall,authoring::TileTopology::straightHorizontal,"masonry"},{11,2,"masonry.11",authoring::TileRole::wall,authoring::TileTopology::straightHorizontal,"masonry"},{12,2,"masonry.12",authoring::TileRole::wall,authoring::TileTopology::straightHorizontal,"masonry"},{13,2,"masonry.13",authoring::TileRole::wall,authoring::TileTopology::straightHorizontal,"masonry"},{14,2,"masonry.14",authoring::TileRole::wall,authoring::TileTopology::outerCorner,"masonry"},
    {0,3,"masonry.15",authoring::TileRole::wall,authoring::TileTopology::straightVertical,"masonry"},{1,3,"masonry.16",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{2,3,"frame.w",authoring::TileRole::wall,authoring::TileTopology::straightVertical,"masonry"},{4,3,"frame.e",authoring::TileRole::wall,authoring::TileTopology::straightVertical,"masonry"},{5,3,"masonry.20",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{6,3,"masonry.21",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{7,3,"masonry.22",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{8,3,"masonry.23",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{9,3,"inset.nw",authoring::TileRole::opening,authoring::TileTopology::outerCorner,"architectural_detail"},{10,3,"inset.ne",authoring::TileRole::opening,authoring::TileTopology::outerCorner,"architectural_detail"},{12,3,"masonry.26",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{13,3,"masonry.27",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{14,3,"masonry.28",authoring::TileRole::wall,authoring::TileTopology::straightVertical,"masonry"},
    {0,4,"masonry.29",authoring::TileRole::wall,authoring::TileTopology::outerCorner,"masonry"},{1,4,"masonry.30",authoring::TileRole::wall,authoring::TileTopology::straightHorizontal,"masonry"},{2,4,"frame.sw",authoring::TileRole::corner,authoring::TileTopology::outerCorner,"masonry"},{3,4,"frame.s",authoring::TileRole::wall,authoring::TileTopology::straightHorizontal,"masonry"},{4,4,"frame.se",authoring::TileRole::corner,authoring::TileTopology::outerCorner,"masonry"},{7,4,"masonry.35",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{8,4,"masonry.36",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{9,4,"inset.sw",authoring::TileRole::opening,authoring::TileTopology::outerCorner,"architectural_detail"},{10,4,"inset.se",authoring::TileRole::opening,authoring::TileTopology::outerCorner,"architectural_detail"},{11,4,"masonry.39",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{12,4,"masonry.40",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{13,4,"masonry.41",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{14,4,"masonry.42",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{15,4,"masonry.43",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{16,4,"masonry.44",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{17,4,"masonry.45",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{18,4,"masonry.46",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},
    {7,5,"strip_right.top",authoring::TileRole::wall,authoring::TileTopology::cap,"architectural_detail"},{11,5,"masonry.48",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{12,5,"masonry.49",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{13,5,"masonry.50",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},{14,5,"masonry.51",authoring::TileRole::wall,authoring::TileTopology::interior,"masonry"},
    {1,6,"strip_left.top",authoring::TileRole::wall,authoring::TileTopology::cap,"architectural_detail"},{4,6,"small_frame.nw",authoring::TileRole::corner,authoring::TileTopology::outerCorner,"masonry"},{5,6,"small_frame.ne",authoring::TileRole::corner,authoring::TileTopology::outerCorner,"masonry"},{7,6,"strip_right.mid_a",authoring::TileRole::wall,authoring::TileTopology::straightVertical,"architectural_detail"},{14,6,"detail.02",authoring::TileRole::detail,authoring::TileTopology::architecturalDetail,"detail"},
    {1,7,"strip_left.mid",authoring::TileRole::wall,authoring::TileTopology::straightVertical,"architectural_detail"},{2,7,"detail.03",authoring::TileRole::detail,authoring::TileTopology::architecturalDetail,"detail"},{4,7,"small_frame.sw",authoring::TileRole::corner,authoring::TileTopology::outerCorner,"masonry"},{5,7,"small_frame.se",authoring::TileRole::corner,authoring::TileTopology::outerCorner,"masonry"},{6,7,"detail.04",authoring::TileRole::detail,authoring::TileTopology::architecturalDetail,"detail"},{7,7,"strip_right.mid_b",authoring::TileRole::wall,authoring::TileTopology::straightVertical,"architectural_detail"},
    {1,8,"strip_left.bottom",authoring::TileRole::wall,authoring::TileTopology::cap,"architectural_detail"},{7,8,"strip_right.bottom",authoring::TileRole::wall,authoring::TileTopology::cap,"architectural_detail"},{3,9,"ledge.left",authoring::TileRole::ledge,authoring::TileTopology::straightHorizontal,"ledge"},{4,9,"ledge.center",authoring::TileRole::ledge,authoring::TileTopology::straightHorizontal,"ledge"},{5,9,"ledge.right",authoring::TileRole::ledge,authoring::TileTopology::straightHorizontal,"ledge"},{3,11,"toothed.left",authoring::TileRole::ledge,authoring::TileTopology::straightHorizontal,"ledge"},{4,11,"toothed.center",authoring::TileRole::ledge,authoring::TileTopology::straightHorizontal,"ledge"},{5,11,"toothed.right",authoring::TileRole::ledge,authoring::TileTopology::straightHorizontal,"ledge"},
};

void addBuiltinDungeonSemantics(AuthoredContentPack& pack) {
    for (const auto& seed : dungeonTiles) {
        AuthoredTileSemantic value;
        value.id = simulation::DefinitionId{"tile.dungeon." + std::string(seed.name)};
        value.tilesetId = simulation::DefinitionId{"tileset.dungeon"}; value.sourceIndex = static_cast<std::uint32_t>(seed.y * 19 + seed.x);
        value.family = seed.family; value.role = seed.role; value.topology = seed.topology; value.preferredLayer = "walls";
        value.semanticConfidence = (seed.role == authoring::TileRole::detail || seed.role == authoring::TileRole::opening) ? authoring::SemanticConfidence::unverified : authoring::SemanticConfidence::probable;
        value.north = value.east = value.south = value.west = seed.family == std::string_view{"masonry"} ? authoring::EdgeProfile::masonry : authoring::EdgeProfile::unknown;
        pack.tileSemantics.push_back(std::move(value));
    }
    const auto tileAt = [&](int x, int y) { const auto index = static_cast<std::uint32_t>(y * 19 + x); for (const auto& tile : pack.tileSemantics) if (tile.sourceIndex == index) return tile.id; return simulation::DefinitionId{}; };
    const auto stamp = [&](const char* id, const char* name, std::uint32_t width, std::uint32_t height, bool atomic, std::initializer_list<std::pair<int, int>> cells) {
        AuthoredStamp value{simulation::DefinitionId{id}, name, width, height, {}, {0, 0}, false, atomic, authoring::SemanticConfidence::confirmed};
        const auto origin = *cells.begin(); for (const auto& [x, y] : cells) value.cells.push_back({x - origin.first, y - origin.second, tileAt(x, y)}); pack.stamps.push_back(std::move(value));
    };
    stamp("stamp.dungeon.masonry_frame_3x3", "Masonry Frame 3x3", 3, 3, true, {{2,2},{3,2},{4,2},{2,3},{4,3},{2,4},{3,4},{4,4}});
    stamp("stamp.dungeon.inset_2x2", "Inset 2x2", 2, 2, true, {{9,3},{10,3},{9,4},{10,4}});
    stamp("stamp.dungeon.vertical_strip_left_1x3", "Vertical Strip Left", 1, 3, true, {{1,6},{1,7},{1,8}});
    stamp("stamp.dungeon.vertical_strip_right_1x4", "Vertical Strip Right", 1, 4, true, {{7,5},{7,6},{7,7},{7,8}});
    stamp("stamp.dungeon.small_masonry_2x2", "Small Masonry 2x2", 2, 2, true, {{4,6},{5,6},{4,7},{5,7}});
    stamp("stamp.dungeon.horizontal_ledge_3x1", "Horizontal Ledge", 3, 1, true, {{3,9},{4,9},{5,9}});
    stamp("stamp.dungeon.horizontal_toothed_3x1", "Horizontal Toothed", 3, 1, true, {{3,11},{4,11},{5,11}});
    stamp("stamp.dungeon.top_cap_3x1", "Top Cap", 3, 1, true, {{4,0},{5,0},{6,0}});
}

void addBuiltinVisualContent(AuthoredContentPack& pack) {
    const auto image = [&](const char* id, const char* path) {
        pack.visualImages.push_back({{id}, presentation::VisualAssetRoot::gameAssets, path});
    };
    const auto directional = [&](const char* prefix, const char* imageId, int frameWidth,
                                int frameHeight, int frameCount, std::uint32_t duration,
                                core::PointI anchor, bool loop) {
        presentation::DirectionalAnimationRef result;
        const char* names[] = {"down", "up", "side"};
        std::optional<simulation::DefinitionId>* ids[] = {
            &result.down, &result.up, &result.side};
        for (int row = 0; row < 3; ++row) {
            const std::string animationId = std::string(prefix) + "." + names[row];
            *ids[row] = simulation::DefinitionId{animationId};
            AuthoredAnimation animationValue;
            animationValue.id = **ids[row];
            animationValue.imageId = {imageId};
            animationValue.loop = loop;
            for (int column = 0; column < frameCount; ++column) {
                animationValue.frames.push_back({{column * frameWidth, row * frameHeight,
                                                    frameWidth, frameHeight}, anchor, {}, duration, {}});
            }
            pack.animations.push_back(std::move(animationValue));
        }
        return result;
    };
    const auto objectAnimation = [&](const char* id, const char* imageId, int width, int height,
                                     int count, std::uint32_t duration, core::PointI anchor,
                                     bool loop, int startColumn = 0) {
        AuthoredAnimation value;
        value.id = {id}; value.imageId = {imageId}; value.loop = loop;
        for (int column = 0; column < count; ++column)
            value.frames.push_back({{(startColumn + column) * width, 0, width, height},
                                    anchor, {}, duration, {}});
        pack.animations.push_back(std::move(value));
    };

    image("image.enemy.soldier.idle", "Characters/Enemies/Evil_soldier/idle/evil_soldier_idle.png");
    image("image.enemy.soldier.walk", "Characters/Enemies/Evil_soldier/walking/evil_soldier_walking.png");
    image("image.enemy.soldier.attack", "Characters/Enemies/Evil_soldier/attacking/evil_soldier_attacking.png");
    image("image.enemy.soldier.death", "Characters/Enemies/Evil_soldier/death/evil_soldier_death.png");
    image("image.enemy.skull.idle", "Characters/Enemies/Skull/idle/skull_idle.png");
    image("image.enemy.skull.walk", "Characters/Enemies/Skull/walking/skull_walking.png");
    image("image.enemy.skull.attack", "Characters/Enemies/Skull/attacking/skull_attacking.png");
    image("image.enemy.skull.death", "Characters/Enemies/Skull/death/skull_death.png");
    image("image.projectile.player.arrow", "Characters/Player/attacking/arrow.png");
    image("image.projectile.skull.arrow", "Characters/Enemies/Skull/attacking/arrow.png");
    image("image.pickup.heart", "Objects/heart.png");
    image("image.pickup.money", "Objects/money.png");
    image("image.item.potion", "Objects/life_potion.png");
    image("image.object.chest", "Tileset/chest.png");
    image("image.object.crate", "Tileset/crate.png");
    image("image.object.breaking_crate", "Tileset/breaking_crate.png");
    image("image.object.vase", "Tileset/vase.png");
    image("image.object.breaking_vase", "Tileset/breaking_vase.png");
    image("image.object.stone_block", "Tileset/block.png");
    image("image.object.stone_block_2", "Tileset/block_2.png");
    image("image.object.block_destroyed", "Tileset/block_destroied.png");
    image("image.object.fire_block", "Tileset/fire_block.png");
    image("image.object.fire_block_with_fire", "Tileset/fire_block_with_fire.png");
    image("image.object.fire_block_destroyed", "Tileset/fire_block_destroied.png");

    const auto addStatic = [&](const char* id, const char* imageId, core::PointI anchor) {
        pack.staticSprites.push_back({{id}, {imageId}, std::nullopt, anchor});
    };
    addStatic("visual.projectile.player.arrow", "image.projectile.player.arrow", {8, 8});
    addStatic("visual.projectile.skull.arrow", "image.projectile.skull.arrow", {8, 8});
    addStatic("visual.pickup.heart", "image.pickup.heart", {8, 8});
    addStatic("visual.pickup.money", "image.pickup.money", {8, 8});
    addStatic("visual.item.life_potion", "image.item.potion", {8, 8});
    addStatic("visual.item.training_armor", "image.item.potion", {8, 8});
    addStatic("visual.item.power_charm", "image.item.potion", {8, 8});

    const auto soldierIdle = directional("anim.enemy.soldier.idle", "image.enemy.soldier.idle", 32, 32, 2, 30, {16, 31}, true);
    const auto soldierWalk = directional("anim.enemy.soldier.walk", "image.enemy.soldier.walk", 32, 32, 4, 8, {16, 31}, true);
    const auto soldierDeath = directional("anim.enemy.soldier.death", "image.enemy.soldier.death", 32, 32, 2, 8, {16, 31}, false);
    const auto soldierAttack = directional("anim.enemy.soldier.attack", "image.enemy.soldier.attack", 48, 48, 4, 6, {24, 31}, false);
    const auto skullIdle = directional("anim.enemy.skull.idle", "image.enemy.skull.idle", 32, 32, 2, 30, {16, 31}, true);
    const auto skullWalk = directional("anim.enemy.skull.walk", "image.enemy.skull.walk", 32, 32, 4, 8, {16, 31}, true);
    const auto skullDeath = directional("anim.enemy.skull.death", "image.enemy.skull.death", 32, 32, 2, 8, {16, 31}, false);
    const auto skullAttack = directional("anim.enemy.skull.attack", "image.enemy.skull.attack", 32, 32, 2, 8, {16, 31}, false);
    pack.enemyVisuals.push_back({{"visual.enemy.evil_soldier"}, soldierIdle, soldierWalk,
                                 std::nullopt, soldierDeath, std::nullopt,
                                 {{{"visual.action.soldier.sword"}, soldierAttack}}});
    pack.enemyVisuals.push_back({{"visual.enemy.skull"}, skullIdle, skullWalk,
                                 std::nullopt, skullDeath, std::nullopt,
                                 {{{"visual.action.skull.arrow"}, skullAttack}}});

    objectAnimation("anim.object.chest.idle", "image.object.chest", 16, 32, 1, 1, {8, 31}, true);
    objectAnimation("anim.object.chest.opened", "image.object.chest", 16, 32, 5, 4, {8, 31}, false);
    objectAnimation("anim.object.crate.idle", "image.object.breaking_crate", 32, 32, 1, 1,
                    {16, 31}, true);
    objectAnimation("anim.object.crate.destroying", "image.object.breaking_crate", 32, 32,
                    7, 4, {16, 31}, false, 1);
    objectAnimation("anim.object.vase.idle", "image.object.vase", 16, 32, 1, 1, {8, 31}, true);
    objectAnimation("anim.object.vase.destroying", "image.object.breaking_vase", 32, 32, 6, 4, {16, 31}, false);
    objectAnimation("anim.object.stone_block.idle", "image.object.stone_block", 16, 32, 1, 1, {8, 31}, true);
    objectAnimation("anim.object.stone_block_2.idle", "image.object.stone_block_2", 16, 32, 1, 1, {8, 31}, true);
    objectAnimation("anim.object.stone_block.destroyed", "image.object.block_destroyed", 16, 32, 1, 1, {8, 31}, true);
    objectAnimation("anim.object.fire_block.inactive", "image.object.fire_block", 16, 32, 1, 1, {8, 31}, true);
    objectAnimation("anim.object.fire_block.active", "image.object.fire_block_with_fire", 16, 32, 4, 4, {8, 31}, true);
    objectAnimation("anim.object.fire_block.destroyed", "image.object.fire_block_destroyed", 16, 32, 1, 1, {8, 31}, true);
    pack.objectVisuals.push_back({{"visual.object.chest"}, {"anim.object.chest.idle"}, {"anim.object.chest.opened"}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
    pack.objectVisuals.push_back({{"visual.object.crate"}, {"anim.object.crate.idle"}, std::nullopt, {"anim.object.crate.destroying"}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
    pack.objectVisuals.push_back({{"visual.object.vase"}, {"anim.object.vase.idle"}, std::nullopt, {"anim.object.vase.destroying"}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
    pack.objectVisuals.push_back({{"visual.object.stone_block"}, {"anim.object.stone_block.idle"}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, {"anim.object.stone_block.destroyed"}});
    pack.objectVisuals.push_back({{"visual.object.stone_block_2"}, {"anim.object.stone_block_2.idle"}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, {"anim.object.stone_block.destroyed"}});
    pack.objectVisuals.push_back({{"visual.object.fire_block"}, {"anim.object.fire_block.inactive"}, std::nullopt, std::nullopt, {"anim.object.fire_block.inactive"}, {"anim.object.fire_block.active"}, std::nullopt, std::nullopt, std::nullopt, {"anim.object.fire_block.destroyed"}});
    pack.objectVisuals.push_back({{"visual.object.bank_access"}, {"anim.object.chest.idle"}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
}

} // namespace

AuthoredContentPack makeBuiltinAuthoredContent() {
    AuthoredContentPack pack;
    pack.tilesets.push_back({{"tileset.dungeon"}, "Dungeon", "Tileset/tileset.png", 16, 19, 12});
    // Temporary development curve; final game balance is intentionally undecided.
    pack.playerProgressions.push_back({{"progression.player.default"}, {5}, {0, 100, 250}});
    pack.projectiles = {
        {{"projectile.player.arrow"}, {"visual.projectile.player.arrow"}, FacingDirection::up, 4, 120, 6, 6, arrowOffsets()},
        {{"projectile.skull.arrow"}, {"visual.projectile.skull.arrow"}, FacingDirection::right, 4, 120, 6, 6, arrowOffsets()}};
    pack.attacks = {
        {{"attack.player.sword"}, AttackKind::meleeHitbox, {1, 32}, 24, 0, 0, 27, {"visual.player.sword"}, swordBoxes(), std::nullopt, {{6, AttackTimelineEventKind::activateHitbox}, {18, AttackTimelineEventKind::deactivateHitbox}}},
        {{"attack.player.bow"}, AttackKind::projectile, {1, 32}, 16, 0, 0, 512, {"visual.player.bow"}, std::nullopt, {"projectile.player.arrow"}, {{8, AttackTimelineEventKind::spawnProjectile}}},
        {{"attack.soldier.sword"}, AttackKind::meleeHitbox, {1, 7}, 24, 45, 0, 27, {"visual.action.soldier.sword"}, swordBoxes(), std::nullopt, {{6, AttackTimelineEventKind::activateHitbox}, {18, AttackTimelineEventKind::deactivateHitbox}}},
        {{"attack.skull.arrow"}, AttackKind::projectile, {1, 5}, 16, 60, 0, 120, {"visual.action.skull.arrow"}, std::nullopt, {"projectile.skull.arrow"}, {{8, AttackTimelineEventKind::spawnProjectile}}}};
    pack.behaviors = {{{"behavior.soldier.melee"}, 100, 132, 60, 90}, {{"behavior.skull.ranged"}, 150, 184, 75, 90}};
    pack.enemies = {
        {{"enemy.evil_soldier"}, {"visual.enemy.evil_soldier"}, {"behavior.soldier.melee"}, gameplay::Faction::enemy, 3, 256, {-5, -8, 10, 8}, {-7, -22, 14, 22}, {{"attack.soldier.sword"}}, {{"reward.enemy.evil_soldier"}}},
        {{"enemy.skull"}, {"visual.enemy.skull"}, {"behavior.skull.ranged"}, gameplay::Faction::enemy, 3, 192, {-5, -8, 10, 8}, {-7, -22, 14, 22}, {{"attack.skull.arrow"}}, {{"reward.enemy.skull"}}}};
    pack.items = {{{"item.life_potion"}, {"visual.item.life_potion"}, gameplay::ItemCategory::consumable, 66, gameplay::ItemUseDefinition{gameplay::ItemUseKind::restoreHealth, 2}}};
    pack.items.push_back({{"item.training_armor"}, {"visual.item.training_armor"}, gameplay::ItemCategory::equipment, 1, std::nullopt, AuthoredEquipment{AuthoredEquipmentSlot::armor, {2, 0}}});
    pack.items.push_back({{"item.power_charm"}, {"visual.item.power_charm"}, gameplay::ItemCategory::equipment, 1, std::nullopt, AuthoredEquipment{AuthoredEquipmentSlot::accessory, {0, 1}}});
    pack.objects = {
        {{"object.chest"}, {"visual.object.chest"}, gameplay::ObjectInteractionDefinition{{-14, -18, 28, 22}}, gameplay::ObjectContainerDefinition{5}, std::nullopt},
        {{"object.crate"}, {"visual.object.crate"}, std::nullopt, std::nullopt, gameplay::ObjectDestructibleDefinition{2, {-8, -24, 16, 24}, 28}},
        {{"object.vase"}, {"visual.object.vase"}, std::nullopt, std::nullopt, gameplay::ObjectDestructibleDefinition{1, {-8, -24, 16, 24}, 24}},
        {{"object.stone_block"}, {"visual.object.stone_block"}, std::nullopt, std::nullopt, gameplay::ObjectDestructibleDefinition{2, {-8, -24, 16, 24}, 1}},
        {{"object.stone_block_2"}, {"visual.object.stone_block_2"}, std::nullopt, std::nullopt, gameplay::ObjectDestructibleDefinition{2, {-8, -24, 16, 24}, 1}},
        {{"object.fire_block"}, {"visual.object.fire_block"}, gameplay::ObjectInteractionDefinition{{-8, -14, 16, 14}}, std::nullopt, gameplay::ObjectDestructibleDefinition{2, {-8, -24, 16, 24}, 1}, std::nullopt, std::nullopt, gameplay::ObjectActivationDefinition{gameplay::ObjectActivationMode::interactToggle, false, std::nullopt}},
        {{"object.bank_access"}, {"visual.object.bank_access"}, gameplay::ObjectInteractionDefinition{{-14, -18, 28, 22}}, std::nullopt, std::nullopt, AuthoredObjectBankAccess{}}};
    pack.pickups = {
        {{"pickup.heart"}, {"visual.pickup.heart"}, {-5, -5, 10, 10}, AuthoredHealthPickup{2}},
        {{"pickup.money"}, {"visual.pickup.money"}, {-5, -5, 10, 10}, AuthoredCurrencyPickup{1}},
        {{"pickup.life_potion"}, {"visual.item.life_potion"}, {-5, -5, 10, 10}, AuthoredItemPickup{{"item.life_potion"}, 1}},
        {{"pickup.training_armor"}, {"visual.item.training_armor"}, {-5, -5, 10, 10}, AuthoredItemPickup{{"item.training_armor"}, 1}},
        {{"pickup.power_charm"}, {"visual.item.power_charm"}, {-5, -5, 10, 10}, AuthoredItemPickup{{"item.power_charm"}, 1}}};
    pack.rewardProfiles = {
        {{"reward.enemy.evil_soldier"}, 60, {{{"pickup.money"}, 10000, 1, 2}, {{"pickup.life_potion"}, 2000, 1, 1}}},
        {{"reward.enemy.skull"}, 40, {{{"pickup.money"}, 10000, 1, 1}, {{"pickup.heart"}, 2500, 1, 1}}}};
    pack.rewardGrants = {{{"reward.quest.scholar.path"}, 40, 25, {{{"item.life_potion"}, 2}, {{"item.training_armor"}, 1}}}};
    pack.shops = {{{"shop.development.general"}, {{{"item.life_potion"}, 25, 10}, {{"item.training_armor"}, 150, 60}, {{"item.power_charm"}, 200, 80}}}};
    pack.presentationEffects = {
        {{"effect.player.hit"}, presentation::PresentationEffectLifetime::transient, 6, 50,
         presentation::CameraShakeDefinition{2},
         presentation::ColorOverlayDefinition{{220, 32, 32, 76}, presentation::PresentationOverlayMode::linearFadeOut, 0, presentation::PresentationCompositionLayer::world},
         std::nullopt, std::nullopt},
        {{"effect.world.heavy_impact"}, presentation::PresentationEffectLifetime::transient, 24, 80,
         presentation::CameraShakeDefinition{6},
         presentation::ColorOverlayDefinition{{255, 255, 255, 100}, presentation::PresentationOverlayMode::linearFadeOut, 0, presentation::PresentationCompositionLayer::final},
         std::nullopt, std::nullopt},
        {{"effect.environment.dark"}, presentation::PresentationEffectLifetime::persistent, 0, 10,
         std::nullopt,
         presentation::ColorOverlayDefinition{{0, 0, 24, 32}, presentation::PresentationOverlayMode::constant, 0, presentation::PresentationCompositionLayer::world},
         presentation::VisionMaskDefinition{48, 72, 220, {0, 0, 0, 255}}, std::nullopt}};
    pack.npcVisuals = {{{"visual.npc.guard"}, {70, 150, 240, 255}, {}}, {{"visual.npc.scholar"}, {220, 180, 70, 255}, {}}, {{"visual.npc.merchant"}, {120, 210, 120, 255}, {}}};
    pack.npcs = {
        {{"npc.guard"}, {"visual.npc.guard"}, {{-14, -28, 28, 22}, true}, {"dialogue.guard.greeting"}, {"npc", "guard"}},
        {{"npc.scholar"}, {"visual.npc.scholar"}, {{-14, -28, 28, 22}, true}, {"dialogue.scholar.greeting"}, {"npc", "scholar"}},
        {{"npc.merchant"}, {"visual.npc.merchant"}, {{-14, -28, 28, 22}, true}, {"dialogue.merchant.greeting"}, {"npc", "merchant", "shop"}}};
    AuthoredDialogue guardDialogue;
    guardDialogue.id = {"dialogue.guard.greeting"}; guardDialogue.entryNodeId = {"guard.entry"};
    guardDialogue.nodes.push_back({{"guard.entry"}, "Guard", {"Halt, traveler.", "The gallery lies beyond the eastern gate."}, {"guard.response"}, {}});
    guardDialogue.nodes.push_back({{"guard.response"}, "Guard", {"Keep your blade ready."}, {}, {}});
    pack.dialogues.push_back(std::move(guardDialogue));
    AuthoredDialogue scholarDialogue;
    scholarDialogue.id = {"dialogue.scholar.greeting"}; scholarDialogue.entryNodeId = {"scholar.entry"};
    AuthoredDialogueChoice ask;
    ask.label = "Ask about the dungeon"; ask.targetNodeId = {"scholar.left"};
    ask.actions.push_back({gameplay::dialogue::DialogueActionKind::setFlag, {"dialogue.scholar.asked"}});
    ask.actions.push_back({gameplay::dialogue::DialogueActionKind::startQuest, {"quest.scholar.path"}});
    AuthoredDialogueChoice farewell{"Say farewell", {"scholar.right"}, {{gameplay::dialogue::DialogueConditionKind::flagNotSet, {"dialogue.scholar.asked"}}}, {}};
    AuthoredDialogueChoice recall{"Recall the lesson", {"scholar.left"}, {{gameplay::dialogue::DialogueConditionKind::flagSet, {"dialogue.scholar.asked"}}}, {}};
    scholarDialogue.nodes.push_back({{"scholar.entry"}, "Scholar", {"The old stones remember every footstep."}, {}, {std::move(ask), std::move(farewell), std::move(recall)}});
    scholarDialogue.nodes.push_back({{"scholar.left"}, "Scholar", {"Study the walls, but trust the path beneath your feet."}, {}, {}});
    scholarDialogue.nodes.push_back({{"scholar.right"}, "Scholar", {"Then walk carefully, friend."}, {}, {}});
    pack.dialogues.push_back(std::move(scholarDialogue));
    AuthoredDialogue merchantDialogue;
    merchantDialogue.id = {"dialogue.merchant.greeting"}; merchantDialogue.entryNodeId = {"merchant.entry"};
    AuthoredDialogueChoice trade{"Trade", {"merchant.trade"}, {}, {{gameplay::dialogue::DialogueActionKind::openShop, {"shop.development.general"}}}};
    AuthoredDialogueChoice leave{"Leave", {"merchant.leave"}, {}, {}};
    merchantDialogue.nodes.push_back({{"merchant.entry"}, "Merchant", {"Looking for supplies?"}, {}, {std::move(trade), std::move(leave)}});
    merchantDialogue.nodes.push_back({{"merchant.trade"}, "Merchant", {"Take a look."}, {}, {}});
    merchantDialogue.nodes.push_back({{"merchant.leave"}, "Merchant", {"Safe travels."}, {}, {}});
    pack.dialogues.push_back(std::move(merchantDialogue));
    AuthoredQuest scholarQuest;
    scholarQuest.id = {"quest.scholar.path"}; scholarQuest.title = "The Scholar's Path";
    scholarQuest.objectives.push_back({{"quest.scholar.kill"}, gameplay::quests::QuestObjectiveKind::kill, {"enemy.evil_soldier"}, 1, "Defeat an evil soldier."});
    scholarQuest.objectives.push_back({{"quest.scholar.pickup"}, gameplay::quests::QuestObjectiveKind::pickup, {"pickup.heart"}, 1, "Find a heart pickup."});
    scholarQuest.tags = {"story", "scholar"};
    scholarQuest.rewardGrantId = {"reward.quest.scholar.path"};
    pack.quests.push_back(std::move(scholarQuest));
    pack.authoringDescriptors = {
        {{"enemy.evil_soldier"}, "Evil Soldier", AuthoringCategory::enemy, {"melee", "hostile"}}, {{"enemy.skull"}, "Skull", AuthoringCategory::enemy, {"ranged", "hostile"}},
        {{"object.chest"}, "Chest", AuthoringCategory::object, {"container", "interactable"}}, {{"object.crate"}, "Crate", AuthoringCategory::object, {"destructible", "prop"}},
        {{"object.vase"}, "Vase", AuthoringCategory::object, {"destructible", "prop"}},
        {{"object.stone_block"}, "Stone Block", AuthoringCategory::object, {"destructible", "prop", "stone"}},
        {{"object.stone_block_2"}, "Stone Block Variant", AuthoringCategory::object, {"destructible", "prop", "stone"}},
        {{"object.fire_block"}, "Fire Block", AuthoringCategory::object, {"destructible", "prop", "fire"}},
        {{"pickup.heart"}, "Heart", AuthoringCategory::pickup, {"health"}}, {{"pickup.money"}, "Money", AuthoringCategory::pickup, {"currency"}}, {{"pickup.life_potion"}, "Life Potion", AuthoringCategory::pickup, {"item", "consumable"}},
        {{"npc.guard"}, "Guard", AuthoringCategory::npc, {"npc", "dialogue"}}, {{"npc.scholar"}, "Scholar", AuthoringCategory::npc, {"npc", "dialogue"}}, {{"npc.merchant"}, "Merchant", AuthoringCategory::npc, {"npc", "merchant", "shop"}},
        {{"reward.enemy.evil_soldier"}, "Evil Soldier Reward", AuthoringCategory::rewardProfile, {"reward", "enemy"}},
        {{"reward.enemy.skull"}, "Skull Reward", AuthoringCategory::rewardProfile, {"reward", "enemy"}},
        {{"reward.quest.scholar.path"}, "Scholar Quest Reward", AuthoringCategory::rewardGrant, {"reward", "quest"}},
        {{"shop.development.general"}, "Development General Shop", AuthoringCategory::shop, {"shop", "development", "general"}},
        {{"item.life_potion"}, "Life Potion", AuthoringCategory::item, {"item", "consumable"}},
        {{"item.training_armor"}, "Training Armor", AuthoringCategory::item, {"item", "equipment"}},
        {{"item.power_charm"}, "Power Charm", AuthoringCategory::item, {"item", "equipment"}},
        {{"object.bank_access"}, "Bank Access", AuthoringCategory::object, {"bank", "storage"}}};
    addBuiltinDungeonSemantics(pack);
    addBuiltinVisualContent(pack);
    return pack;
}

GameContentRegistry compileBuiltinContentOrThrow() {
    const auto result = compileContent(makeBuiltinAuthoredContent());
    if (!result) {
        std::string message = "builtin content compilation failed";
        for (const auto& diagnostic : result.report.diagnostics) if (diagnostic.severity == ContentDiagnosticSeverity::error) message += " [" + diagnostic.code + "] " + diagnostic.message;
        throw std::runtime_error(message);
    }
    return *result.registry;
}

} // namespace underworld::game::content
