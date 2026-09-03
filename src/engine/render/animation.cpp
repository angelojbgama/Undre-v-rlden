#include "engine/render/animation.h"

#include "engine/render/image.h"
#include "engine/render/renderer_2d.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace underworld::render {

AnimationClip::AnimationClip(std::string id, std::shared_ptr<const SpriteSheet> sheet,
                             std::vector<AnimationFrame> frames, bool loop)
    : id_(std::move(id)), sheet_(std::move(sheet)), frames_(std::move(frames)), loop_(loop) {
    if (id_.empty() || !sheet_ || frames_.empty()) {
        throw std::invalid_argument("animation clip requires an id, sheet, and frames");
    }
    const Image& image = sheet_->image();
    for (const AnimationFrame& frame : frames_) {
        const auto& source = frame.sprite.source;
        const std::int64_t right = static_cast<std::int64_t>(source.x) + source.width;
        const std::int64_t bottom = static_cast<std::int64_t>(source.y) + source.height;
        if (frame.durationTicks == 0 || source.empty() || source.x < 0 || source.y < 0 ||
            right > image.width() || bottom > image.height()) {
            throw std::invalid_argument("animation frame is invalid for its sprite sheet");
        }
        if (durationTicks_ > std::numeric_limits<std::uint64_t>::max() - frame.durationTicks) {
            throw std::length_error("animation duration overflows");
        }
        durationTicks_ += frame.durationTicks;
    }
}

void Animator::play(std::shared_ptr<const AnimationClip> clip, bool restart) {
    if (!clip) {
        throw std::invalid_argument("animator cannot play a null clip");
    }
    if (!restart && clip_ == clip) {
        playing_ = true;
        return;
    }
    clip_ = std::move(clip);
    frameIndex_ = 0;
    elapsedFrameTicks_ = 0;
    playing_ = true;
}

void Animator::updateTicks(std::uint64_t ticks) {
    std::vector<AnimationMarkerEvent> ignored;
    updateTicks(ticks, ignored);
}

void Animator::updateTicks(std::uint64_t ticks,
                           std::vector<AnimationMarkerEvent>& events) {
    if (!playing_ || !clip_ || ticks == 0) {
        return;
    }
    while (ticks > 0 && playing_) {
        const std::uint32_t duration = clip_->frames()[frameIndex_].durationTicks;
        const std::uint64_t remaining = duration - elapsedFrameTicks_;
        if (ticks < remaining) {
            elapsedFrameTicks_ += static_cast<std::uint32_t>(ticks);
            return;
        }
        ticks -= remaining;
        elapsedFrameTicks_ = 0;
        if (frameIndex_ + 1U < clip_->frames().size()) {
            ++frameIndex_;
        } else if (clip_->loops()) {
            frameIndex_ = 0;
        } else {
            playing_ = false;
        }
        if (playing_) {
            for (const std::string& marker : clip_->frames()[frameIndex_].markers) {
                events.push_back({marker, frameIndex_});
            }
        }
    }
}

const AnimationClip& Animator::clip() const {
    if (!clip_) {
        throw std::logic_error("animator has no clip");
    }
    return *clip_;
}

const AnimationFrame& Animator::currentFrame() const {
    return clip().frames()[frameIndex_];
}

void drawAnimator(Renderer2D& renderer, const Animator& animator,
                  core::PointI logicalPosition, bool additionalFlipX) {
    drawSprite(renderer, animator.clip().sheet(), animator.currentFrame().sprite,
               logicalPosition, additionalFlipX);
}

} // namespace underworld::render
