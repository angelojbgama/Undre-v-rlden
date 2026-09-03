#pragma once

#include "engine/render/sprite.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace underworld::render {

struct AnimationFrame final {
    SpriteFrame sprite{};
    std::uint32_t durationTicks{1};
    std::vector<std::string> markers{};
};

struct AnimationMarkerEvent final {
    std::string_view marker{};
    std::size_t frameIndex{};
};

class AnimationClip final {
public:
    AnimationClip(std::string id, std::shared_ptr<const SpriteSheet> sheet,
                  std::vector<AnimationFrame> frames, bool loop);

    [[nodiscard]] std::string_view id() const noexcept { return id_; }
    [[nodiscard]] const SpriteSheet& sheet() const noexcept { return *sheet_; }
    [[nodiscard]] const std::vector<AnimationFrame>& frames() const noexcept { return frames_; }
    [[nodiscard]] bool loops() const noexcept { return loop_; }
    [[nodiscard]] std::uint64_t durationTicks() const noexcept { return durationTicks_; }

private:
    std::string id_;
    std::shared_ptr<const SpriteSheet> sheet_;
    std::vector<AnimationFrame> frames_;
    bool loop_{};
    std::uint64_t durationTicks_{};
};

class Animator final {
public:
    void play(std::shared_ptr<const AnimationClip> clip, bool restart = true);
    void updateTicks(std::uint64_t ticks);
    void updateTicks(std::uint64_t ticks, std::vector<AnimationMarkerEvent>& events);
    void setPlaying(bool playing) noexcept { playing_ = playing && clip_ != nullptr; }

    [[nodiscard]] bool hasClip() const noexcept { return clip_ != nullptr; }
    [[nodiscard]] bool isPlaying() const noexcept { return playing_; }
    [[nodiscard]] std::size_t frameIndex() const noexcept { return frameIndex_; }
    [[nodiscard]] std::uint32_t elapsedFrameTicks() const noexcept { return elapsedFrameTicks_; }
    [[nodiscard]] const AnimationClip& clip() const;
    [[nodiscard]] const AnimationFrame& currentFrame() const;
    [[nodiscard]] bool finished() const noexcept { return clip_ != nullptr && !playing_; }

private:
    std::shared_ptr<const AnimationClip> clip_{};
    std::size_t frameIndex_{};
    std::uint32_t elapsedFrameTicks_{};
    bool playing_{};
};

void drawAnimator(Renderer2D& renderer, const Animator& animator,
                  core::PointI logicalPosition, bool additionalFlipX = false);

} // namespace underworld::render
