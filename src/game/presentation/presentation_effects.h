#pragma once

#include "engine/core/color_rgba8.h"
#include "engine/core/coordinates.h"
#include "engine/simulation/definition_id.h"
#include "engine/simulation/persistent_id.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace underworld::render { class Framebuffer; }

namespace underworld::game::presentation {

enum class PresentationEffectLifetime { transient, persistent };
enum class PresentationOverlayMode { constant, linearFadeOut, pulse };
enum class PresentationCompositionLayer { world, final };
enum class PresentationEffectSourceKind { region };

struct CameraShakeDefinition final {
    int amplitudePixels{};
    [[nodiscard]] bool operator==(const CameraShakeDefinition&) const noexcept = default;
};

struct ColorOverlayDefinition final {
    core::ColorRGBA8 color{};
    PresentationOverlayMode mode{PresentationOverlayMode::constant};
    std::uint32_t pulsePeriodTicks{};
    PresentationCompositionLayer layer{PresentationCompositionLayer::world};
    [[nodiscard]] bool operator==(const ColorOverlayDefinition&) const noexcept = default;
};

struct VisionMaskDefinition final {
    int innerRadiusPixels{};
    int outerRadiusPixels{};
    std::uint8_t outsideAlpha{};
    core::ColorRGBA8 color{0, 0, 0, 255};
    [[nodiscard]] bool operator==(const VisionMaskDefinition&) const noexcept = default;
};

struct FadeDefinition final {
    core::ColorRGBA8 color{};
    std::uint8_t startAlpha{};
    std::uint8_t endAlpha{};
    [[nodiscard]] bool operator==(const FadeDefinition&) const noexcept = default;
};

struct PresentationEffectDefinition final {
    simulation::DefinitionId id{};
    PresentationEffectLifetime lifetime{PresentationEffectLifetime::transient};
    std::uint32_t durationTicks{};
    int priority{};
    std::optional<CameraShakeDefinition> cameraShake{};
    std::optional<ColorOverlayDefinition> overlay{};
    std::optional<VisionMaskDefinition> visionMask{};
    std::optional<FadeDefinition> fade{};
    [[nodiscard]] bool operator==(const PresentationEffectDefinition&) const noexcept = default;
};

class PresentationEffectCatalog final {
public:
    void add(PresentationEffectDefinition definition);
    [[nodiscard]] const PresentationEffectDefinition* find(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const PresentationEffectDefinition& require(
        const simulation::DefinitionId& id) const;
    [[nodiscard]] const std::vector<PresentationEffectDefinition>& definitions() const noexcept {
        return definitions_;
    }

private:
    std::vector<PresentationEffectDefinition> definitions_;
};

struct PresentationEffectSourceKey final {
    PresentationEffectSourceKind kind{PresentationEffectSourceKind::region};
    simulation::MapId mapId{};
    simulation::DefinitionId sourceId{};
    [[nodiscard]] bool operator==(const PresentationEffectSourceKey&) const noexcept = default;
};

struct ResolvedOverlay final {
    simulation::DefinitionId effectId{};
    int priority{};
    core::ColorRGBA8 color{};
    PresentationCompositionLayer layer{PresentationCompositionLayer::world};
    [[nodiscard]] bool operator==(const ResolvedOverlay&) const noexcept = default;
};

struct ResolvedVisionMask final {
    simulation::DefinitionId effectId{};
    int priority{};
    int innerRadiusPixels{};
    int outerRadiusPixels{};
    std::uint8_t outsideAlpha{};
    core::ColorRGBA8 color{};
    [[nodiscard]] bool operator==(const ResolvedVisionMask&) const noexcept = default;
};

struct ResolvedFade final {
    simulation::DefinitionId effectId{};
    int priority{};
    core::ColorRGBA8 color{};
    std::uint8_t alpha{};
    [[nodiscard]] bool operator==(const ResolvedFade&) const noexcept = default;
};

struct PresentationEffectFrame final {
    core::LogicalPointI cameraOffset{};
    std::vector<ResolvedOverlay> worldOverlays;
    std::optional<ResolvedVisionMask> visionMask{};
    std::vector<ResolvedOverlay> finalOverlays;
    std::vector<ResolvedFade> fades;
};

class PresentationEffectSystem final {
public:
    explicit PresentationEffectSystem(const PresentationEffectCatalog& catalog) noexcept
        : catalog_(catalog) {}

    [[nodiscard]] bool play(const simulation::DefinitionId& effectId);
    [[nodiscard]] bool activatePersistent(const simulation::DefinitionId& effectId,
                                          const PresentationEffectSourceKey& source);
    [[nodiscard]] bool deactivatePersistent(const simulation::DefinitionId& effectId,
                                            const PresentationEffectSourceKey& source);
    void advance(std::uint32_t ticks = 1) noexcept;
    void clearTransients() noexcept;
    void clearAll() noexcept;
    void clearPersistentSourcesForMap(const simulation::MapId& mapId) noexcept;

    [[nodiscard]] bool isActive(const simulation::DefinitionId& effectId) const noexcept;
    [[nodiscard]] std::size_t persistentSourceCount(
        const simulation::DefinitionId& effectId) const noexcept;
    [[nodiscard]] PresentationEffectFrame resolveFrame() const;

private:
    struct ActiveTransient final {
        simulation::DefinitionId effectId{};
        std::uint32_t elapsedTicks{};
    };
    struct PersistentActivation final {
        simulation::DefinitionId effectId{};
        std::uint32_t elapsedTicks{};
        std::vector<PresentationEffectSourceKey> sources;
    };

    [[nodiscard]] const PresentationEffectDefinition* definition(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] std::vector<const PresentationEffectDefinition*> activeDefinitions() const;

    const PresentationEffectCatalog& catalog_;
    std::vector<ActiveTransient> transients_;
    std::vector<PersistentActivation> persistent_;
};

} // namespace underworld::game::presentation
