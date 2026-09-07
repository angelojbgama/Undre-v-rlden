#include "game/presentation/presentation_effects.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace underworld::game::presentation {
namespace {

constexpr std::array<core::LogicalPointI, 8> shakePattern{{
    {0, 0}, {1, -1}, {-1, 1}, {1, 1}, {-1, -1}, {1, 0}, {0, -1}, {-1, 0}}};

std::uint8_t scaleAlpha(std::uint8_t alpha, std::uint32_t numerator,
                       std::uint32_t denominator) noexcept {
    if (denominator == 0U) return 0U;
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(alpha) * numerator + denominator / 2U) / denominator);
}

std::uint8_t pulseAlpha(const ColorOverlayDefinition& overlay,
                        std::uint32_t elapsed) noexcept {
    if (overlay.pulsePeriodTicks <= 1U) return overlay.color.a;
    const std::uint32_t period = overlay.pulsePeriodTicks;
    const std::uint32_t half = std::max(1U, period / 2U);
    const std::uint32_t phase = elapsed % period;
    if (phase <= half) return scaleAlpha(overlay.color.a, phase, half);
    return scaleAlpha(overlay.color.a, period - phase, period - half);
}

std::uint32_t linearNumerator(const PresentationEffectDefinition& effect,
                             std::uint32_t elapsed) noexcept {
    if (effect.durationTicks <= 1U) return 0U;
    return effect.durationTicks - 1U - std::min(elapsed, effect.durationTicks - 1U);
}

std::uint32_t linearDenominator(const PresentationEffectDefinition& effect) noexcept {
    return effect.durationTicks <= 1U ? 1U : effect.durationTicks - 1U;
}

bool definitionBefore(const PresentationEffectDefinition* left,
                      const PresentationEffectDefinition* right) noexcept {
    return left->id.value() < right->id.value();
}

} // namespace

void PresentationEffectCatalog::add(PresentationEffectDefinition definition) {
    if (definition.id.empty()) throw std::invalid_argument("presentation effect ID is empty");
    if (find(definition.id) != nullptr) {
        throw std::invalid_argument("duplicate presentation effect definition");
    }
    definitions_.push_back(std::move(definition));
}

const PresentationEffectDefinition* PresentationEffectCatalog::find(
    const simulation::DefinitionId& id) const noexcept {
    const auto found = std::find_if(definitions_.begin(), definitions_.end(),
        [&](const auto& value) { return value.id == id; });
    return found == definitions_.end() ? nullptr : &*found;
}

const PresentationEffectDefinition& PresentationEffectCatalog::require(
    const simulation::DefinitionId& id) const {
    const auto* found = find(id);
    if (!found) throw std::out_of_range("presentation effect definition does not exist");
    return *found;
}

const PresentationEffectDefinition* PresentationEffectSystem::definition(
    const simulation::DefinitionId& id) const noexcept {
    return catalog_.find(id);
}

bool PresentationEffectSystem::play(const simulation::DefinitionId& effectId) {
    const auto* effect = definition(effectId);
    if (!effect || effect->lifetime != PresentationEffectLifetime::transient) return false;
    const auto found = std::find_if(transients_.begin(), transients_.end(),
        [&](const auto& value) { return value.effectId == effectId; });
    if (found == transients_.end()) transients_.push_back({effectId, 0});
    else found->elapsedTicks = 0;
    return true;
}

bool PresentationEffectSystem::activatePersistent(
    const simulation::DefinitionId& effectId, const PresentationEffectSourceKey& source) {
    const auto* effect = definition(effectId);
    if (!effect || effect->lifetime != PresentationEffectLifetime::persistent) return false;
    auto found = std::find_if(persistent_.begin(), persistent_.end(),
        [&](const auto& value) { return value.effectId == effectId; });
    if (found == persistent_.end()) {
        persistent_.push_back({effectId, 0, {source}});
        return true;
    }
    if (std::find(found->sources.begin(), found->sources.end(), source) == found->sources.end()) {
        found->sources.push_back(source);
    }
    return true;
}

bool PresentationEffectSystem::deactivatePersistent(
    const simulation::DefinitionId& effectId, const PresentationEffectSourceKey& source) {
    const auto found = std::find_if(persistent_.begin(), persistent_.end(),
        [&](const auto& value) { return value.effectId == effectId; });
    if (found == persistent_.end()) return false;
    const auto sourceIt = std::find(found->sources.begin(), found->sources.end(), source);
    if (sourceIt == found->sources.end()) return false;
    found->sources.erase(sourceIt);
    if (found->sources.empty()) persistent_.erase(found);
    return true;
}

void PresentationEffectSystem::advance(std::uint32_t ticks) noexcept {
    for (auto& value : persistent_) value.elapsedTicks += ticks;
    for (auto& value : transients_) {
        const auto* effect = definition(value.effectId);
        if (effect && ticks < effect->durationTicks - std::min(value.elapsedTicks, effect->durationTicks)) {
            value.elapsedTicks += ticks;
        } else if (effect) {
            value.elapsedTicks = effect->durationTicks;
        }
    }
    transients_.erase(std::remove_if(transients_.begin(), transients_.end(), [&](const auto& value) {
        const auto* effect = definition(value.effectId);
        return effect == nullptr || value.elapsedTicks >= effect->durationTicks;
    }), transients_.end());
}

void PresentationEffectSystem::clearTransients() noexcept { transients_.clear(); }

void PresentationEffectSystem::clearAll() noexcept {
    transients_.clear();
    persistent_.clear();
}

