#include "game/gameplay/attack_definitions.h"

namespace underworld::game::gameplay {

world::AabbI swordHitboxBounds(core::WorldPointI feet, FacingDirection facing) noexcept {
    switch (facing) {
    case FacingDirection::down: return {feet.x - 10, feet.y - 1, 20, 18};
    case FacingDirection::up: return {feet.x - 10, feet.y - 27, 20, 19};
    case FacingDirection::left: return {feet.x - 27, feet.y - 18, 21, 18};
    case FacingDirection::right: return {feet.x + 6, feet.y - 18, 21, 18};
    }
    return {};
}

core::WorldPointI directionVector(FacingDirection facing) noexcept {
    switch (facing) {
    case FacingDirection::down: return {0, 1};
    case FacingDirection::up: return {0, -1};
    case FacingDirection::left: return {-1, 0};
    case FacingDirection::right: return {1, 0};
    }
    return {};
}

core::WorldPointI arrowSpawnPosition(core::WorldPointI feet, FacingDirection facing) noexcept {
    switch (facing) {
    case FacingDirection::down: return {feet.x, feet.y + 3};
    case FacingDirection::up: return {feet.x, feet.y - 20};
    case FacingDirection::left: return {feet.x - 12, feet.y - 10};
    case FacingDirection::right: return {feet.x + 12, feet.y - 10};
    }
    return feet;
}

} // namespace underworld::game::gameplay
