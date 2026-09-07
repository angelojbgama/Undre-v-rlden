#pragma once

#include "engine/simulation/events.h"
#include "game/maps/map_data.h"
#include "game/presentation/presentation_effects.h"

namespace underworld::game::presentation {

class PresentationFeedbackController final {
public:
    void consume(const simulation::EventBuffer& events, const maps::MapData& map,
                 simulation::EntityHandle player, PresentationEffectSystem& effects) const;
};

} // namespace underworld::game::presentation
