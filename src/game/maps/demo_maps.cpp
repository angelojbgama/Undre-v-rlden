#include "game/maps/demo_maps.h"

#include "engine/core/game_metrics.h"
#include "game/gameplay/creatures/creature_engine.h"

#include <array>

namespace underworld::game::maps {
namespace {

constexpr std::uint32_t width = 64;
constexpr std::uint32_t height = 48;
constexpr std::uint32_t atlasColumns = 19;
constexpr std::uint32_t atlasIndex(std::uint32_t x, std::uint32_t y) {
    return y * atlasColumns + x;
}
constexpr std::uint32_t floorPlain = atlasIndex(10, 4);
constexpr std::uint32_t floorCracked = atlasIndex(11, 4);
constexpr std::uint32_t wallHorizontal = atlasIndex(11, 2);
constexpr std::uint32_t wallVertical = atlasIndex(1, 6);
constexpr std::uint32_t wallLowerFace = atlasIndex(4, 9);
constexpr std::uint32_t floorMarkFirst = atlasIndex(14, 4);
constexpr std::array<std::uint32_t, 3> archTiles{
    atlasIndex(4, 0), atlasIndex(5, 0), atlasIndex(6, 0)};

std::size_t cell(std::uint32_t x, std::uint32_t y) {
    return static_cast<std::size_t>(y) * width + x;
}

MapData makeRoom(simulation::MapId id, bool roomA) {
    MapData map;
    map.id = std::move(id);
    map.width = width;
    map.height = height;
    map.tileSize = core::GameMetrics::tileSize;
    map.tileReferences.reserve(19U * 12U * 2U);
    for (std::uint32_t source = 0; source < 19U * 12U; ++source) {
        map.tileReferences.push_back(
            {simulation::DefinitionId{"tileset.dungeon"}, source, world::TileFlags::none});
        map.tileReferences.push_back(
            {simulation::DefinitionId{"tileset.dungeon"}, source, world::TileFlags::flipX});
    }
    const auto tile = [](std::uint32_t source, bool flip = false) {
        return source * 2U + (flip ? 1U : 0U);
    };
    map.layers.push_back({"ground", true, std::vector<std::optional<std::uint32_t>>(width * height)});
    map.layers.push_back({"decoration_low", true, std::vector<std::optional<std::uint32_t>>(width * height)});
    map.layers.push_back({"foreground", true, std::vector<std::optional<std::uint32_t>>(width * height)});
    map.collision.assign(width * height, 0);
    auto& ground = map.layers[0].cells;
    auto& low = map.layers[1].cells;
    auto& foreground = map.layers[2].cells;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            ground[cell(x, y)] = tile((x * 7U + y * 11U) % 29U == 0U
                                          ? floorCracked : floorPlain);
        }
    }
    const auto horizontal = [&](std::uint32_t first, std::uint32_t last,
                                std::uint32_t y) {
        for (std::uint32_t x = first; x <= last; ++x) {
            low[cell(x, y)] = tile(wallHorizontal);
            map.collision[cell(x, y)] = 1;
            if (y + 1U < height) { foreground[cell(x, y + 1U)] = tile(wallLowerFace); }
        }
    };
    const auto vertical = [&](std::uint32_t x, std::uint32_t first, std::uint32_t last,
                              bool flip, std::uint32_t gapFirst, std::uint32_t gapLast) {
        for (std::uint32_t y = first; y <= last; ++y) {
            if (y >= gapFirst && y <= gapLast) { continue; }
            low[cell(x, y)] = tile(wallVertical, flip);
            map.collision[cell(x, y)] = 1;
        }
    };
    horizontal(2, 61, 2);
    horizontal(2, 61, 45);
    vertical(2, 3, 44, false, roomA ? 100U : 8U, roomA ? 99U : 12U);
    vertical(61, 3, 44, true, roomA ? 8U : 100U, roomA ? 12U : 99U);
    horizontal(2, 10, 15);
    horizontal(14, 28, 15);
    vertical(28, 2, 28, true, 8, 12);
    horizontal(28, 43, 28);
    horizontal(47, 61, 28);
    vertical(44, 28, 45, false, 36, 38);
    for (std::uint32_t index = 0; index < archTiles.size(); ++index) {
        foreground[cell(11U + index, 14)] = tile(archTiles[index]);
    }
    for (std::uint32_t y = 5; y < height - 3U; y += 7U) {
        for (std::uint32_t x = 5; x < width - 3U; x += 11U) {
            if (map.collision[cell(x, y)] == 0) {
                low[cell(x, y)] = tile(floorMarkFirst + (x + y) % 5U);
            }
        }
    }

