#include "editor/editor_text_layout.h"

#include "engine/core/utf8.h"

#include <algorithm>
#include <vector>

namespace underworld::editor {
namespace {
struct CodepointSlice final { std::size_t begin{}; std::size_t end{}; };

std::vector<CodepointSlice> slices(std::string_view text) {
    std::vector<CodepointSlice> result;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto begin = offset;
        const auto decoded = core::decodeUtf8Codepoint(text, offset);
        if (offset == begin) ++offset;
        result.push_back({begin, offset});
        static_cast<void>(decoded);
    }
    return result;
}
}

int measureTextWidth(std::string_view text, int advance) noexcept {
    return static_cast<int>(core::utf8CodepointCount(text)) * std::max(0, advance);
}

std::string ellipsizeText(std::string_view text, int maxWidth, int advance) {
    if (maxWidth <= 0) return {};
    if (measureTextWidth(text, advance) <= maxWidth) return std::string(text);
    constexpr std::string_view ellipsis = "...";
    const int ellipsisWidth = measureTextWidth(ellipsis, advance);
    if (ellipsisWidth >= maxWidth) {
        const auto parts = slices(ellipsis);
        const auto count = static_cast<std::size_t>(maxWidth / std::max(1, advance));
        return std::string(ellipsis.substr(0, std::min(count, parts.size())));
    }
    const auto parts = slices(text);
    const auto keep = static_cast<std::size_t>((maxWidth - ellipsisWidth) /
                                                std::max(1, advance));
    if (keep == 0) return std::string(ellipsis);
    const auto count = std::min(keep, parts.size());
    return std::string(text.substr(0, parts[count - 1].end)) + std::string(ellipsis);
}

std::string fitText(std::string_view text, int maxWidth, bool fromEnd, int advance) {
    if (maxWidth <= 0) return {};
    if (measureTextWidth(text, advance) <= maxWidth) return std::string(text);
    if (!fromEnd) return ellipsizeText(text, maxWidth, advance);
    constexpr std::string_view ellipsis = "...";
    const int ellipsisWidth = measureTextWidth(ellipsis, advance);
    if (ellipsisWidth >= maxWidth) return std::string(ellipsis.substr(0,
        std::min<std::size_t>(ellipsis.size(), static_cast<std::size_t>(maxWidth / std::max(1, advance)))));
    const auto parts = slices(text);
    const auto keep = std::min(parts.size(), static_cast<std::size_t>((maxWidth - ellipsisWidth) /
                                                                       std::max(1, advance)));
    if (keep == 0) return std::string(ellipsis);
    return std::string(ellipsis) + std::string(text.substr(parts[parts.size() - keep].begin));
}

} // namespace underworld::editor
