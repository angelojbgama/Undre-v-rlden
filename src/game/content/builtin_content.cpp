#include "game/content/builtin_content.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace underworld::game::content {
namespace {

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
        const char* names[] = {"down", "up", "left", "right"};
        const int rows[] = {0, 1, 2, 2};
        const bool mirrored[] = {false, false, false, true};
        std::optional<simulation::DefinitionId>* ids[] = {
            &result.down, &result.up, &result.left, &result.right};
        for (int binding = 0; binding < 4; ++binding) {
            const std::string animationId =
                std::string(prefix) + "." + names[binding];
            *ids[binding] = simulation::DefinitionId{animationId};
            AuthoredAnimation animationValue;
            animationValue.id = **ids[binding];
            animationValue.imageId = {imageId};
            animationValue.loop = loop;
            for (int column = 0; column < frameCount; ++column) {
                animationValue.frames.push_back({
                    {column * frameWidth, rows[binding] * frameHeight,
                     frameWidth, frameHeight},
                    anchor, {}, duration, {}, false, {}});
                animationValue.frames.back().flipX = mirrored[binding];
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
                                    anchor, {}, duration, {}, false, {}});
        pack.animations.push_back(std::move(value));
    };

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
    addStatic("visual.pickup.heart", "image.pickup.heart", {8, 8});
    addStatic("visual.pickup.money", "image.pickup.money", {8, 8});
    addStatic("visual.item.life_potion", "image.item.potion", {8, 8});
    addStatic("visual.item.training_armor", "image.item.potion", {8, 8});
    addStatic("visual.item.power_charm", "image.item.potion", {8, 8});

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
    pack.objectVisuals.push_back({{"visual.object.chest"}, {"anim.object.chest.idle"}, {"anim.object.chest.opened"}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
    pack.objectVisuals.push_back({{"visual.object.crate"}, {"anim.object.crate.idle"}, std::nullopt, {"anim.object.crate.destroying"}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
    pack.objectVisuals.push_back({{"visual.object.vase"}, {"anim.object.vase.idle"}, std::nullopt, {"anim.object.vase.destroying"}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
    pack.objectVisuals.push_back({{"visual.object.stone_block"}, {"anim.object.stone_block.idle"}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, {"anim.object.stone_block.destroyed"}, std::nullopt});
    pack.objectVisuals.push_back({{"visual.object.stone_block_2"}, {"anim.object.stone_block_2.idle"}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, {"anim.object.stone_block.destroyed"}, std::nullopt});
    pack.objectVisuals.push_back({{"visual.object.fire_block"}, {"anim.object.fire_block.inactive"}, std::nullopt, std::nullopt, {"anim.object.fire_block.inactive"}, {"anim.object.fire_block.active"}, std::nullopt, std::nullopt, std::nullopt, {"anim.object.fire_block.destroyed"}, std::nullopt});
    pack.objectVisuals.push_back({{"visual.object.bank_access"}, {"anim.object.chest.idle"}, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt});

    // The built-in Player now uses the same authored visual pipeline as
    // workspace Players. These definitions form the fallback base layer.
    image("image.player.idle", "Characters/Player/idle/player_idle.png");
    image("image.player.walk", "Characters/Player/walking/player_walking.png");
    image("image.player.sword", "Characters/Player/attacking/player_attacking.png");
    image("image.player.bow", "Characters/Player/attacking/player_attacking_bow.png");
    image("image.player.hurt", "Characters/Player/death/player_death.png");

    const auto playerIdle = directional(
        "anim.player.idle", "image.player.idle", 32, 32, 2, 30,
        {16, 31}, true);
    const auto playerWalk = directional(
        "anim.player.walk", "image.player.walk", 32, 32, 4, 8,
        {16, 31}, true);
    const auto playerSword = directional(
        "anim.player.sword", "image.player.sword", 48, 48, 4,
        gameplay::makePlayerSwordAttackDefinition().totalTicks / 4,
        {24, 31}, false);
    // This sword source row faces RIGHT. Swap the generic lateral
    // convention for this pair only.
    auto& playerSwordLeft = pack.animations[pack.animations.size() - 2];
    auto& playerSwordRight = pack.animations.back();
    for (auto& frame : playerSwordLeft.frames) frame.flipX = true;
    for (auto& frame : playerSwordRight.frames) frame.flipX = false;
    const auto playerBow = directional(
        "anim.player.bow", "image.player.bow", 32, 32, 2,
        gameplay::makePlayerBowAttackDefinition().totalTicks / 2,
        {16, 31}, false);
    const auto playerHurt = directional(
        "anim.player.hurt", "image.player.hurt", 32, 32, 2, 4,
        {16, 31}, false);

    AuthoredPlayerVisual playerVisual;
    playerVisual.id = {"visual.player.hero"};
    playerVisual.idle = playerIdle;
    playerVisual.walk = playerWalk;
    playerVisual.hurt = playerHurt;
    playerVisual.actions.push_back({"sword", playerSword});
    playerVisual.actions.push_back({"bow", playerBow});
    pack.playerVisuals.push_back(std::move(playerVisual));
}

} // namespace

AuthoredContentPack makeBuiltinAuthoredContent() {
    AuthoredContentPack pack;
    pack.tilesets.push_back({{"tileset.dungeon"}, "Dungeon", "Tileset/tileset.png", 16, 19, 12, {}});
    // Temporary development curve; final game balance is intentionally undecided.
    pack.playerProgressions.push_back({{"progression.player.default"}, {5}, {0, 100, 250}});
    pack.players.push_back({{"player.hero"}, {"visual.player.hero"},
                            {"progression.player.default"}, std::nullopt});
    // Combat content (attacks, projectiles, enemies, behaviors, enemy
    // visuals, combat quests) is authored content. The fallback base layer
    // stays self-consistent without it.
    pack.items = {{{"item.life_potion"}, {"visual.item.life_potion"}, gameplay::ItemCategory::consumable, 66, gameplay::ItemUseDefinition{gameplay::ItemUseKind::restoreHealth, 2}}};
    pack.items.push_back({{"item.training_armor"}, {"visual.item.training_armor"}, gameplay::ItemCategory::equipment, 1, std::nullopt, AuthoredEquipment{AuthoredEquipmentSlot::armor, {2, 0}}});
    pack.items.push_back({{"item.power_charm"}, {"visual.item.power_charm"}, gameplay::ItemCategory::equipment, 1, std::nullopt, AuthoredEquipment{AuthoredEquipmentSlot::accessory, {0, 1}}});
    pack.objects = {
        {{"object.chest"}, {"visual.object.chest"}, gameplay::ObjectInteractionDefinition{{-14, -18, 28, 22}}, gameplay::ObjectContainerDefinition{5}, std::nullopt},
        {{"object.crate"}, {"visual.object.crate"}, std::nullopt, std::nullopt, gameplay::ObjectDestructibleDefinition{2, {-8, -24, 16, 24}, 28, 8, std::nullopt, false}},
        {{"object.vase"}, {"visual.object.vase"}, std::nullopt, std::nullopt, gameplay::ObjectDestructibleDefinition{1, {-8, -24, 16, 24}, 24, 8, std::nullopt, false}},
        {{"object.stone_block"}, {"visual.object.stone_block"}, std::nullopt, std::nullopt, gameplay::ObjectDestructibleDefinition{2, {-8, -24, 16, 24}, 1, 8, std::nullopt, true}},
        {{"object.stone_block_2"}, {"visual.object.stone_block_2"}, std::nullopt, std::nullopt, gameplay::ObjectDestructibleDefinition{2, {-8, -24, 16, 24}, 1, 8, std::nullopt, true}},
        {{"object.fire_block"}, {"visual.object.fire_block"}, gameplay::ObjectInteractionDefinition{{-8, -14, 16, 14}}, std::nullopt, gameplay::ObjectDestructibleDefinition{2, {-8, -24, 16, 24}, 1, 8, std::nullopt, true}, std::nullopt, std::nullopt, gameplay::ObjectActivationDefinition{gameplay::ObjectActivationMode::interactToggle, false, std::nullopt}},
        {{"object.bank_access"}, {"visual.object.bank_access"}, gameplay::ObjectInteractionDefinition{{-14, -18, 28, 22}}, std::nullopt, std::nullopt, AuthoredObjectBankAccess{}}};
    pack.pickups = {
        {{"pickup.heart"}, {"visual.pickup.heart"}, {-5, -5, 10, 10}, AuthoredHealthPickup{2}},
        {{"pickup.money"}, {"visual.pickup.money"}, {-5, -5, 10, 10}, AuthoredCurrencyPickup{1}},
        {{"pickup.life_potion"}, {"visual.item.life_potion"}, {-5, -5, 10, 10}, AuthoredItemPickup{{"item.life_potion"}, 1}},
        {{"pickup.training_armor"}, {"visual.item.training_armor"}, {-5, -5, 10, 10}, AuthoredItemPickup{{"item.training_armor"}, 1}},
        {{"pickup.power_charm"}, {"visual.item.power_charm"}, {-5, -5, 10, 10}, AuthoredItemPickup{{"item.power_charm"}, 1}}};
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
    AuthoredDialogueChoice farewell{"Say farewell", {"scholar.right"}, {}, {}};
    scholarDialogue.nodes.push_back({{"scholar.entry"}, "Scholar", {"The old stones remember every footstep."}, {}, {std::move(farewell)}});
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
    scholarQuest.objectives.push_back({{"quest.scholar.pickup"}, gameplay::quests::QuestObjectiveKind::pickup, {"pickup.heart"}, 1, "Find a heart pickup."});
    scholarQuest.tags = {"story", "scholar"};
    scholarQuest.rewardGrantId = {"reward.quest.scholar.path"};
    pack.quests.push_back(std::move(scholarQuest));
    pack.authoringDescriptors = {
        {{"object.chest"}, "Chest", AuthoringCategory::object, {"container", "interactable"}}, {{"object.crate"}, "Crate", AuthoringCategory::object, {"destructible", "prop"}},
        {{"object.vase"}, "Vase", AuthoringCategory::object, {"destructible", "prop"}},
        {{"object.stone_block"}, "Stone Block", AuthoringCategory::object, {"destructible", "prop", "stone"}},
        {{"object.stone_block_2"}, "Stone Block Variant", AuthoringCategory::object, {"destructible", "prop", "stone"}},
        {{"object.fire_block"}, "Fire Block", AuthoringCategory::object, {"destructible", "prop", "fire"}},
        {{"pickup.heart"}, "Heart", AuthoringCategory::pickup, {"health"}}, {{"pickup.money"}, "Money", AuthoringCategory::pickup, {"currency"}}, {{"pickup.life_potion"}, "Life Potion", AuthoringCategory::pickup, {"item", "consumable"}},
        {{"npc.guard"}, "Guard", AuthoringCategory::npc, {"npc", "dialogue"}}, {{"npc.scholar"}, "Scholar", AuthoringCategory::npc, {"npc", "dialogue"}}, {{"npc.merchant"}, "Merchant", AuthoringCategory::npc, {"npc", "merchant", "shop"}},
        {{"reward.quest.scholar.path"}, "Scholar Quest Reward", AuthoringCategory::rewardGrant, {"reward", "quest"}},
        {{"shop.development.general"}, "Development General Shop", AuthoringCategory::shop, {"shop", "development", "general"}},
        {{"item.life_potion"}, "Life Potion", AuthoringCategory::item, {"item", "consumable"}},
        {{"item.training_armor"}, "Training Armor", AuthoringCategory::item, {"item", "equipment"}},
        {{"item.power_charm"}, "Power Charm", AuthoringCategory::item, {"item", "equipment"}},
        {{"object.bank_access"}, "Bank Access", AuthoringCategory::object, {"bank", "storage"}}};
    addBuiltinDungeonSemantics(pack);
    addBuiltinVisualContent(pack);

    // HUD as authored UI content (docs/UI_ENGINE.md proof 1a, extended): the
    // full legacy HUD bar — hearts, gold, ammo, quickslots — reproduces the
    // hardcoded draw pixel-for-pixel and becomes Studio-editable. "MAP:" and
    // the last-event label stay runtime-drawn (world state, not player state).
    pack.visualImages.push_back(
        {{"img.hud.heart" }, presentation::VisualAssetRoot::gameAssets, "Icons/heart_complete.png"});
    pack.staticSprites.push_back(
        {{"spr.hud.heart" }, { "img.hud.heart" }, std::nullopt, core::PointI{0, 0}});
    pack.visualImages.push_back(
        {{"img.hud.money" }, presentation::VisualAssetRoot::gameAssets, "Icons/money.png"});
    pack.staticSprites.push_back(
        {{"spr.hud.money" }, { "img.hud.money" }, std::nullopt, core::PointI{0, 0}});
    ui::ScreenDefinition hudScreen;
    hudScreen.id = simulation::DefinitionId{"screen.hud"};
    hudScreen.kind = ui::ScreenKind::hud;
    ui::NodeDefinition hudRoot;
    hudRoot.id = "hud.root";
    hudRoot.component = ui::ComponentKind::group;
    const auto hudChild = [&hudRoot](ui::NodeDefinition node) {
        hudRoot.children.push_back(std::move(node));
    };
    ui::NodeDefinition topBar;
    topBar.id = "hud.topBar";
    topBar.component = ui::ComponentKind::panel;
    topBar.layout.width = 272;
    topBar.layout.height = 14;
    topBar.background = core::ColorRGBA8{8, 10, 16, 220};
    hudChild(std::move(topBar));
    ui::NodeDefinition hearts;
    hearts.id = "hud.health";
    hearts.component = ui::ComponentKind::meter;
    hearts.layout.offsetX = 3;
    hearts.layout.offsetY = 2;
    ui::MeterDefinition healthMeter;
    healthMeter.mode = ui::MeterMode::segmented;
    healthMeter.segmentValue = 1;
    healthMeter.sprites.full = simulation::DefinitionId{"spr.hud.heart"};
    healthMeter.spacing = 1;
    healthMeter.emptyRect = ui::MeterEmptyRect{core::ColorRGBA8{54, 30, 38, 255}, 0, 1, 9, 8};
    hearts.meter = healthMeter;
    hearts.bindings.push_back({"value", ui::BindingPath::playerHealthCurrent});
    hearts.bindings.push_back({"maximum", ui::BindingPath::playerHealthMax});
    hudChild(std::move(hearts));
    ui::NodeDefinition moneyIcon;
    moneyIcon.id = "hud.gold.icon";
    moneyIcon.component = ui::ComponentKind::image;
    moneyIcon.spriteId = simulation::DefinitionId{"spr.hud.money"};
    moneyIcon.layout.offsetX = 68;
    moneyIcon.layout.offsetY = 2;
    hudChild(std::move(moneyIcon));
    ui::NodeDefinition goldText;
    goldText.id = "hud.gold.text";
    goldText.component = ui::ComponentKind::text;
    goldText.layout.offsetX = 79;
    goldText.layout.offsetY = 2;
    goldText.bindings.push_back({"text", ui::BindingPath::playerGold});
    hudChild(std::move(goldText));
    // Ammo (bottom right): hidden unless the player carries the authored
    // attack ammo; the icon is bound to the carried item's visual.
    const auto gated = [](ui::NodeDefinition node) {
        node.layout.visible = false;
        ui::StateDefinition present;
        present.id = "carrying";
        present.condition = ui::StateCondition{
            ui::BindingPath::playerAmmoPresent, ui::ConditionOperator::equal, 1};
        present.visual.visible = true;
        node.states.push_back(std::move(present));
        return node;
    };
    ui::NodeDefinition ammoIcon = gated(ui::NodeDefinition{});
    ammoIcon.id = "hud.ammo.icon";
    ammoIcon.component = ui::ComponentKind::image;
    ammoIcon.layout.offsetX = 246;
    ammoIcon.layout.offsetY = 199;
    ammoIcon.bindings.push_back({"icon", ui::BindingPath::playerAmmoIcon});
    hudChild(std::move(ammoIcon));
    ui::NodeDefinition ammoPrefix = gated(ui::NodeDefinition{});
    ammoPrefix.id = "hud.ammo.prefix";
    ammoPrefix.component = ui::ComponentKind::text;
    ammoPrefix.layout.offsetX = 226;
    ammoPrefix.layout.offsetY = 209;
    ammoPrefix.text = "x";
    hudChild(std::move(ammoPrefix));
    ui::NodeDefinition ammoCount = gated(ui::NodeDefinition{});
    ammoCount.id = "hud.ammo.count";
    ammoCount.component = ui::ComponentKind::text;
    ammoCount.layout.offsetX = 233;
    ammoCount.layout.offsetY = 209;
    ammoCount.bindings.push_back({"text", ui::BindingPath::playerAmmoAmount});
    hudChild(std::move(ammoCount));
    ui::NodeDefinition bottomBar;
    bottomBar.id = "hud.bottomBar";
    bottomBar.component = ui::ComponentKind::panel;
    bottomBar.layout.offsetY = 194;
    bottomBar.layout.width = 272;
    bottomBar.layout.height = 30;
    bottomBar.background = core::ColorRGBA8{8, 10, 16, 220};
    hudChild(std::move(bottomBar));
    ui::NodeDefinition quickSlots;
    quickSlots.id = "hud.quickSlots";
    quickSlots.component = ui::ComponentKind::repeater;
    quickSlots.layout.offsetX = 4;
    quickSlots.layout.offsetY = 197;
    quickSlots.columns = 4;
    quickSlots.cellWidth = 40;
    quickSlots.cellHeight = 23;
    quickSlots.bindings.push_back({"source", ui::BindingPath::playerQuickSlotSlots});
    ui::NodeDefinition quickSlot;
    quickSlot.id = "hud.quickSlot";
    quickSlot.component = ui::ComponentKind::slot;
    quickSlot.layout.width = 34;
    quickSlot.layout.height = 23;
    quickSlot.background = core::ColorRGBA8{54, 30, 38, 255};
    quickSlot.iconOffset = core::PointI{10, 2};
    quickSlot.countOffset = core::PointI{22, 12};
    quickSlot.countAlways = true;
    quickSlot.bindings.push_back({"icon", ui::BindingPath::contextItemIcon});
    quickSlot.bindings.push_back({"count", ui::BindingPath::contextItemAmount});
    quickSlots.children.push_back(std::move(quickSlot));
    hudChild(std::move(quickSlots));
    for (int index = 0; index < 4; ++index) {
        ui::NodeDefinition number;
        number.id = "hud.quickSlot.number" + std::to_string(index + 1);
        number.component = ui::ComponentKind::text;
        number.layout.offsetX = 4 + index * 40 + 2;
        number.layout.offsetY = 199;
        number.text = std::to_string(index + 1);
        hudChild(std::move(number));
    }
    ui::NodeDefinition hint;
    hint.id = "hud.hint";
    hint.component = ui::ComponentKind::text;
    hint.layout.offsetX = 169;
    hint.layout.offsetY = 203;
    hint.text = "I ITEMS  E OPEN";
    hudChild(std::move(hint));
    hudScreen.root = std::move(hudRoot);
    pack.uiScreens.push_back(std::move(hudScreen));

    // Inventory overlay as authored UI content (docs/UI_ENGINE.md proof 2):
    // the definition reproduces the legacy inventory panel pixel-for-pixel —
    // panel rect, title and the 30-slot grid through repeater+slot with the
    // derived selection flag. Rearranging the grid is a definition edit only.
    ui::ScreenDefinition inventoryScreen;
    inventoryScreen.id = simulation::DefinitionId{"screen.inventory"};
    inventoryScreen.kind = ui::ScreenKind::overlay;
    ui::NodeDefinition panel;
    panel.id = "inventory.panel";
    panel.component = ui::ComponentKind::panel;
    panel.layout.anchor = ui::Anchor::topLeft;
    panel.layout.offsetX = 6;
    panel.layout.offsetY = 52;
    panel.layout.width = 260;
    panel.layout.height = 145;
    panel.background = core::ColorRGBA8{8, 10, 16, 245};
    ui::NodeDefinition title;
    title.id = "inventory.title";
    title.component = ui::ComponentKind::text;
    title.layout.offsetX = 10;
    title.layout.offsetY = 55;
    title.text = "INVENTORY";
    ui::NodeDefinition grid;
    grid.id = "inventory.grid";
    grid.component = ui::ComponentKind::repeater;
    grid.layout.offsetX = 10;
    grid.layout.offsetY = 66;
    grid.columns = 10;
    grid.cellWidth = 25;
    grid.cellHeight = 20;
    grid.bindings.push_back({"source", ui::BindingPath::playerInventorySlots});
    ui::NodeDefinition slot;
    slot.id = "inventory.slot";
    slot.component = ui::ComponentKind::slot;
    slot.layout.width = 22;
    slot.layout.height = 18;
    slot.background = core::ColorRGBA8{54, 30, 38, 255};
    slot.iconOffset = core::PointI{3, 1};
    slot.countOffset = core::PointI{10, 9};
    slot.bindings.push_back({"icon", ui::BindingPath::contextItemIcon});
    slot.bindings.push_back({"count", ui::BindingPath::contextItemAmount});
    ui::StateDefinition selected;
    selected.id = "selected";
    selected.condition = ui::StateCondition{
        ui::BindingPath::overlayInventorySlotSelected, ui::ConditionOperator::equal, 1};
    selected.visual.background = core::ColorRGBA8{220, 180, 72, 255};
    slot.states.push_back(std::move(selected));
    grid.children.push_back(std::move(slot));
    panel.children.push_back(std::move(title));
    panel.children.push_back(std::move(grid));

    // Equipment section: authored slot groups with selection-gated
    // background states and dynamically bound icons; the stats line and the
    // control hints close the overlay.
    ui::NodeDefinition equipmentTitle;
    equipmentTitle.id = "inventory.equipment.title";
    equipmentTitle.component = ui::ComponentKind::text;
    equipmentTitle.layout.offsetX = 10;
    equipmentTitle.layout.offsetY = 128;
    equipmentTitle.text = "EQUIPMENT";
    panel.children.push_back(std::move(equipmentTitle));
    const auto makeEquipmentSlot = [&](const char* id, const char* label, int x,
                                       ui::BindingPath iconPath,
                                       ui::BindingPath selectedPath) {
        ui::NodeDefinition slotGroup;
        slotGroup.id = id;
        slotGroup.component = ui::ComponentKind::group;
        slotGroup.layout.offsetX = x;
        slotGroup.layout.offsetY = 138;
        slotGroup.layout.width = 92;
        slotGroup.layout.height = 22;
        slotGroup.background = core::ColorRGBA8{54, 30, 38, 255};
        ui::StateDefinition selected;
        selected.id = "selected";
        selected.condition = ui::StateCondition{selectedPath,
                                                ui::ConditionOperator::equal, 1};
        selected.visual.background = core::ColorRGBA8{220, 180, 72, 255};
        slotGroup.states.push_back(std::move(selected));
        ui::NodeDefinition labelText;
        labelText.id = std::string(id) + ".label";
        labelText.component = ui::ComponentKind::text;
        labelText.layout.offsetX = x + 3;
        labelText.layout.offsetY = 141;
        labelText.text = label;
        slotGroup.children.push_back(std::move(labelText));
        ui::NodeDefinition icon;
        icon.id = std::string(id) + ".icon";
        icon.component = ui::ComponentKind::image;
        icon.layout.offsetX = x + 55;
        icon.layout.offsetY = 140;
        icon.bindings.push_back({"icon", iconPath});
        slotGroup.children.push_back(std::move(icon));
        panel.children.push_back(std::move(slotGroup));
    };
    makeEquipmentSlot("inventory.equipment.armor", "ARMOR", 10,
                      ui::BindingPath::playerArmorIcon,
                      ui::BindingPath::overlayEquipmentArmorSelected);
    makeEquipmentSlot("inventory.equipment.accessory", "ACCESSORY", 108,
                      ui::BindingPath::playerAccessoryIcon,
                      ui::BindingPath::overlayEquipmentAccessorySelected);
    ui::NodeDefinition statsLabel;
    statsLabel.id = "inventory.stats.label";
    statsLabel.component = ui::ComponentKind::text;
    statsLabel.layout.offsetX = 10;
    statsLabel.layout.offsetY = 164;
    statsLabel.text = "MAX HP";
    panel.children.push_back(std::move(statsLabel));
    ui::NodeDefinition statsHealth;
    statsHealth.id = "inventory.stats.health";
    statsHealth.component = ui::ComponentKind::text;
    statsHealth.layout.offsetX = 59;
    statsHealth.layout.offsetY = 164;
    statsHealth.bindings.push_back({"text", ui::BindingPath::playerDerivedMaxHealth});
    panel.children.push_back(std::move(statsHealth));
    ui::NodeDefinition statsAttackLabel;
    statsAttackLabel.id = "inventory.stats.attack.label";
    statsAttackLabel.component = ui::ComponentKind::text;
    statsAttackLabel.layout.offsetX = 87;
    statsAttackLabel.layout.offsetY = 164;
    statsAttackLabel.text = "ATK";
    panel.children.push_back(std::move(statsAttackLabel));
    ui::NodeDefinition statsAttack;
    statsAttack.id = "inventory.stats.attack";
    statsAttack.component = ui::ComponentKind::text;
    statsAttack.layout.offsetX = 115;
    statsAttack.layout.offsetY = 164;
    statsAttack.bindings.push_back({"text", ui::BindingPath::playerAttackDamageBonus});
    panel.children.push_back(std::move(statsAttack));
    ui::NodeDefinition hints;
    hints.id = "inventory.hints";
    hints.component = ui::ComponentKind::text;
    hints.layout.offsetX = 10;
    hints.layout.offsetY = 181;
    hints.text = "Z USE/EQUIP  1-4 BIND  I CLOSE";
    panel.children.push_back(std::move(hints));
    inventoryScreen.root = std::move(panel);
    pack.uiScreens.push_back(std::move(inventoryScreen));

    // Pause menu (UI-5): the first authored menu screen. Buttons are group
    // nodes with activate actions; RESUME closes the menu through the
    // presentation-level screen.close action.
    ui::ScreenDefinition menuScreen;
    menuScreen.id = simulation::DefinitionId{"screen.menu"};
    menuScreen.kind = ui::ScreenKind::screen;
    ui::NodeDefinition menuPanel;
    menuPanel.id = "menu.panel";
    menuPanel.component = ui::ComponentKind::panel;
    menuPanel.layout.offsetX = 86;
    menuPanel.layout.offsetY = 60;
    menuPanel.layout.width = 100;
    menuPanel.layout.height = 108;
    menuPanel.background = core::ColorRGBA8{8, 10, 16, 245};
    ui::NodeDefinition menuTitle;
    menuTitle.id = "menu.title";
    menuTitle.component = ui::ComponentKind::text;
    menuTitle.layout.offsetX = 115;
    menuTitle.layout.offsetY = 68;
    menuTitle.text = "PAUSED";
    const auto makeButton = [&](const char* id, const char* label, int y,
                                ui::ActionId action) {
        ui::NodeDefinition button;
        button.id = id;
        button.component = ui::ComponentKind::group;
        button.layout.offsetX = 94;
        button.layout.offsetY = y;
        button.layout.width = 84;
        button.layout.height = 18;
        button.background = core::ColorRGBA8{54, 30, 38, 255};
        button.actions.push_back({"activate", action});
        ui::NodeDefinition buttonText;
        buttonText.id = std::string(id) + ".label";
        buttonText.component = ui::ComponentKind::text;
        const int labelWidth = static_cast<int>(std::string(label).size()) * 7;
        buttonText.layout.offsetX = 94 + (84 - labelWidth) / 2;
        buttonText.layout.offsetY = y + 5;
        buttonText.text = label;
        button.children.push_back(std::move(buttonText));
        return button;
    };
    menuPanel.children.push_back(std::move(menuTitle));
    menuPanel.children.push_back(makeButton("menu.save", "SAVE", 84, ui::ActionId::gameSave));
    menuPanel.children.push_back(makeButton("menu.load", "LOAD", 106, ui::ActionId::gameLoad));
    menuPanel.children.push_back(
        makeButton("menu.saves", "SLOTS", 150, ui::ActionId::screenOpenSaves));
    menuPanel.children.push_back(
        makeButton("menu.resume", "RESUME", 172, ui::ActionId::screenClose));
    menuScreen.root = std::move(menuPanel);
    pack.uiScreens.push_back(std::move(menuScreen));

    // Quest journal (UI-5 companion screen): passive overlay toggled with J,
    // listing started quests from the read model with a DONE marker gated by
    // the authored completion state.
    ui::ScreenDefinition journalScreen;
    journalScreen.id = simulation::DefinitionId{"screen.journal"};
    journalScreen.kind = ui::ScreenKind::screen;
    ui::NodeDefinition journalPanel;
    journalPanel.id = "journal.panel";
    journalPanel.component = ui::ComponentKind::panel;
    journalPanel.layout.offsetX = 14;
    journalPanel.layout.offsetY = 20;
    journalPanel.layout.width = 244;
    journalPanel.layout.height = 184;
    journalPanel.background = core::ColorRGBA8{8, 10, 16, 245};
    ui::NodeDefinition journalTitle;
    journalTitle.id = "journal.title";
    journalTitle.component = ui::ComponentKind::text;
    journalTitle.layout.offsetX = 24;
    journalTitle.layout.offsetY = 28;
    journalTitle.text = "JOURNAL";
    ui::NodeDefinition questList;
    questList.id = "journal.quests";
    questList.component = ui::ComponentKind::repeater;
    questList.layout.offsetX = 24;
    questList.layout.offsetY = 44;
    questList.columns = 1;
    questList.cellWidth = 228;
    questList.cellHeight = 12;
    questList.bindings.push_back({"source", ui::BindingPath::questsJournal});
    ui::NodeDefinition questEntry;
    questEntry.id = "journal.entry";
    questEntry.component = ui::ComponentKind::group;
    ui::NodeDefinition questText;
    questText.id = "journal.entry.title";
    questText.component = ui::ComponentKind::text;
    questText.layout.offsetX = 6;
    questText.bindings.push_back({"text", ui::BindingPath::contextQuestTitle});
    questEntry.children.push_back(std::move(questText));
    ui::NodeDefinition questDone;
    questDone.id = "journal.entry.done";
    questDone.component = ui::ComponentKind::text;
    questDone.layout.offsetX = 190;
    questDone.layout.visible = false;
    questDone.text = "DONE";
    ui::StateDefinition questCompleted;
    questCompleted.id = "completed";
    questCompleted.condition = ui::StateCondition{
        ui::BindingPath::contextQuestCompleted, ui::ConditionOperator::equal, 1};
    questCompleted.visual.visible = true;
    questDone.states.push_back(std::move(questCompleted));
    questEntry.children.push_back(std::move(questDone));
    questList.children.push_back(std::move(questEntry));
    journalPanel.children.push_back(std::move(journalTitle));
    journalPanel.children.push_back(std::move(questList));
    journalScreen.root = std::move(journalPanel);
    pack.uiScreens.push_back(std::move(journalScreen));

    // Saves screen (opened from the pause menu): three authored slot rows.
    // Labels are string bindings over the shell's slot readout; each row has
    // explicit LOAD/SAVE buttons acting on that slot. BACK is screen.close.
    ui::ScreenDefinition savesScreen;
    savesScreen.id = simulation::DefinitionId{"screen.saves"};
    savesScreen.kind = ui::ScreenKind::screen;
    ui::NodeDefinition savesPanel;
    savesPanel.id = "saves.panel";
    savesPanel.component = ui::ComponentKind::panel;
    savesPanel.layout.offsetX = 66;
    savesPanel.layout.offsetY = 36;
    savesPanel.layout.width = 140;
    savesPanel.layout.height = 146;
    savesPanel.background = core::ColorRGBA8{8, 10, 16, 245};
    ui::NodeDefinition savesTitle;
    savesTitle.id = "saves.title";
    savesTitle.component = ui::ComponentKind::text;
    savesTitle.layout.offsetX = 118;
    savesTitle.layout.offsetY = 44;
    savesTitle.text = "SAVES";
    savesPanel.children.push_back(std::move(savesTitle));
    const auto makeSaveRow = [&](int slotIndex) {
        const int rowY = 62 + slotIndex * 30;
        const std::string slotName = std::to_string(slotIndex + 1);
        ui::NodeDefinition label;
        label.id = "saves.slot" + slotName + ".label";
        label.component = ui::ComponentKind::text;
        label.layout.offsetX = 76;
        label.layout.offsetY = rowY;
        const ui::BindingPath labelPaths[] = {ui::BindingPath::saveSlot1Label,
                                              ui::BindingPath::saveSlot2Label,
                                              ui::BindingPath::saveSlot3Label};
        label.bindings.push_back({"text", labelPaths[slotIndex]});
        savesPanel.children.push_back(std::move(label));
        ui::NodeDefinition loadButton;
        loadButton.id = "saves.slot" + slotName + ".load";
        loadButton.component = ui::ComponentKind::group;
        loadButton.layout.offsetX = 76;
        loadButton.layout.offsetY = rowY + 10;
        loadButton.layout.width = 40;
        loadButton.layout.height = 16;
        loadButton.background = core::ColorRGBA8{54, 30, 38, 255};
        const ui::ActionId loadActions[] = {ui::ActionId::gameLoadSlot1,
                                            ui::ActionId::gameLoadSlot2,
                                            ui::ActionId::gameLoadSlot3};
        loadButton.actions.push_back({"activate", loadActions[slotIndex]});
        ui::NodeDefinition loadText;
        loadText.id = "saves.slot" + slotName + ".load.text";
        loadText.component = ui::ComponentKind::text;
        loadText.layout.offsetX = 82;
        loadText.layout.offsetY = rowY + 13;
        loadText.text = "LOAD";
        loadButton.children.push_back(std::move(loadText));
        savesPanel.children.push_back(std::move(loadButton));
        ui::NodeDefinition saveButton;
        saveButton.id = "saves.slot" + slotName + ".save";
        saveButton.component = ui::ComponentKind::group;
        saveButton.layout.offsetX = 122;
        saveButton.layout.offsetY = rowY + 10;
        saveButton.layout.width = 40;
        saveButton.layout.height = 16;
        saveButton.background = core::ColorRGBA8{54, 30, 38, 255};
        // Hidden while the title shell uses this screen as the start picker
        // (saves.startMode == 1): overwriting saves makes no sense there.
        saveButton.layout.visible = false;
        ui::StateDefinition saveVisible;
        saveVisible.id = "play-mode";
        saveVisible.condition = ui::StateCondition{ui::BindingPath::savesStartMode,
                                                   ui::ConditionOperator::equal, 0};
        saveVisible.visual.visible = true;
        saveButton.states.push_back(std::move(saveVisible));
        const ui::ActionId saveActions[] = {ui::ActionId::gameSaveSlot1,
                                            ui::ActionId::gameSaveSlot2,
                                            ui::ActionId::gameSaveSlot3};
        saveButton.actions.push_back({"activate", saveActions[slotIndex]});
        ui::NodeDefinition saveText;
        saveText.id = "saves.slot" + slotName + ".save.text";
        saveText.component = ui::ComponentKind::text;
        saveText.layout.offsetX = 128;
        saveText.layout.offsetY = rowY + 13;
        saveText.text = "SAVE";
        saveButton.children.push_back(std::move(saveText));
        savesPanel.children.push_back(std::move(saveButton));
    };
    makeSaveRow(0);
    makeSaveRow(1);
    makeSaveRow(2);
    ui::NodeDefinition backButton;
    backButton.id = "saves.back";
    backButton.component = ui::ComponentKind::group;
    backButton.layout.offsetX = 104;
    backButton.layout.offsetY = 158;
    backButton.layout.width = 64;
    backButton.layout.height = 16;
    backButton.background = core::ColorRGBA8{54, 30, 38, 255};
    backButton.actions.push_back({"activate", ui::ActionId::screenClose});
    ui::NodeDefinition backText;
    backText.id = "saves.back.text";
    backText.component = ui::ComponentKind::text;
    backText.layout.offsetX = 113;
    backText.layout.offsetY = 161;
    backText.text = "BACK";
    backButton.children.push_back(std::move(backText));
    savesPanel.children.push_back(std::move(backButton));
    savesScreen.root = std::move(savesPanel);
    pack.uiScreens.push_back(std::move(savesScreen));

    // Title shell (boot state, Scene/Game-State trilha): a dim over the
    // static world with the authored name and a single PRESS E node whose
    // activation opens the saves screen in start mode — LOAD continues a
    // saved slot or starts a new game into an empty one; BACK returns here.
    ui::ScreenDefinition titleScreen;
    titleScreen.id = simulation::DefinitionId{"screen.title"};
    titleScreen.kind = ui::ScreenKind::screen;
    ui::NodeDefinition titlePanel;
    titlePanel.id = "title.panel";
    titlePanel.component = ui::ComponentKind::panel;
    titlePanel.layout.offsetX = 0;
    titlePanel.layout.offsetY = 0;
    titlePanel.layout.width = 272;
    titlePanel.layout.height = 224;
    titlePanel.background = core::ColorRGBA8{8, 10, 16, 216};
    const auto titleChild = [&](ui::NodeDefinition node) {
        titlePanel.children.push_back(std::move(node));
    };
    ui::NodeDefinition titleName;
    titleName.id = "title.name";
    titleName.component = ui::ComponentKind::text;
    titleName.layout.offsetX = 87;
    titleName.layout.offsetY = 62;
    titleName.text = "UNDERWORLD";
    titleChild(std::move(titleName));
    ui::NodeDefinition titleSubtitle;
    titleSubtitle.id = "title.subtitle";
    titleSubtitle.component = ui::ComponentKind::text;
    titleSubtitle.layout.offsetX = 93;
    titleSubtitle.layout.offsetY = 76;
    titleSubtitle.text = "DUNGEON UNDERWORLD";
    titleChild(std::move(titleSubtitle));
    ui::NodeDefinition titleStart;
    titleStart.id = "title.start";
    titleStart.component = ui::ComponentKind::group;
    titleStart.layout.offsetX = 94;
    titleStart.layout.offsetY = 138;
    titleStart.layout.width = 84;
    titleStart.layout.height = 18;
    titleStart.background = core::ColorRGBA8{54, 30, 38, 255};
    titleStart.actions.push_back({"activate", ui::ActionId::screenOpenSaves});
    ui::NodeDefinition titleStartText;
    titleStartText.id = "title.start.label";
    titleStartText.component = ui::ComponentKind::text;
    titleStartText.layout.offsetX = 111;
    titleStartText.layout.offsetY = 143;
    titleStartText.text = "PRESS E";
    titleStart.children.push_back(std::move(titleStartText));
    titleChild(std::move(titleStart));
    ui::NodeDefinition titleHints;
    titleHints.id = "title.hints";
    titleHints.component = ui::ComponentKind::text;
    titleHints.layout.offsetX = 64;
    titleHints.layout.offsetY = 186;
    titleHints.text = "ARROWS SELECT  E CONFIRM";
    titleChild(std::move(titleHints));
    titleScreen.root = std::move(titlePanel);
    pack.uiScreens.push_back(std::move(titleScreen));

    // Bank overlay as authored UI content: carried grid, storage grid (both
    // repeaters with selection-gated states) and the gold readout.
    ui::ScreenDefinition bankScreen;
    bankScreen.id = simulation::DefinitionId{"screen.bank"};
    bankScreen.kind = ui::ScreenKind::overlay;
    ui::NodeDefinition bankPanel;
    bankPanel.id = "bank.panel";
    bankPanel.component = ui::ComponentKind::panel;
    bankPanel.layout.offsetX = 4;
    bankPanel.layout.offsetY = 24;
    bankPanel.layout.width = 264;
    bankPanel.layout.height = 169;
    bankPanel.background = core::ColorRGBA8{8, 10, 16, 248};
    const auto bankText = [&](const char* id, const char* value, int x, int y) {
        ui::NodeDefinition node;
        node.id = id;
        node.component = ui::ComponentKind::text;
        node.layout.offsetX = x;
        node.layout.offsetY = y;
        node.text = value;
        bankPanel.children.push_back(std::move(node));
    };
    bankText("bank.title", "BANK", 8, 27);
    bankText("bank.inventory.title", "INVENTORY", 8, 43);
    const auto makeBankGrid = [&](const char* id, ui::BindingPath source,
                                  ui::BindingPath selectedPath, int y) {
        ui::NodeDefinition grid;
        grid.id = id;
        grid.component = ui::ComponentKind::repeater;
        grid.layout.offsetX = 8;
        grid.layout.offsetY = y;
        grid.columns = 10;
        grid.cellWidth = 26;
        grid.cellHeight = 15;
        grid.bindings.push_back({"source", source});
        ui::NodeDefinition slot;
        slot.id = std::string(id) + ".slot";
        slot.component = ui::ComponentKind::slot;
        slot.layout.width = 24;
        slot.layout.height = 13;
        slot.background = core::ColorRGBA8{54, 30, 38, 255};
        slot.iconOffset = core::PointI{1, 0};
        slot.countOffset = core::PointI{1, 4};
        slot.bindings.push_back({"icon", ui::BindingPath::contextItemIcon});
        slot.bindings.push_back({"count", ui::BindingPath::contextItemAmount});
        ui::StateDefinition selected;
        selected.id = "selected";
        selected.condition = ui::StateCondition{selectedPath,
                                                ui::ConditionOperator::equal, 1};
        selected.visual.background = core::ColorRGBA8{220, 180, 72, 255};
        slot.states.push_back(std::move(selected));
        grid.children.push_back(std::move(slot));
        bankPanel.children.push_back(std::move(grid));
    };
    makeBankGrid("bank.inventory.grid", ui::BindingPath::playerInventorySlots,
                 ui::BindingPath::overlayBankInventorySelected, 53);
    bankText("bank.storage.title", "STORAGE", 8, 105);
    makeBankGrid("bank.storage.grid", ui::BindingPath::bankSlots,
                 ui::BindingPath::overlayBankStorageSelected, 115);
    bankText("bank.carried.label", "CARRIED", 8, 188);
    ui::NodeDefinition carriedGold;
    carriedGold.id = "bank.carried.gold";
    carriedGold.component = ui::ComponentKind::text;
    carriedGold.layout.offsetX = 57;
    carriedGold.layout.offsetY = 188;
    carriedGold.bindings.push_back({"text", ui::BindingPath::playerGold});
    bankPanel.children.push_back(std::move(carriedGold));
    bankText("bank.stored.label", "STORED", 104, 188);
    ui::NodeDefinition storedGold;
    storedGold.id = "bank.stored.gold";
    storedGold.component = ui::ComponentKind::text;
    storedGold.layout.offsetX = 141;
    storedGold.layout.offsetY = 188;
    storedGold.bindings.push_back({"text", ui::BindingPath::bankGoldStored});
    bankPanel.children.push_back(std::move(storedGold));
    bankText("bank.hints", "Z TRANSFER  I CLOSE", 152, 208);
    bankScreen.root = std::move(bankPanel);
    pack.uiScreens.push_back(std::move(bankScreen));

    // Shop overlay as authored UI content: BUY mode shows the priced offer
    // list (repeater over shop.offers with a selection state); SELL mode
    // shows the carried inventory grid with the sell-selection highlight.
    // Nodes gate on authored visibility states over overlay.shop.modeSell.
    ui::ScreenDefinition shopScreen;
    shopScreen.id = simulation::DefinitionId{"screen.shop"};
    shopScreen.kind = ui::ScreenKind::overlay;
    ui::NodeDefinition shopPanel;
    shopPanel.id = "shop.panel";
    shopPanel.component = ui::ComponentKind::panel;
    shopPanel.layout.offsetX = 4;
    shopPanel.layout.offsetY = 24;
    shopPanel.layout.width = 264;
    shopPanel.layout.height = 169;
    shopPanel.background = core::ColorRGBA8{8, 10, 16, 248};
    const auto shopModeGated = [&](ui::NodeDefinition node, std::int64_t modeValue) {
        node.layout.visible = false;
        ui::StateDefinition mode;
        mode.id = "mode";
        mode.condition = ui::StateCondition{ui::BindingPath::overlayShopModeSell,
                                            ui::ConditionOperator::equal, modeValue};
        mode.visual.visible = true;
        node.states.push_back(std::move(mode));
        shopPanel.children.push_back(std::move(node));
    };
    ui::NodeDefinition shopTitle;
    shopTitle.id = "shop.title";
    shopTitle.component = ui::ComponentKind::text;
    shopTitle.layout.offsetX = 8;
    shopTitle.layout.offsetY = 27;
    shopTitle.text = "SHOP";
    shopPanel.children.push_back(std::move(shopTitle));
    ui::NodeDefinition buyLabel;
    buyLabel.id = "shop.mode.buy";
    buyLabel.component = ui::ComponentKind::text;
    buyLabel.layout.offsetX = 43;
    buyLabel.layout.offsetY = 27;
    buyLabel.text = "BUY";
    shopModeGated(std::move(buyLabel), 0);
    ui::NodeDefinition sellLabel;
    sellLabel.id = "shop.mode.sell";
    sellLabel.component = ui::ComponentKind::text;
    sellLabel.layout.offsetX = 43;
    sellLabel.layout.offsetY = 27;
    sellLabel.text = "SELL";
    shopModeGated(std::move(sellLabel), 1);
    ui::NodeDefinition carriedLabel;
    carriedLabel.id = "shop.carried.label";
    carriedLabel.component = ui::ComponentKind::text;
    carriedLabel.layout.offsetX = 8;
    carriedLabel.layout.offsetY = 39;
    carriedLabel.text = "CARRIED GOLD";
    shopPanel.children.push_back(std::move(carriedLabel));
    ui::NodeDefinition shopCarriedGold;
    shopCarriedGold.id = "shop.carried.gold";
    shopCarriedGold.component = ui::ComponentKind::text;
    shopCarriedGold.layout.offsetX = 99;
    shopCarriedGold.layout.offsetY = 39;
    shopCarriedGold.bindings.push_back({"text", ui::BindingPath::playerGold});
    shopPanel.children.push_back(std::move(shopCarriedGold));

    ui::NodeDefinition offerList;
    offerList.id = "shop.offers";
    offerList.component = ui::ComponentKind::repeater;
    offerList.layout.offsetX = 8;
    offerList.layout.offsetY = 49;
    offerList.columns = 1;
    offerList.cellWidth = 250;
    offerList.cellHeight = 12;
    offerList.bindings.push_back({"source", ui::BindingPath::shopOffers});
    ui::NodeDefinition offerRow;
    offerRow.id = "shop.offers.row";
    offerRow.component = ui::ComponentKind::group;
    offerRow.layout.width = 250;
    offerRow.layout.height = 11;
    offerRow.background = core::ColorRGBA8{54, 30, 38, 255};
    ui::StateDefinition offerSelected;
    offerSelected.id = "selected";
    offerSelected.condition = ui::StateCondition{ui::BindingPath::overlayShopBuySelected,
                                                 ui::ConditionOperator::equal, 1};
    offerSelected.visual.background = core::ColorRGBA8{96, 62, 54, 255};
    offerRow.states.push_back(std::move(offerSelected));
    ui::NodeDefinition offerText;
    offerText.id = "shop.offers.line";
    offerText.component = ui::ComponentKind::text;
    offerText.layout.offsetX = 3;
    offerText.layout.offsetY = 1;
    offerText.bindings.push_back({"text", ui::BindingPath::contextOfferLine});
    offerRow.children.push_back(std::move(offerText));
    offerList.children.push_back(std::move(offerRow));
    shopModeGated(std::move(offerList), 0);

    ui::NodeDefinition sellGrid;
    sellGrid.id = "shop.sell.grid";
    sellGrid.component = ui::ComponentKind::repeater;
    sellGrid.layout.offsetX = 8;
    sellGrid.layout.offsetY = 51;
    sellGrid.columns = 10;
    sellGrid.cellWidth = 25;
    sellGrid.cellHeight = 20;
    sellGrid.bindings.push_back({"source", ui::BindingPath::playerInventorySlots});
    ui::NodeDefinition sellSlot;
    sellSlot.id = "shop.sell.slot";
    sellSlot.component = ui::ComponentKind::slot;
    sellSlot.layout.width = 22;
    sellSlot.layout.height = 18;
    sellSlot.background = core::ColorRGBA8{54, 30, 38, 255};
    sellSlot.iconOffset = core::PointI{3, 1};
    sellSlot.bindings.push_back({"icon", ui::BindingPath::contextItemIcon});
    ui::StateDefinition sellSelected;
    sellSelected.id = "selected";
    sellSelected.condition = ui::StateCondition{ui::BindingPath::overlayShopSellSelected,
                                                ui::ConditionOperator::equal, 1};
    sellSelected.visual.background = core::ColorRGBA8{220, 180, 72, 255};
    sellSlot.states.push_back(std::move(sellSelected));
    sellGrid.children.push_back(std::move(sellSlot));
    shopModeGated(std::move(sellGrid), 1);
    ui::NodeDefinition sellName;
    sellName.id = "shop.sell.name";
    sellName.component = ui::ComponentKind::text;
    sellName.layout.offsetX = 8;
    sellName.layout.offsetY = 116;
    sellName.layout.visible = false;
    sellName.bindings.push_back({"text", ui::BindingPath::overlayShopSellItemName});
    shopModeGated(std::move(sellName), 1);
    ui::NodeDefinition sellHint;
    sellHint.id = "shop.sell.hint";
    sellHint.component = ui::ComponentKind::text;
    sellHint.layout.offsetX = 8;
    sellHint.layout.offsetY = 128;
    sellHint.layout.visible = false;
    sellHint.text = "SELL: see offer";
    shopModeGated(std::move(sellHint), 1);

    ui::NodeDefinition feedbackLabel;
    feedbackLabel.id = "shop.feedback.label";
    feedbackLabel.component = ui::ComponentKind::text;
    feedbackLabel.layout.offsetX = 8;
    feedbackLabel.layout.offsetY = 177;
    feedbackLabel.layout.visible = false;
    feedbackLabel.text = "STATUS";
    ui::StateDefinition feedbackPresent;
    feedbackPresent.id = "present";
    feedbackPresent.condition = ui::StateCondition{ui::BindingPath::overlayShopFeedbackPresent,
                                                   ui::ConditionOperator::equal, 1};
    feedbackPresent.visual.visible = true;
    feedbackLabel.states.push_back(std::move(feedbackPresent));
    shopPanel.children.push_back(std::move(feedbackLabel));
    ui::NodeDefinition feedbackValue;
    feedbackValue.id = "shop.feedback.value";
    feedbackValue.component = ui::ComponentKind::text;
    feedbackValue.layout.offsetX = 57;
    feedbackValue.layout.offsetY = 177;
    feedbackValue.layout.visible = false;
    feedbackValue.bindings.push_back({"text", ui::BindingPath::overlayShopFeedbackPresent});
    ui::StateDefinition feedbackPresentValue;
    feedbackPresentValue.id = "present";
    feedbackPresentValue.condition = ui::StateCondition{
        ui::BindingPath::overlayShopFeedbackPresent, ui::ConditionOperator::equal, 1};
    feedbackPresentValue.visual.visible = true;
    feedbackValue.states.push_back(std::move(feedbackPresentValue));
    shopPanel.children.push_back(std::move(feedbackValue));
    ui::NodeDefinition shopHints;
    shopHints.id = "shop.hints";
    shopHints.component = ui::ComponentKind::text;
    shopHints.layout.offsetX = 8;
    shopHints.layout.offsetY = 188;
    shopHints.text = "PRIMARY TRADE  SECONDARY SWITCH  I CLOSE";
    shopPanel.children.push_back(std::move(shopHints));
    shopScreen.root = std::move(shopPanel);
    pack.uiScreens.push_back(std::move(shopScreen));

    // Crafting overlay as authored UI content: the ITEMS/CRAFTING/BOOK tab
    // strip with authored highlight panels, the craft list and the recipe
    // book as repeaters with selection states, and the selected-recipe
    // detail as string-bound text nodes (lines composed in the view model).
    ui::ScreenDefinition craftingScreen;
    craftingScreen.id = simulation::DefinitionId{"screen.crafting"};
    craftingScreen.kind = ui::ScreenKind::overlay;
    ui::NodeDefinition craftingPanel;
    craftingPanel.id = "crafting.panel";
    craftingPanel.component = ui::ComponentKind::panel;
    craftingPanel.layout.offsetX = 6;
    craftingPanel.layout.offsetY = 52;
    craftingPanel.layout.width = 260;
    craftingPanel.layout.height = 145;
    craftingPanel.background = core::ColorRGBA8{8, 10, 16, 245};
    const auto craftingChild = [&](ui::NodeDefinition node) {
        craftingPanel.children.push_back(std::move(node));
    };
    ui::NodeDefinition craftingTitle;
    craftingTitle.id = "crafting.title";
    craftingTitle.component = ui::ComponentKind::text;
    craftingTitle.layout.offsetX = 10;
    craftingTitle.layout.offsetY = 55;
    craftingTitle.text = "INVENTORY";
    craftingChild(std::move(craftingTitle));
    ui::NodeDefinition tabsText;
    tabsText.id = "crafting.tabs";
    tabsText.component = ui::ComponentKind::text;
    tabsText.layout.offsetX = 150;
    tabsText.layout.offsetY = 55;
    tabsText.text = "ITEMS  CRAFTING  BOOK";
    craftingChild(std::move(tabsText));
    ui::NodeDefinition craftTabHighlight;
    craftTabHighlight.id = "crafting.tab.craft.highlight";
    craftTabHighlight.component = ui::ComponentKind::panel;
    craftTabHighlight.layout.offsetX = 158;
    craftTabHighlight.layout.offsetY = 53;
    craftTabHighlight.layout.width = 52;
    craftTabHighlight.layout.height = 11;
    craftTabHighlight.background = core::ColorRGBA8{96, 62, 54, 160};
    ui::StateDefinition craftTabShow;
    craftTabShow.id = "active";
    craftTabShow.condition = ui::StateCondition{ui::BindingPath::overlayCraftingTabCraft,
                                                ui::ConditionOperator::equal, 1};
    craftTabShow.visual.visible = true;
    craftTabHighlight.layout.visible = false;
    craftTabHighlight.states.push_back(std::move(craftTabShow));
    craftingChild(std::move(craftTabHighlight));
    ui::NodeDefinition bookTabHighlight;
    bookTabHighlight.id = "crafting.tab.book.highlight";
    bookTabHighlight.component = ui::ComponentKind::panel;
    bookTabHighlight.layout.offsetX = 214;
    bookTabHighlight.layout.offsetY = 53;
    bookTabHighlight.layout.width = 28;
    bookTabHighlight.layout.height = 11;
    bookTabHighlight.background = core::ColorRGBA8{96, 62, 54, 160};
    ui::StateDefinition bookTabShow;
    bookTabShow.id = "active";
    bookTabShow.condition = ui::StateCondition{ui::BindingPath::overlayCraftingTabBook,
                                               ui::ConditionOperator::equal, 1};
    bookTabShow.visual.visible = true;
    bookTabHighlight.layout.visible = false;
    bookTabHighlight.states.push_back(std::move(bookTabShow));
    craftingChild(std::move(bookTabHighlight));

    // CRAFT tab: recipe list with the selection highlight.
    ui::NodeDefinition craftList;
    craftList.id = "crafting.craft.list";
    craftList.component = ui::ComponentKind::repeater;
    craftList.layout.offsetX = 8;
    craftList.layout.offsetY = 38;
    craftList.columns = 1;
    craftList.cellWidth = 250;
    craftList.cellHeight = 11;
    craftList.bindings.push_back({"source", ui::BindingPath::craftingRecipesPath});
    ui::NodeDefinition craftRow;
    craftRow.id = "crafting.craft.row";
    craftRow.component = ui::ComponentKind::group;
    craftRow.layout.width = 250;
    craftRow.layout.height = 10;
    ui::StateDefinition craftRowSelected;
    craftRowSelected.id = "selected";
    craftRowSelected.condition = ui::StateCondition{
        ui::BindingPath::overlayCraftingRowSelected, ui::ConditionOperator::equal, 1};
    craftRowSelected.visual.background = core::ColorRGBA8{96, 62, 54, 255};
    craftRow.states.push_back(std::move(craftRowSelected));
    ui::NodeDefinition craftRowText;
    craftRowText.id = "crafting.craft.line";
    craftRowText.component = ui::ComponentKind::text;
    craftRowText.layout.offsetX = 3;
    craftRowText.layout.offsetY = 1;
    craftRowText.bindings.push_back({"text", ui::BindingPath::contextCraftLine});
    craftRow.children.push_back(std::move(craftRowText));
    craftList.children.push_back(std::move(craftRow));
    {
        ui::StateDefinition craftMode;
        craftMode.id = "mode";
        craftMode.condition = ui::StateCondition{ui::BindingPath::overlayCraftingTabCraft,
                                                 ui::ConditionOperator::equal, 1};
        craftMode.visual.visible = true;
        craftList.layout.visible = false;
        craftList.states.push_back(std::move(craftMode));
    }
    craftingChild(std::move(craftList));

    // Ingredients, output, quantity, feedback and hints (craft tab only).
    const auto craftingDetail = [&](const char* id, ui::BindingPath source, int x, int y) {
        ui::NodeDefinition node;
        node.id = id;
        node.component = ui::ComponentKind::text;
        node.layout.offsetX = x;
        node.layout.offsetY = y;
        node.layout.visible = false;
        node.bindings.push_back({"text", source});
        ui::StateDefinition craftMode;
        craftMode.id = "mode";
        craftMode.condition = ui::StateCondition{ui::BindingPath::overlayCraftingTabCraft,
                                                 ui::ConditionOperator::equal, 1};
        craftMode.visual.visible = true;
        node.states.push_back(std::move(craftMode));
        craftingChild(std::move(node));
    };
    craftingDetail("crafting.ingredient0", ui::BindingPath::craftingIngredient0, 11, 133);
    craftingDetail("crafting.ingredient1", ui::BindingPath::craftingIngredient1, 77, 133);
    craftingDetail("crafting.ingredient2", ui::BindingPath::craftingIngredient2, 11, 142);
    craftingDetail("crafting.ingredient3", ui::BindingPath::craftingIngredient3, 77, 142);
    craftingDetail("crafting.output", ui::BindingPath::craftingOutputLine, 150, 133);
    craftingDetail("crafting.qty", ui::BindingPath::craftingQtyLine, 150, 144);
    craftingDetail("crafting.feedback", ui::BindingPath::craftingFeedbackLine, 8, 177);
    ui::NodeDefinition craftingHints;
    craftingHints.id = "crafting.hints";
    craftingHints.component = ui::ComponentKind::text;
    craftingHints.layout.offsetX = 8;
    craftingHints.layout.offsetY = 188;
    craftingHints.layout.visible = false;
    craftingHints.text = "Z/E CRAFT  LEFT/RIGHT QTY  X BOOK  I CLOSE";
    {
        ui::StateDefinition craftMode;
        craftMode.id = "mode";
        craftMode.condition = ui::StateCondition{ui::BindingPath::overlayCraftingTabCraft,
                                                 ui::ConditionOperator::equal, 1};
        craftMode.visual.visible = true;
        craftingHints.states.push_back(std::move(craftMode));
    }
    craftingChild(std::move(craftingHints));

    // BOOK tab: every authored recipe with made/quest markers (the output
    // icon/silhouette joins when slot components gain silhouette support).
    ui::NodeDefinition bookList;
    bookList.id = "crafting.book.list";
    bookList.component = ui::ComponentKind::repeater;
    bookList.layout.offsetX = 8;
    bookList.layout.offsetY = 38;
    bookList.columns = 1;
    bookList.cellWidth = 250;
    bookList.cellHeight = 14;
    bookList.bindings.push_back({"source", ui::BindingPath::craftingRecipesPath});
    ui::NodeDefinition bookRow;
    bookRow.id = "crafting.book.row";
    bookRow.component = ui::ComponentKind::group;
    bookRow.layout.width = 250;
    bookRow.layout.height = 13;
    ui::StateDefinition bookRowSelected;
    bookRowSelected.id = "selected";
    bookRowSelected.condition = ui::StateCondition{
        ui::BindingPath::overlayCraftingBookRowSelected, ui::ConditionOperator::equal, 1};
    bookRowSelected.visual.background = core::ColorRGBA8{96, 62, 54, 255};
    bookRow.states.push_back(std::move(bookRowSelected));
    ui::NodeDefinition bookRowText;
    bookRowText.id = "crafting.book.line";
    bookRowText.component = ui::ComponentKind::text;
    bookRowText.layout.offsetX = 3;
    bookRowText.layout.offsetY = 1;
    bookRowText.bindings.push_back({"text", ui::BindingPath::contextBookLine});
    bookRow.children.push_back(std::move(bookRowText));
    bookList.children.push_back(std::move(bookRow));
    {
        ui::StateDefinition bookMode;
        bookMode.id = "mode";
        bookMode.condition = ui::StateCondition{ui::BindingPath::overlayCraftingTabBook,
                                                ui::ConditionOperator::equal, 1};
        bookMode.visual.visible = true;
        bookList.layout.visible = false;
        bookList.states.push_back(std::move(bookMode));
    }
    craftingChild(std::move(bookList));
    craftingScreen.root = std::move(craftingPanel);
    pack.uiScreens.push_back(std::move(craftingScreen));

    // Dialogue box as authored UI content: the speaker/page header, the
    // wrapped page text as a repeater over the composed lines, the numbered
    // choices as a repeater with a selection highlight (the session reports
    // zero choices while text pages advance, so no extra gating is needed),
    // and the advance hint gated on the choices visibility.
    ui::ScreenDefinition dialogueScreen;
    dialogueScreen.id = simulation::DefinitionId{"screen.dialogue"};
    dialogueScreen.kind = ui::ScreenKind::overlay;
    ui::NodeDefinition dialoguePanel;
    dialoguePanel.id = "dialogue.panel";
    dialoguePanel.component = ui::ComponentKind::panel;
    dialoguePanel.layout.offsetX = 8;
    dialoguePanel.layout.offsetY = 130;
    dialoguePanel.layout.width = 256;
    dialoguePanel.layout.height = 62;
    dialoguePanel.background = core::ColorRGBA8{8, 10, 16, 248};
    const auto dialogueChild = [&](ui::NodeDefinition node) {
        dialoguePanel.children.push_back(std::move(node));
    };
    ui::NodeDefinition dialogueInner;
    dialogueInner.id = "dialogue.panel.inner";
    dialogueInner.component = ui::ComponentKind::panel;
    dialogueInner.layout.offsetX = 9;
    dialogueInner.layout.offsetY = 131;
    dialogueInner.layout.width = 254;
    dialogueInner.layout.height = 60;
    dialogueInner.background = core::ColorRGBA8{54, 30, 38, 255};
    dialogueChild(std::move(dialogueInner));
    ui::NodeDefinition dialogueSpeaker;
    dialogueSpeaker.id = "dialogue.speaker";
    dialogueSpeaker.component = ui::ComponentKind::text;
    dialogueSpeaker.layout.offsetX = 14;
    dialogueSpeaker.layout.offsetY = 134;
    dialogueSpeaker.bindings.push_back({"text", ui::BindingPath::dialogueSpeaker});
    dialogueChild(std::move(dialogueSpeaker));
    ui::NodeDefinition dialoguePageCounter;
    dialoguePageCounter.id = "dialogue.page.counter";
    dialoguePageCounter.component = ui::ComponentKind::text;
    dialoguePageCounter.layout.offsetX = 238;
    dialoguePageCounter.layout.offsetY = 134;
    dialoguePageCounter.bindings.push_back({"text", ui::BindingPath::dialoguePageText});
    dialogueChild(std::move(dialoguePageCounter));
    ui::NodeDefinition dialoguePageLines;
    dialoguePageLines.id = "dialogue.page.lines";
    dialoguePageLines.component = ui::ComponentKind::repeater;
    dialoguePageLines.layout.offsetX = 14;
    dialoguePageLines.layout.offsetY = 145;
    dialoguePageLines.columns = 1;
    dialoguePageLines.cellWidth = 250;
    dialoguePageLines.cellHeight = 9; // bitmap font line height
    dialoguePageLines.bindings.push_back({"source", ui::BindingPath::dialoguePageLines});
    ui::NodeDefinition dialoguePageLine;
    dialoguePageLine.id = "dialogue.page.line";
    dialoguePageLine.component = ui::ComponentKind::text;
    dialoguePageLine.bindings.push_back({"text", ui::BindingPath::contextPageLine});
    dialoguePageLines.children.push_back(std::move(dialoguePageLine));
    dialogueChild(std::move(dialoguePageLines));
    ui::NodeDefinition dialogueChoices;
    dialogueChoices.id = "dialogue.choices";
    dialogueChoices.component = ui::ComponentKind::repeater;
    dialogueChoices.layout.offsetX = 12;
    dialogueChoices.layout.offsetY = 163;
    dialogueChoices.columns = 1;
    dialogueChoices.cellWidth = 244;
    dialogueChoices.cellHeight = 11;
    dialogueChoices.bindings.push_back({"source", ui::BindingPath::dialogueChoices});
    ui::NodeDefinition dialogueChoiceRow;
    dialogueChoiceRow.id = "dialogue.choice.row";
    dialogueChoiceRow.component = ui::ComponentKind::group;
    dialogueChoiceRow.layout.width = 244;
    dialogueChoiceRow.layout.height = 10;
    ui::StateDefinition dialogueChoiceSelected;
    dialogueChoiceSelected.id = "selected";
    dialogueChoiceSelected.condition = ui::StateCondition{
        ui::BindingPath::overlayDialogueChoiceSelected, ui::ConditionOperator::equal, 1};
    dialogueChoiceSelected.visual.background = core::ColorRGBA8{96, 62, 54, 255};
    dialogueChoiceRow.states.push_back(std::move(dialogueChoiceSelected));
    ui::NodeDefinition dialogueChoiceLine;
    dialogueChoiceLine.id = "dialogue.choice.line";
    dialogueChoiceLine.component = ui::ComponentKind::text;
    dialogueChoiceLine.layout.offsetX = 2;
    dialogueChoiceLine.layout.offsetY = 1;
    dialogueChoiceLine.bindings.push_back({"text", ui::BindingPath::contextChoiceLine});
    dialogueChoiceRow.children.push_back(std::move(dialogueChoiceLine));
    dialogueChoices.children.push_back(std::move(dialogueChoiceRow));
    dialogueChild(std::move(dialogueChoices));
    ui::NodeDefinition dialogueHints;
    dialogueHints.id = "dialogue.hints";
    dialogueHints.component = ui::ComponentKind::text;
    dialogueHints.layout.offsetX = 14;
    dialogueHints.layout.offsetY = 181;
    dialogueHints.layout.visible = false;
    dialogueHints.text = "E NEXT  X CLOSE";
    ui::StateDefinition dialogueHintsShow;
    dialogueHintsShow.id = "text-mode";
    dialogueHintsShow.condition = ui::StateCondition{ui::BindingPath::dialogueChoicesVisible,
                                                     ui::ConditionOperator::equal, 0};
    dialogueHintsShow.visual.visible = true;
    dialogueHints.states.push_back(std::move(dialogueHintsShow));
    dialogueChild(std::move(dialogueHints));
    dialogueScreen.root = std::move(dialoguePanel);
    pack.uiScreens.push_back(std::move(dialogueScreen));
    return pack;
}

} // namespace underworld::game::content