    if (roomA) {
        map.playerSpawns = {
            {simulation::SpawnId{"entry.start"}, {104, 144}, gameplay::FacingDirection::down},
            {simulation::SpawnId{"entry.from_b"}, {928, 160}, gameplay::FacingDirection::left}};
        map.enemies.push_back({{5}, gameplay::creatures::soldierEnemyId(), {220, 320},
                               gameplay::FacingDirection::left});
        map.objects.push_back({{1}, simulation::DefinitionId{"object.chest"}, {152, 144},
                               {{gameplay::lifePotionItemId(), 2}}});
        map.objects.push_back({{2}, simulation::DefinitionId{"object.crate"}, {184, 144}, {}});
        map.pickups.push_back({{3}, simulation::DefinitionId{"pickup.money"},
            simulation::DefinitionId{"visual.pickup.money"}, {120, 144}, {-5, -5, 10, 10},
            gameplay::CurrencyPickup{1}});
        map.pickups.push_back({{4}, simulation::DefinitionId{"pickup.life_potion"},
            simulation::DefinitionId{"visual.item.life_potion"}, {88, 144}, {-5, -5, 10, 10},
            gameplay::ItemPickup{gameplay::lifePotionItemId(), 4}});
        map.links.push_back({"exit.east", {960, 128, 16, 80}, demoRoomBId(),
                             simulation::SpawnId{"entry.from_a"}});
    } else {
        map.playerSpawns = {
            {simulation::SpawnId{"entry.from_a"}, {64, 160}, gameplay::FacingDirection::right}};
        map.enemies.push_back({{1}, gameplay::creatures::skullEnemyId(), {168, 320},
                               gameplay::FacingDirection::left});
        map.enemies.push_back({{2}, gameplay::creatures::soldierEnemyId(), {240, 384},
                               gameplay::FacingDirection::left});
        map.pickups.push_back({{3}, simulation::DefinitionId{"pickup.heart"},
            simulation::DefinitionId{"visual.pickup.heart"}, {136, 208}, {-5, -5, 10, 10},
            gameplay::HealthPickup{2}});
        map.links.push_back({"exit.west", {32, 128, 16, 80}, demoRoomAId(),
                             simulation::SpawnId{"entry.from_b"}});
    }
    return map;
}

MapData makeEditorSmokeRoom() {
    constexpr std::uint32_t smokeWidth = 32;
    constexpr std::uint32_t smokeHeight = 24;
    const auto smokeCell = [](std::uint32_t x, std::uint32_t y) {
        return static_cast<std::size_t>(y) * smokeWidth + x;
    };

    MapData map;
    map.id = editorSmokeMapId();
    map.width = smokeWidth;
    map.height = smokeHeight;
    map.tileSize = core::GameMetrics::tileSize;
    map.tileReferences = {
        {simulation::DefinitionId{"tileset.dungeon"}, floorPlain, world::TileFlags::none},
        {simulation::DefinitionId{"tileset.dungeon"}, floorCracked, world::TileFlags::none},
        {simulation::DefinitionId{"tileset.dungeon"}, wallHorizontal, world::TileFlags::none},
        {simulation::DefinitionId{"tileset.dungeon"}, wallVertical, world::TileFlags::none},
        {simulation::DefinitionId{"tileset.dungeon"}, wallVertical, world::TileFlags::flipX},
    };
    map.layers.push_back({"ground", true,
        std::vector<std::optional<std::uint32_t>>(smokeWidth * smokeHeight)});
    map.layers.push_back({"walls", true,
        std::vector<std::optional<std::uint32_t>>(smokeWidth * smokeHeight)});
    map.collision.assign(smokeWidth * smokeHeight, 0);

    for (std::uint32_t y = 0; y < smokeHeight; ++y) {
        for (std::uint32_t x = 0; x < smokeWidth; ++x) {
            map.layers[0].cells[smokeCell(x, y)] =
                ((x + y * 3U) % 11U == 0U) ? 1U : 0U;
        }
    }
    const auto wall = [&](std::uint32_t x, std::uint32_t y, std::uint32_t tile) {
        map.layers[1].cells[smokeCell(x, y)] = tile;
        map.collision[smokeCell(x, y)] = 1;
    };
    for (std::uint32_t x = 1; x <= 30; ++x) {
        wall(x, 1, 2);
        wall(x, 22, 2);
    }
    for (std::uint32_t y = 2; y <= 21; ++y) {
        wall(1, y, 3);
        if (y < 10 || y > 12) { wall(30, y, 4); }
    }
    for (std::uint32_t x = 10; x <= 21; ++x) { wall(x, 14, 2); }

    map.playerSpawns = {
        {simulation::SpawnId{"entry.start"}, {64, 112}, gameplay::FacingDirection::down},
    };
    map.enemies = {
        {{101}, gameplay::creatures::soldierEnemyId(), {160, 160},
         gameplay::FacingDirection::left},
        {{102}, gameplay::creatures::skullEnemyId(), {288, 160},
         gameplay::FacingDirection::right},
    };
    map.objects = {
        {{201}, simulation::DefinitionId{"object.chest"}, {176, 240},
         {{gameplay::lifePotionItemId(), 2}}},
        {{202}, simulation::DefinitionId{"object.crate"}, {224, 240}, {}},
    };
    map.pickups = {
        {{301}, simulation::DefinitionId{"pickup.money"},
         simulation::DefinitionId{"visual.pickup.money"}, {112, 240}, {-5, -5, 10, 10},
         gameplay::CurrencyPickup{7}},
        {{302}, simulation::DefinitionId{"pickup.life_potion"},
         simulation::DefinitionId{"visual.item.life_potion"}, {128, 240}, {-5, -5, 10, 10},
         gameplay::ItemPickup{gameplay::lifePotionItemId(), 1}},
    };
    map.links = {{"exit.self", {480, 160, 16, 48}, editorSmokeMapId(),
                  simulation::SpawnId{"entry.start"}}};
    return map;
}

} // namespace

const simulation::MapId& demoRoomAId() {
    static const simulation::MapId id{"map.demo.room_a"};
    return id;
}
const simulation::MapId& demoRoomBId() {
    static const simulation::MapId id{"map.demo.room_b"};
    return id;
}
const simulation::MapId& editorSmokeMapId() {
    static const simulation::MapId id{"map.editor.smoke"};
    return id;
}
MapData makeDemoRoomA() { return makeRoom(demoRoomAId(), true); }
MapData makeDemoRoomB() { return makeRoom(demoRoomBId(), false); }
MapData makeEditorSmokeMap() { return makeEditorSmokeRoom(); }

} // namespace underworld::game::maps
