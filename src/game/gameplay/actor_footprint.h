#pragma once

#include "engine/core/coordinates.h"
#include "engine/world/collision.h"
#include "game/gameplay/facing_direction.h"

#include <array>
#include <vector>

namespace underworld::game::gameplay {

// Stable ground footprint used for movement collision. It is intentionally
// independent from sprite alpha, animation frames and combat hurtboxes.
struct ActorFootprintDefinition final {
    int offsetX{};
    int offsetY{};
    int width{};
    int height{};

    [[nodiscard]] world::AabbI at(core::WorldPointI feet) const noexcept {
        return {feet.x + offsetX, feet.y + offsetY, width, height};
    }

    [[nodiscard]] bool valid() const noexcept {
        return width > 0 && height > 0;
    }

    [[nodiscard]] bool operator==(const ActorFootprintDefinition&) const noexcept = default;
};

// Stable per-facing footprints. They may differ between directions, but never
// between animation frames, so visual animation cannot jitter movement physics.
struct DirectionalActorFootprints final {
    std::array<ActorFootprintDefinition, 4> values{}; // down, up, left, right

    [[nodiscard]] const ActorFootprintDefinition& forFacing(
        FacingDirection facing) const noexcept {
        switch (facing) {
        case FacingDirection::down: return values[0];
        case FacingDirection::up: return values[1];
        case FacingDirection::left: return values[2];
        case FacingDirection::right: return values[3];
        }
        return values[0];
    }

    [[nodiscard]] bool valid() const noexcept {
        for (const auto& footprint : values) {
            if (!footprint.valid()) return false;
        }
        return true;
    }
};


// Compact stable movement shape. Authoring masks are compiled into these
// rectangles once; runtime movement never samples sprite pixels.
struct ActorCollisionShapeDefinition final {
    std::vector<ActorFootprintDefinition> regions;

    [[nodiscard]] bool valid() const noexcept {
        if (regions.empty()) return false;
        for (const auto& region : regions) {
            if (!region.valid()) return false;
        }
        return true;
    }

    [[nodiscard]] std::vector<world::AabbI> at(
        core::WorldPointI feet) const {
        std::vector<world::AabbI> result;
        result.reserve(regions.size());
        for (const auto& region : regions) {
            result.push_back(region.at(feet));
        }
        return result;
    }
};

struct DirectionalActorCollisionShapes final {
    std::array<ActorCollisionShapeDefinition, 4> values{}; // down/up/left/right

    [[nodiscard]] const ActorCollisionShapeDefinition& forFacing(
        FacingDirection facing) const noexcept {
        switch (facing) {
        case FacingDirection::down: return values[0];
        case FacingDirection::up: return values[1];
        case FacingDirection::left: return values[2];
        case FacingDirection::right: return values[3];
        }
        return values[0];
    }

    [[nodiscard]] bool valid() const noexcept {
        for (const auto& shape : values) {
            if (!shape.valid()) return false;
        }
        return true;
    }
};

} // namespace underworld::game::gameplay
