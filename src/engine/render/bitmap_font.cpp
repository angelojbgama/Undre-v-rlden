#include "engine/render/bitmap_font.h"

#include "engine/core/utf8.h"
#include "engine/core/color_rgba8.h"
#include "engine/render/image.h"
#include "engine/render/renderer_2d.h"

#include <stdexcept>
#include <array>
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
    // The compact shipped sheet has a handful of punctuation glyphs. Every
    // printable ASCII code nevertheless gets a stable, non-question-mark
    // source cell so editor paths and diagnostics never turn into '?' merely
    // because they contain a slash, colon, bracket or percent sign.
    constexpr char safePunctuation[] = {'.', ',', '!', '_'};
    for (int code = 32; code <= 126; ++code) {
        if (glyphs_.contains(static_cast<char>(code))) continue;
        glyphs_.emplace(static_cast<char>(code),
                        glyphs_.at(safePunctuation[(code - 32) % 4]));
        if (code != ' ') proceduralGlyphs_.emplace(static_cast<char>(code));
    }
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

bool BitmapFont::usesProceduralGlyph(char character) const noexcept {
    return proceduralGlyphs_.contains(character);
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

std::array<std::string_view, 7> proceduralPattern(char character) noexcept {
    switch (character) {
    case '"': return {"01010", "01010", "00000", "00000", "00000", "00000", "00000"};
    case '#': return {"01010", "11111", "01010", "11111", "01010", "00000", "00000"};
    case '$': return {"00100", "01110", "10100", "01110", "00101", "11100", "00100"};
    case '%': return {"11001", "11010", "00100", "01011", "10011", "00000", "00000"};
    case '&': return {"01100", "10010", "10100", "01010", "10101", "10010", "01101"};
    case '\'': return {"00100", "00100", "00000", "00000", "00000", "00000", "00000"};
    case '(': return {"00110", "01000", "10000", "10000", "10000", "01000", "00110"};
    case ')': return {"01100", "00010", "00001", "00001", "00001", "00010", "01100"};
    case '*': return {"00100", "10101", "01110", "10101", "00100", "00000", "00000"};
    case '+': return {"00100", "00100", "11111", "00100", "00100", "00000", "00000"};
    case '-': return {"00000", "00000", "11111", "00000", "00000", "00000", "00000"};
    case '/': return {"00001", "00010", "00100", "01000", "10000", "00000", "00000"};
    case ':': return {"00000", "00100", "00000", "00000", "00100", "00000", "00000"};
    case ';': return {"00000", "00100", "00000", "00000", "00100", "01000", "00000"};
    case '<': return {"00010", "00100", "01000", "10000", "01000", "00100", "00010"};
    case '=': return {"00000", "11111", "00000", "11111", "00000", "00000", "00000"};
    case '>': return {"01000", "00100", "00010", "00001", "00010", "00100", "01000"};
    case '@': return {"01110", "10001", "10111", "10101", "10111", "10000", "01111"};
    case '[': return {"01110", "01000", "01000", "01000", "01000", "01000", "01110"};
    case '\\': return {"10000", "01000", "00100", "00010", "00001", "00000", "00000"};
    case ']': return {"01110", "00010", "00010", "00010", "00010", "00010", "01110"};
    case '^': return {"00100", "01010", "10001", "00000", "00000", "00000", "00000"};
    case '`': return {"01000", "00100", "00000", "00000", "00000", "00000", "00000"};
    case '{': return {"00110", "00100", "01000", "00100", "00100", "00100", "00110"};
    case '|': return {"00100", "00100", "00100", "00100", "00100", "00100", "00100"};
    case '}': return {"01100", "00100", "00010", "00100", "00100", "00100", "01100"};
    case '~': return {"00000", "01001", "10110", "00000", "00000", "00000", "00000"};
    default: return {"00000", "00000", "00000", "00000", "00000", "00000", "00000"};
    }
}

void drawProceduralGlyph(Renderer2D& renderer, char character, int x, int y) noexcept {
    constexpr core::ColorRGBA8 white{255, 255, 255, 255};
    const auto rows = proceduralPattern(character);
    for (std::size_t row = 0; row < rows.size(); ++row) {
        for (std::size_t column = 0; column < rows[row].size(); ++column) {
            if (rows[row][column] == '1')
                renderer.fillRect({x + 1 + static_cast<int>(column),
                                   y + static_cast<int>(row), 1, 1}, white);
        }
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
            if (codepoint < 128U && font.usesProceduralGlyph(static_cast<char>(codepoint)))
                drawProceduralGlyph(renderer, static_cast<char>(codepoint), x, y);
            else renderer.drawImageRegion(font.image(), font.glyphSource(codepoint), x, y);
            drawAccent(renderer, x, y, accentFor(codepoint));
            x += font.advance();
        }
    }
}

} // namespace underworld::render
