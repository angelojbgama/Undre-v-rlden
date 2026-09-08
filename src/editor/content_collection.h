#pragma once

#include <cstddef>
#include <optional>

namespace underworld::editor {

// Small, editor-only collection rules shared by typed inspectors. The data
// mutation remains in ContentWorkspaceDocument; these helpers only keep list
// selection safe when rows are inserted or removed.
[[nodiscard]] std::optional<std::size_t> clampCollectionSelection(
    std::size_t size, std::optional<std::size_t> selection) noexcept;
[[nodiscard]] std::optional<std::size_t> selectionAfterErase(
    std::size_t sizeAfterErase, std::size_t erasedIndex) noexcept;
[[nodiscard]] std::size_t selectionAfterMove(
    std::size_t selection, std::size_t from, std::size_t to) noexcept;

} // namespace underworld::editor
