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

    // HUD health hearts as authored UI content (docs/UI_ENGINE.md proof 1a):
    // the definition reproduces the legacy HUD bar hearts pixel-for-pixel —
    // full segment Icons/heart_complete.png at (3 + i*12, 2), empty segments
    // as the legacy inset {54,30,38} rect at (+0,+1). Workspaces override by
    // definition id only.
    pack.visualImages.push_back(
        {{ "img.hud.heart" }, presentation::VisualAssetRoot::gameAssets, "Icons/heart_complete.png"});
    pack.staticSprites.push_back(
        {{ "spr.hud.heart" }, { "img.hud.heart" }, std::nullopt, core::PointI{0, 0}});
    ui::ScreenDefinition hudScreen;
    hudScreen.id = simulation::DefinitionId{"screen.hud"};
    hudScreen.kind = ui::ScreenKind::hud;
    ui::NodeDefinition hearts;
    hearts.id = "hud.health";
    hearts.component = ui::ComponentKind::meter;
    hearts.layout.anchor = ui::Anchor::topLeft;
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
    hudScreen.root = std::move(hearts);
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
    menuPanel.layout.height = 96;
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
        makeButton("menu.resume", "RESUME", 128, ui::ActionId::screenClose));
    menuScreen.root = std::move(menuPanel);
    pack.uiScreens.push_back(std::move(menuScreen));
    return pack;
}

} // namespace underworld::game::content
