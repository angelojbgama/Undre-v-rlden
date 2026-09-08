#include "editor/content_collection.h"

#include <algorithm>

namespace underworld::editor {

std::optional<std::size_t> clampCollectionSelection(
    std::size_t size, std::optional<std::size_t> selection) noexcept {
    if (size == 0 || !selection) return std::nullopt;
    return std::min(*selection, size - 1);
}

std::optional<std::size_t> selectionAfterErase(
    std::size_t sizeAfterErase, std::size_t erasedIndex) noexcept {
    if (sizeAfterErase == 0) return std::nullopt;
    return std::min(erasedIndex, sizeAfterErase - 1);
}

std::size_t selectionAfterMove(std::size_t selection, std::size_t from,
                               std::size_t to) noexcept {
    if (selection == from) return to;
    if (from < to && selection > from && selection <= to) return selection - 1;
    if (to < from && selection >= to && selection < from) return selection + 1;
    return selection;
}

} // namespace underworld::editor
