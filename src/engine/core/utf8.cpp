#include "engine/core/utf8.h"

#include <limits>

namespace underworld::core {
namespace {
constexpr std::uint32_t replacementCharacter = 0xfffd;

[[nodiscard]] bool continuation(std::uint8_t value) noexcept {
    return (value & 0xc0U) == 0x80U;
}

[[nodiscard]] bool validScalar(std::uint32_t value) noexcept {
    return value <= 0x10ffffU && !(value >= 0xd800U && value <= 0xdfffU);
}
}

std::optional<std::uint32_t> decodeUtf8Codepoint(std::string_view text,
                                                 std::size_t& offset) noexcept {
    if (offset >= text.size()) return std::nullopt;
    const auto byte = [&](std::size_t index) {
        return static_cast<std::uint8_t>(static_cast<unsigned char>(text[index]));
    };
    const std::size_t start = offset;
    const std::uint8_t first = byte(offset++);
    if (first < 0x80U) return static_cast<std::uint32_t>(first);

    std::size_t length = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xc2U && first <= 0xdfU) {
        length = 2; value = first & 0x1fU; minimum = 0x80U;
    } else if (first >= 0xe0U && first <= 0xefU) {
        length = 3; value = first & 0x0fU; minimum = 0x800U;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        length = 4; value = first & 0x07U; minimum = 0x10000U;
    } else {
        return std::nullopt;
    }
    if (start + length > text.size()) {
        offset = start + 1;
        return std::nullopt;
    }
    for (std::size_t index = 1; index < length; ++index) {
        const std::uint8_t next = byte(start + index);
        if (!continuation(next)) {
            offset = start + 1;
            return std::nullopt;
        }
        value = (value << 6U) | (next & 0x3fU);
    }
    if (value < minimum || !validScalar(value)) {
        offset = start + 1;
        return std::nullopt;
    }
    offset = start + length;
    return value;
}

bool appendUtf8Codepoint(std::string& text, std::uint32_t codepoint) {
    if (!validScalar(codepoint)) codepoint = replacementCharacter;
    if (codepoint <= 0x7fU) {
        text.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffU) {
        text.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
        text.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0xffffU) {
        text.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
        text.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        text.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else {
        text.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
        text.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
        text.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        text.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    }
    return true;
}

bool eraseLastUtf8Codepoint(std::string& text) noexcept {
    if (text.empty()) return false;
    std::size_t begin = text.size() - 1;
    while (begin > 0 && (static_cast<unsigned char>(text[begin]) & 0xc0U) == 0x80U) --begin;
    text.resize(begin);
    return true;
}

std::size_t utf8CodepointCount(std::string_view text) noexcept {
    std::size_t count = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t before = offset;
        (void)decodeUtf8Codepoint(text, offset);
        if (offset == before) ++offset;
        ++count;
    }
    return count;
}

} // namespace underworld::core
