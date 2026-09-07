#include "game/presentation/presentation_feedback_controller.h"

#include <algorithm>
#include <variant>

namespace underworld::game::presentation {

void PresentationFeedbackController::consume(const simulation::EventBuffer& events,
                                              const maps::MapData& map,
                                              simulation::EntityHandle player,
                                              PresentationEffectSystem& effects) const {
    for (const auto& event : events.events()) {
        if (const auto* entered = std::get_if<simulation::MapEntered>(&event)) {
            static_cast<void>(entered);
            // Presentation state is derived from the active map's region events.  A map
            // activation must not carry sources or transients from the previous map.
            effects.clearAll();
        } else if (const auto* damaged = std::get_if<simulation::EntityDamaged>(&event)) {
            if (damaged->target == player) {
                static_cast<void>(effects.play({"effect.player.hit"}));
            }
        } else if (const auto* requested =
                   std::get_if<simulation::PresentationEffectRequested>(&event)) {
            if (requested->mapId.empty() || requested->mapId == map.id) {
                static_cast<void>(effects.play(requested->effectId));
            }
        } else if (const auto* entered = std::get_if<simulation::RegionEntered>(&event)) {
            if (entered->mapId != map.id) continue;
            const auto region = std::find_if(map.regions.begin(), map.regions.end(),
                [&](const auto& value) { return value.id == entered->regionId; });
            if (region != map.regions.end() && region->environmentEffectId) {
                static_cast<void>(effects.activatePersistent(
                    *region->environmentEffectId,
                    {PresentationEffectSourceKind::region, map.id, region->id}));
            }
        } else if (const auto* exited = std::get_if<simulation::RegionExited>(&event)) {
            if (exited->mapId != map.id) continue;
            const auto region = std::find_if(map.regions.begin(), map.regions.end(),
                [&](const auto& value) { return value.id == exited->regionId; });
            if (region != map.regions.end() && region->environmentEffectId) {
                static_cast<void>(effects.deactivatePersistent(
                    *region->environmentEffectId,
                    {PresentationEffectSourceKind::region, map.id, region->id}));
            }
        }
    }
}

} // namespace underworld::game::presentation
