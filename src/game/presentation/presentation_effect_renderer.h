#pragma once

#include "engine/core/coordinates.h"
#include "game/presentation/presentation_effects.h"

namespace underworld::render { class Framebuffer; }

namespace underworld::game::presentation {

class PresentationEffectRenderer final {
public:
    static void applyWorld(render::Framebuffer& framebuffer,
                           const PresentationEffectFrame& frame,
                           core::LogicalPointI visionCenter) noexcept;
    static void applyFinal(render::Framebuffer& framebuffer,
                           const PresentationEffectFrame& frame) noexcept;
};

} // namespace underworld::game::presentation
