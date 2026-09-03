#include "game/training_puppet.h"

namespace underworld::game {

TrainingPuppet::TrainingPuppet(simulation::EntityHandle handle, core::WorldPointI feet)
    : target_{handle,
              gameplay::Faction::enemy,
              feet,
              {{feet.x - 5, feet.y - 8, 10, 8}},
              {{feet.x - 7, feet.y - 22, 14, 22}, true},
              gameplay::Health{maximumHealth},
              0,
              false} {}

} // namespace underworld::game
