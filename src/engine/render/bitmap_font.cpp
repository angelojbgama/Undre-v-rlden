#include "engine/render/bitmap_font.h"

#include "engine/render/image.h"
#include "engine/render/renderer_2d.h"

#include <stdexcept>
#include <utility>

namespace underworld::render {

BitmapFont::BitmapFont(std::shared_ptr<const Image> image) : image_(std::move(image)) {
    if (!image_ || image_->width() < 182 || image_->height() < 27) {
        throw std::invalid_argument("bitmap font image must contain the 26x3 grid of 7x9 cells");
    }
    for (int index = 0; index < 26; ++index) {
        glyphs_.emplace(static_cast<char>('A' + index), core::RectI{index * 7, 0, 7, 9});
        glyphs_.emplace(static_cast<char>('a' + index), core::RectI{index * 7, 9, 7, 9});
    }
    for (int index = 0; index < 10; ++index) {
        glyphs_.emplace(static_cast<char>('0' + index), core::RectI{index * 7, 18, 7, 9});
    }
    constexpr char punctuation[] = {'.', ',', '!', '?', '_'};
    for (int index = 0; index < 5; ++index) {
        glyphs_.emplace(punctuation[index], core::RectI{(10 + index) * 7, 18, 7, 9});
    }
    // The final visible symbol in the source asset is intentionally left unmapped.
}

core::RectI BitmapFont::glyphSource(char character) const noexcept {
    const auto found = glyphs_.find(character);
    if (found != glyphs_.end()) {
        return found->second;
    }
    return {13 * 7, 18, 7, 9}; // Explicit '?' fallback; no lookup can throw here.
}

void drawText(Renderer2D& renderer, const BitmapFont& font, std::string_view text,
              int x, int y) {
    const int lineStart = x;
    for (char character : text) {
        if (character == '\n') {
            x = lineStart;
            y += font.lineHeight();
        } else if (character == ' ') {
            x += font.advance();
        } else {
            renderer.drawImageRegion(font.image(), font.glyphSource(character), x, y);
            x += font.advance();
        }
    }
}

} // namespace underworld::render