void PresentationEffectSystem::clearPersistentSourcesForMap(
    const simulation::MapId& mapId) noexcept {
    for (auto& value : persistent_) {
        value.sources.erase(std::remove_if(value.sources.begin(), value.sources.end(),
            [&](const auto& source) { return source.mapId == mapId; }), value.sources.end());
    }
    persistent_.erase(std::remove_if(persistent_.begin(), persistent_.end(),
        [](const auto& value) { return value.sources.empty(); }), persistent_.end());
}

bool PresentationEffectSystem::isActive(
    const simulation::DefinitionId& effectId) const noexcept {
    return std::any_of(transients_.begin(), transients_.end(),
        [&](const auto& value) { return value.effectId == effectId; }) ||
        std::any_of(persistent_.begin(), persistent_.end(),
        [&](const auto& value) { return value.effectId == effectId && !value.sources.empty(); });
}

std::size_t PresentationEffectSystem::persistentSourceCount(
    const simulation::DefinitionId& effectId) const noexcept {
    const auto found = std::find_if(persistent_.begin(), persistent_.end(),
        [&](const auto& value) { return value.effectId == effectId; });
    return found == persistent_.end() ? 0U : found->sources.size();
}

std::vector<const PresentationEffectDefinition*>
PresentationEffectSystem::activeDefinitions() const {
    std::vector<const PresentationEffectDefinition*> result;
    result.reserve(transients_.size() + persistent_.size());
    for (const auto& value : transients_) {
        if (const auto* effect = definition(value.effectId)) result.push_back(effect);
    }
    for (const auto& value : persistent_) {
        if (!value.sources.empty()) {
            if (const auto* effect = definition(value.effectId)) result.push_back(effect);
        }
    }
    std::sort(result.begin(), result.end(), definitionBefore);
    return result;
}

PresentationEffectFrame PresentationEffectSystem::resolveFrame() const {
    PresentationEffectFrame result;
    const auto effects = activeDefinitions();
    int shakeX = 0;
    int shakeY = 0;
    for (const auto* effect : effects) {
        std::uint32_t elapsed = 0;
        if (const auto found = std::find_if(transients_.begin(), transients_.end(),
                [&](const auto& value) { return value.effectId == effect->id; });
            found != transients_.end()) {
            elapsed = found->elapsedTicks;
        } else if (const auto found = std::find_if(persistent_.begin(), persistent_.end(),
                       [&](const auto& value) { return value.effectId == effect->id; });
                   found != persistent_.end()) {
            elapsed = found->elapsedTicks;
        }

        if (effect->cameraShake) {
            const auto& pattern = shakePattern[elapsed % shakePattern.size()];
            const auto numerator = linearNumerator(*effect, elapsed);
            const auto denominator = linearDenominator(*effect);
            const int amount = static_cast<int>(scaleAlpha(
                static_cast<std::uint8_t>(std::min(effect->cameraShake->amplitudePixels, 255)),
                numerator, denominator));
            shakeX += pattern.x * amount;
            shakeY += pattern.y * amount;
        }
        if (effect->overlay) {
            auto color = effect->overlay->color;
            if (effect->overlay->mode == PresentationOverlayMode::linearFadeOut) {
                color.a = scaleAlpha(color.a, linearNumerator(*effect, elapsed),
                                      linearDenominator(*effect));
            } else if (effect->overlay->mode == PresentationOverlayMode::pulse) {
                color.a = pulseAlpha(*effect->overlay, elapsed);
            }
            if (color.a != 0U) {
                auto& overlays = effect->overlay->layer == PresentationCompositionLayer::world
                                     ? result.worldOverlays
                                     : result.finalOverlays;
                overlays.push_back({effect->id, effect->priority, color,
                                    effect->overlay->layer});
            }
        }
        if (effect->visionMask) {
            const ResolvedVisionMask candidate{effect->id, effect->priority,
                effect->visionMask->innerRadiusPixels, effect->visionMask->outerRadiusPixels,
                effect->visionMask->outsideAlpha, effect->visionMask->color};
            if (!result.visionMask || candidate.priority > result.visionMask->priority ||
                (candidate.priority == result.visionMask->priority &&
                 candidate.effectId.value() < result.visionMask->effectId.value())) {
                result.visionMask = candidate;
            }
        }
        if (effect->fade) {
            const auto& fade = *effect->fade;
            const auto denominator = linearDenominator(*effect);
            const auto numerator = denominator - linearNumerator(*effect, elapsed);
            const auto alpha = static_cast<std::uint8_t>(
                (static_cast<std::uint32_t>(fade.startAlpha) * (denominator - numerator) +
                 static_cast<std::uint32_t>(fade.endAlpha) * numerator + denominator / 2U) /
                denominator);
            auto color = fade.color;
            color.a = alpha;
            result.fades.push_back({effect->id, effect->priority, color, alpha});
        }
    }
    const auto overlayOrder = [](const ResolvedOverlay& left, const ResolvedOverlay& right) {
        return left.priority == right.priority ? left.effectId.value() < right.effectId.value()
                                                : left.priority < right.priority;
    };
    std::sort(result.worldOverlays.begin(), result.worldOverlays.end(), overlayOrder);
    std::sort(result.finalOverlays.begin(), result.finalOverlays.end(), overlayOrder);
    std::sort(result.fades.begin(), result.fades.end(), [](const auto& left, const auto& right) {
        return left.priority == right.priority ? left.effectId.value() < right.effectId.value()
                                                : left.priority < right.priority;
    });
    result.cameraOffset.x = std::clamp(shakeX, -12, 12);
    result.cameraOffset.y = std::clamp(shakeY, -12, 12);
    return result;
}

} // namespace underworld::game::presentation
