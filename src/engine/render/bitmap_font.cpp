#include "engine/render/bitmap_font.h"

#include "engine/core/utf8.h"
#include "engine/core/color_rgba8.h"
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

core::RectI BitmapFont::glyphSource(std::uint32_t codepoint) const noexcept {
    switch (codepoint) {
    case 0x00c1: case 0x00c0: case 0x00c2: case 0x00c3: return glyphSource('A');
    case 0x00e1: case 0x00e0: case 0x00e2: case 0x00e3: return glyphSource('a');
    case 0x00c9: case 0x00ca: return glyphSource('E');
    case 0x00e9: case 0x00ea: return glyphSource('e');
    case 0x00cd: return glyphSource('I');
    case 0x00ed: return glyphSource('i');
    case 0x00d3: case 0x00d4: case 0x00d5: return glyphSource('O');
    case 0x00f3: case 0x00f4: case 0x00f5: return glyphSource('o');
    case 0x00da: return glyphSource('U');
    case 0x00fa: return glyphSource('u');
    case 0x00c7: return glyphSource('C');
    case 0x00e7: return glyphSource('c');
    default: return codepoint < 128U ? glyphSource(static_cast<char>(codepoint)) : glyphSource('?');
    }
}

namespace {
enum class Accent { none, acute, grave, circumflex, tilde, cedilla };

Accent accentFor(std::uint32_t codepoint) noexcept {
    switch (codepoint) {
    case 0x00c1: case 0x00e1: case 0x00c9: case 0x00e9: case 0x00cd: case 0x00ed:
    case 0x00d3: case 0x00f3: case 0x00da: case 0x00fa: return Accent::acute;
    case 0x00c0: case 0x00e0: return Accent::grave;
    case 0x00c2: case 0x00e2: case 0x00ca: case 0x00ea: case 0x00d4: case 0x00f4: return Accent::circumflex;
    case 0x00c3: case 0x00e3: case 0x00d5: case 0x00f5: return Accent::tilde;
    case 0x00c7: case 0x00e7: return Accent::cedilla;
    default: return Accent::none;
    }
}

void drawAccent(Renderer2D& renderer, int x, int y, Accent accent) noexcept {
    constexpr core::ColorRGBA8 white{255, 255, 255, 255};
    if (accent == Accent::cedilla) {
        renderer.fillRect({x + 2, y + 8, 1, 1}, white);
        renderer.fillRect({x + 3, y + 9, 1, 1}, white);
        return;
    }
    if (accent == Accent::acute) {
        renderer.fillRect({x + 4, y - 1, 1, 1}, white);
        renderer.fillRect({x + 3, y, 1, 1}, white);
    } else if (accent == Accent::grave) {
        renderer.fillRect({x + 2, y - 1, 1, 1}, white);
        renderer.fillRect({x + 3, y, 1, 1}, white);
    } else if (accent == Accent::circumflex) {
        renderer.fillRect({x + 2, y - 1, 1, 1}, white);
        renderer.fillRect({x + 3, y - 2, 1, 1}, white);
        renderer.fillRect({x + 4, y - 1, 1, 1}, white);
    } else if (accent == Accent::tilde) {
        renderer.fillRect({x + 2, y - 1, 1, 1}, white);
        renderer.fillRect({x + 3, y, 1, 1}, white);
        renderer.fillRect({x + 4, y - 1, 1, 1}, white);
    }
}
} // namespace

void drawText(Renderer2D& renderer, const BitmapFont& font, std::string_view text,
              int x, int y) {
    const int lineStart = x;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t before = offset;
        const auto decoded = core::decodeUtf8Codepoint(text, offset);
        if (offset == before) ++offset;
        const std::uint32_t codepoint = decoded.value_or(static_cast<std::uint32_t>('?'));
        if (codepoint == '\n') {
            x = lineStart;
            y += font.lineHeight();
        } else if (codepoint == ' ') {
            x += font.advance();
        } else {
            renderer.drawImageRegion(font.image(), font.glyphSource(codepoint), x, y);
            drawAccent(renderer, x, y, accentFor(codepoint));
            x += font.advance();
        }
    }
}

} // namespace underworld::render
