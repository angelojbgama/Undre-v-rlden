#pragma once

#include <string>
#include <string_view>

namespace underworld::editor {

[[nodiscard]] int measureTextWidth(std::string_view text, int advance = 7) noexcept;
[[nodiscard]] std::string ellipsizeText(std::string_view text, int maxWidth,
                                        int advance = 7);
[[nodiscard]] std::string fitText(std::string_view text, int maxWidth,
                                  bool fromEnd = false, int advance = 7);

} // namespace underworld::editor
