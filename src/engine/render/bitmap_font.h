#pragma once

#include "engine/core/geometry.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <unordered_map>

namespace underworld::render {

class Image;
class Renderer2D;

class BitmapFont final {
public:
    explicit BitmapFont(std::shared_ptr<const Image> image);

    [[nodiscard]] core::RectI glyphSource(char character) const noexcept;
    [[nodiscard]] core::RectI glyphSource(std::uint32_t codepoint) const noexcept;
    [[nodiscard]] bool usesProceduralGlyph(char character) const noexcept;
    [[nodiscard]] int advance() const noexcept { return 7; }
    [[nodiscard]] int lineHeight() const noexcept { return 9; }
    [[nodiscard]] const Image& image() const noexcept { return *image_; }

private:
    std::shared_ptr<const Image> image_;
    std::unordered_map<char, core::RectI> glyphs_;
    std::unordered_set<char> proceduralGlyphs_;
};

void drawText(Renderer2D& renderer, const BitmapFont& font, std::string_view text,
              int x, int y);

} // namespace underworld::render
